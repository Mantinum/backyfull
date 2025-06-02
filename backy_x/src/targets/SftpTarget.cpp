#include "targets/SftpTarget.h"
#include "core/IStorageTarget.h" // Required for IStorageTarget::FileMetadata
#include <iostream> // For std::cerr/std::cout
#include <fstream>  // For std::ifstream
#include <filesystem> // For std::filesystem::path (C++17)
#include <vector>   // For directory creation components
#include <sstream>  // For std::istringstream (used in parseSshAuthTypes and listFiles)
#include <iomanip>  // For std::get_time (potentially, if not using Qt)
#include <algorithm> // for std::transform

#include <QString> // For service name and password with CredentialManager
#include <QDebug> // For qDebug, qWarning
#include <QByteArray> // For QByteArray and toUtf8()
#include <QUrl> // For QUrl and QUrl::toPercentEncoding
#include <QDateTime> // For SFTP date parsing
#include <QLocale>   // For SFTP date parsing
#include <QStringList> // For new parseSftpDate
#include "util/CredentialManager.h" // For createPlatformCredentialManager and CredentialManager class

// For libcurl (SFTP operations)
#include <curl/curl.h>

// Static callback function for libcurl to read data for uploading
static size_t read_callback(char *ptr, size_t size, size_t nmemb, void *stream);
// Static callback function for libcurl to write SFTP listing data
static size_t sftpListWriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
// Generic file write callback for libcurl
static size_t fileWriteCallback(void*contents, size_t size, size_t nmemb, void*userp);
// Helper function to parse date strings from SFTP listing
static std::chrono::system_clock::time_point parseSftpDate(const QString& monthToken, const QString& dayToken, const QString& timeOrYearToken);


// Helper functions for SFTP path and URL construction
static std::string buildSftpAbsolutePath(const std::string& basePath, const std::string& relativePath);
// Simplified: builds only the sftp://host:port part. Path will be appended after escaping.
static std::string buildSftpBaseUrl(const std::string& host, int port);

// Helper function to parse SSH authentication types string
static long parseSshAuthTypes(const std::string& authTypesStr) {
    if (authTypesStr.empty() || authTypesStr == "any") {
        return CURLSSH_AUTH_ANY;
    }
    long types = 0;
    std::string token;
    std::istringstream tokenStream(authTypesStr);
    while (std::getline(tokenStream, token, ',')) {
        token.erase(0, token.find_first_not_of(" \t\n\r\f\v"));
        token.erase(token.find_last_not_of(" \t\n\r\f\v") + 1);
        if (token == "password") types |= CURLSSH_AUTH_PASSWORD;
        else if (token == "publickey") types |= CURLSSH_AUTH_PUBLICKEY;
        else if (token == "agent") types |= CURLSSH_AUTH_AGENT;
        else if (token == "keyboard-interactive") types |= CURLSSH_AUTH_KEYBOARD;
        else if (token == "any") types |= CURLSSH_AUTH_ANY;
        else if (!token.empty()) {
            std::cerr << "SftpTarget: Unknown or empty SSH auth type specified: '" << token << "'" << std::endl;
        }
    }
    return types == 0 ? CURLSSH_AUTH_ANY : types;
}

static size_t read_callback(char *ptr, size_t size, size_t nmemb, void *stream) {
    std::ifstream *fileStream = static_cast<std::ifstream*>(stream);
    fileStream->read(ptr, size * nmemb);
    size_t bytesRead = fileStream->gcount();
    if (bytesRead == 0 && fileStream->eof()) {
    } else if (bytesRead < size * nmemb && fileStream->fail() && !fileStream->eof()) {
        std::cerr << "SftpTarget::read_callback: Read error from input file." << std::endl;
        return CURL_READFUNC_ABORT;
    }
    return bytesRead;
}

static std::string joinPaths(std::string basePath, std::string relativePath) {
    while (basePath.length() > 1 && basePath.back() == '/') basePath.pop_back();
    if ((basePath.empty() || basePath == "/") && !relativePath.empty() && relativePath.front() == '/') {
        relativePath = relativePath.substr(1);
    } else if (!basePath.empty() && basePath != "/" && !relativePath.empty() && relativePath.front() != '/') {
        basePath += "/";
    } else if (!basePath.empty() && basePath != "/" && !relativePath.empty() && relativePath.front() == '/'){
        if (relativePath.front() == '/') relativePath = relativePath.substr(1);
         basePath += "/";
    } else if (basePath == "/" && relativePath.empty()){
        return "/";
    }
    std::string fullPath = basePath + relativePath;
    size_t doubleSlashPos = fullPath.find("//");
    if (fullPath.length() > 0 && fullPath.front() == '/' && doubleSlashPos == 0) {
        fullPath = fullPath.substr(1);
    }
    if (!fullPath.empty() && fullPath.front() != '/') fullPath = "/" + fullPath;
    if (fullPath.empty()) return "/";
    std::string result_path;
    bool last_was_slash = false;
    for (char ch : fullPath) {
        if (ch == '/') {
            if (!last_was_slash) result_path += ch;
            last_was_slash = true;
        } else {
            result_path += ch;
            last_was_slash = false;
        }
    }
    if (result_path.empty() && !fullPath.empty()) return "/";
    if (result_path.length() > 1 && result_path.back() == '/') result_path.pop_back();
    return result_path;
}

static std::string buildSftpAbsolutePath(const std::string& basePath, const std::string& relativePath) {
    return joinPaths(basePath, relativePath);
}

// Simplified: builds only the sftp://host:port part. Path will be appended after escaping.
static std::string buildSftpBaseUrl(const std::string& host, int port) {
    return "sftp://" + host + ":" + std::to_string(port);
}

SftpTarget::SftpTarget(const std::map<std::string, std::string>& config)
    : m_port(22),
      m_curlHandle(nullptr),
      m_credentialManager(nullptr),
      m_sshSkipHostVerification(false),
      m_sshAuthTypes(CURLSSH_AUTH_ANY),
      m_verboseLogging(false),
      m_properlyConfigured(true) {
    m_curlHandle = nullptr; 
    m_credentialManager = std::unique_ptr<CredentialManager>(createPlatformCredentialManager());
    auto itHost = config.find("host");
    if (itHost != config.end() && !itHost->second.empty()) m_host = itHost->second;
    else { std::cerr << "SftpTarget: Critical configuration 'host' is missing or empty." << std::endl; m_properlyConfigured = false; }
    auto itUser = config.find("username");
    if (itUser != config.end() && !itUser->second.empty()) m_username = itUser->second;
    else { std::cerr << "SftpTarget: Critical configuration 'username' is missing or empty." << std::endl; m_properlyConfigured = false; }
    auto itPort = config.find("port");
    if (itPort != config.end()) {
        try { m_port = std::stoi(itPort->second); }
        catch (const std::exception& e) { std::cerr << "SftpTarget: Invalid port number '" << itPort->second << "'. Using default 22. Error: " << e.what() << std::endl; m_port = 22; }
    } else { std::cout << "SftpTarget: 'port' not found in configuration, defaulting to 22." << std::endl; }
    QString serviceName = QString("sftp_%1_%2").arg(QString::fromStdString(m_host)).arg(m_port);
    QString qUsername = QString::fromStdString(m_username);
    auto itPass = config.find("password");
    if (itPass != config.end() && !itPass->second.empty()) {
        m_password = itPass->second;
        if (m_credentialManager) {
            if (!m_credentialManager->storeSecret(serviceName, qUsername, QString::fromStdString(m_password)))
                 std::cerr << "SftpTarget: Failed to store password via CredentialManager." << std::endl;
        }
    } else {
        if (m_credentialManager) {
            std::optional<QString> retrievedPassword = m_credentialManager->retrieveSecret(serviceName, qUsername);
            if (retrievedPassword) m_password = retrievedPassword->toStdString();
            else std::cout << "SftpTarget: Failed to retrieve password from CredentialManager or no password stored." << std::endl;
        } else std::cerr << "SftpTarget: CredentialManager not available." << std::endl;
    }
    auto itPath = config.find("remoteBasePath");
    if (itPath != config.end()) m_remoteBasePath = itPath->second;
    else { m_remoteBasePath = "/"; std::cout << "SftpTarget: 'remoteBasePath' not found, defaulting to '/'." << std::endl; }
    std::cout << "SftpTarget configured for host: " << m_host << ", user: " << m_username << ", path: " << m_remoteBasePath << ", port: " << m_port << std::endl;
    if (config.count("ssh_public_key_path")) m_sshPublicKeyPath = config.at("ssh_public_key_path");
    if (config.count("ssh_private_key_path")) m_sshPrivateKeyPath = config.at("ssh_private_key_path");
    if (config.count("ssh_key_passphrase")) m_sshKeyPassphrase = config.at("ssh_key_passphrase");
    if (config.count("ssh_known_hosts_path")) m_sshKnownHostsPath = config.at("ssh_known_hosts_path");
    if (config.count("ssh_skip_host_verification")) {
        std::string val = config.at("ssh_skip_host_verification"); std::transform(val.begin(), val.end(), val.begin(), ::tolower);
        if (val == "true" || val == "1") m_sshSkipHostVerification = true;
    }
    if (config.count("ssh_auth_types") && !config.at("ssh_auth_types").empty()) m_sshAuthTypes = parseSshAuthTypes(config.at("ssh_auth_types"));
    else m_sshAuthTypes = CURLSSH_AUTH_ANY;
    if (config.count("verbose_logging")) {
        std::string val = config.at("verbose_logging"); std::transform(val.begin(), val.end(), val.begin(), ::tolower);
        if (val == "true" || val == "1") m_verboseLogging = true;
    }
    if (m_verboseLogging) std::cout << "SftpTarget: Verbose logging enabled." << std::endl;
}

SftpTarget::~SftpTarget() {
    if (m_curlHandle) { curl_easy_cleanup(m_curlHandle); m_curlHandle = nullptr; }
    std::cout << "SftpTarget destroyed." << std::endl;
}

bool SftpTarget::beginSession() {
    lastError_.clear();
    if (!m_properlyConfigured) {
        lastError_ = "SFTP Target not properly configured.";
        // std::cerr is kept for now, but lastError_ is the primary mechanism.
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return false;
    }
    if (m_curlHandle) {
        curl_easy_cleanup(m_curlHandle);
        m_curlHandle = nullptr;
    }
    m_curlHandle = curl_easy_init();
    if (!m_curlHandle) {
        lastError_ = "curl_easy_init() failed.";
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return false;
    }

    CURLcode res_setopt; // Renamed from 'res' to avoid conflict if any other 'res' is used locally

    // SSH host key verification logic:
    // If skipping verification, set known_hosts to NULL (or /dev/null as fallback).
    // Otherwise, if a path is provided, use it. Otherwise, rely on libcurl's default.
    if (m_sshSkipHostVerification) {
        res_setopt = curl_easy_setopt(m_curlHandle, CURLOPT_SSH_KNOWNHOSTS, NULL);
        if (res_setopt != CURLE_OK) { // If NULL is not accepted, try /dev/null
            res_setopt = curl_easy_setopt(m_curlHandle, CURLOPT_SSH_KNOWNHOSTS, "/dev/null");
        }
        if (res_setopt != CURLE_OK) {
             std::cerr << "SftpTarget: Warning - Could not set CURLOPT_SSH_KNOWNHOSTS to NULL or /dev/null to skip host verification: " << curl_easy_strerror(res_setopt) << std::endl;
        } else {
            std::cout << "SftpTarget: SSH host verification is being skipped." << std::endl;
        }
    } else if (!m_sshKnownHostsPath.empty()) {
        res_setopt = curl_easy_setopt(m_curlHandle, CURLOPT_SSH_KNOWNHOSTS, m_sshKnownHostsPath.c_str());
        if (res_setopt != CURLE_OK) {
            std::cerr << "SftpTarget: Warning - Failed to set custom CURLOPT_SSH_KNOWNHOSTS path: " << m_sshKnownHostsPath << " Error: " << curl_easy_strerror(res_setopt) << std::endl;
        }
    } else {
        // Using default known_hosts file. No specific setopt for this, libcurl handles it.
        std::cout << "SftpTarget: SSH host verification using default known_hosts file." << std::endl;
    }


    auto setopt_check = [&](CURLoption opt, auto val, const char* name, bool critical = true) {
        res_setopt = curl_easy_setopt(m_curlHandle, opt, val);
        if (res_setopt != CURLE_OK) {
            // Set lastError_ only if it's a critical option or if lastError_ is currently empty (to avoid overwriting a critical error with a non-critical one)
            if (critical || lastError_.empty()) {
                lastError_ = "Failed to set SFTP option " + std::string(name) + ": " + curl_easy_strerror(res_setopt);
            }
            std::cerr << "SftpTarget: Failed to set " << name << ": " << curl_easy_strerror(res_setopt) << (critical ? " (Critical)" : " (Non-critical)") << std::endl;
        }
        return res_setopt == CURLE_OK;
    };

    if (!setopt_check(CURLOPT_USERNAME, m_username.c_str(), "CURLOPT_USERNAME")) goto error;
    if (!m_password.empty() && !setopt_check(CURLOPT_PASSWORD, m_password.c_str(), "CURLOPT_PASSWORD")) goto error;
    if (!setopt_check(CURLOPT_PORT, (long)m_port, "CURLOPT_PORT")) goto error;

    setopt_check(CURLOPT_VERBOSE, m_verboseLogging ? 1L : 0L, "CURLOPT_VERBOSE", false); // Verbose is not critical

    if (m_sshAuthTypes != CURLSSH_AUTH_ANY) {
        if (!setopt_check(CURLOPT_SSH_AUTH_TYPES, m_sshAuthTypes, "CURLOPT_SSH_AUTH_TYPES")) goto error;
    }
    if (!m_sshPrivateKeyPath.empty()) {
        if (!setopt_check(CURLOPT_SSH_PRIVATE_KEYFILE, m_sshPrivateKeyPath.c_str(), "CURLOPT_SSH_PRIVATE_KEYFILE")) goto error;
    }
    if (!m_sshPublicKeyPath.empty()) {
        if (!setopt_check(CURLOPT_SSH_PUBLIC_KEYFILE, m_sshPublicKeyPath.c_str(), "CURLOPT_SSH_PUBLIC_KEYFILE")) goto error;
    }
    if (!m_sshKeyPassphrase.empty() && !m_sshPrivateKeyPath.empty()) {
        if (!setopt_check(CURLOPT_KEYPASSWD, m_sshKeyPassphrase.c_str(), "CURLOPT_KEYPASSWD")) goto error;
    }
    // Note: CURLOPT_SSH_KNOWNHOSTS is handled above, not part of the main setopt_check loop with goto error for critical failure.
    // Its failure is logged as a warning but doesn't necessarily stop the session attempt.

    std::cout << "SftpTarget: Session options configured. Connection will be established on first operation." << std::endl;
    // If we reached here, critical options were set. Clear lastError_ if it was set by a non-critical option.
    // However, the setopt_check lambda logic with `critical || lastError_.empty()` should prevent overwriting critical errors.
    // If a critical error occurred, `goto error` would have been taken.
    // So, if lastError_ is set here, it must be from a non-critical option that we allowed to pass.
    // For a truly "successful" beginSession configuration, lastError_ should be clear.
    if (!lastError_.empty()) { // Implies a non-critical setopt failed (e.g. verbose)
        std::cout << "SftpTarget: Note - some non-critical options failed to set. Last recorded non-critical error: " << lastError_ << std::endl;
        // Decide if this should be cleared or not. If getLastError() is called now, it would show this.
        // Let's clear it to indicate readiness for operations, as critical ones passed.
        lastError_.clear();
    }
    return true;
error:
    // lastError_ should have been set by the failing critical setopt_check
    if (m_curlHandle) {
        curl_easy_cleanup(m_curlHandle);
        m_curlHandle = nullptr;
    }
    return false;
}

bool SftpTarget::sendFile(const std::string& localPath, const FileMetadata& metadata) {
    lastError_.clear();
    std::cout << "SftpTarget: sendFile(" << localPath << ", remote_path: " << metadata.name << ") called." << std::endl;
    if (!m_curlHandle) {
        lastError_ = "SFTP sendFile: Session not begun or curl handle not initialized.";
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return false;
    }
    std::ifstream sourceFile(localPath, std::ios::binary);
    if (!sourceFile.is_open()) {
        lastError_ = "SFTP sendFile: Failed to open local source file: " + localPath;
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return false;
    }
    sourceFile.seekg(0, std::ios::end);
    curl_off_t fileSize = sourceFile.tellg();
    sourceFile.seekg(0, std::ios::beg);
    if (fileSize < 0) {
        lastError_ = "SFTP sendFile: Failed to get size of local source file: " + localPath;
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        sourceFile.close();
        return false;
    }
    
    std::string full_logical_path = joinPaths(m_remoteBasePath, metadata.name);
    if (full_logical_path.empty() && metadata.name != "/") { // Allow "/" for specific root operations if any, but generally expect non-empty
        lastError_ = "SFTP sendFile: Generated logical path is empty for remote item: " + metadata.name;
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        sourceFile.close();
        return false;
    }

    // curl_easy_escape requires an initialized curl handle.
    if (!m_curlHandle) { // Should have been caught earlier, but as a safeguard here.
        lastError_ = "SFTP sendFile: curl handle not initialized before escaping path.";
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        sourceFile.close();
        return false;
    }

    char* escaped_path_c = curl_easy_escape(m_curlHandle, full_logical_path.c_str(), 0);
    if (!escaped_path_c) {
        lastError_ = "SFTP sendFile: curl_easy_escape failed for path: " + full_logical_path;
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        sourceFile.close();
        return false;
    }
    std::string escaped_path(escaped_path_c);
    curl_free(escaped_path_c);

    // The escaped_path will start with %2F if full_logical_path started with /.
    // Base URL should be sftp://host:port
    std::string remoteUrl = buildSftpBaseUrl(m_host, m_port) + escaped_path;
    std::cout << "SftpTarget: Uploading to URL: " << remoteUrl << std::endl;

    CURLcode res_setopt_url = curl_easy_setopt(m_curlHandle, CURLOPT_URL, remoteUrl.c_str());
    if (res_setopt_url != CURLE_OK) {
        lastError_ = "SFTP sendFile: Failed to set URL '" + remoteUrl + "': " + std::string(curl_easy_strerror(res_setopt_url));
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        sourceFile.close();
        return false;
    }

    curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 1L);
    // CURLOPT_FTP_CREATE_MISSING_DIRS is useful for SFTP too, creates parent dirs for the file.
    curl_easy_setopt(m_curlHandle, CURLOPT_FTP_CREATE_MISSING_DIRS, (long)CURLFTP_CREATE_DIR_RETRY);
    curl_easy_setopt(m_curlHandle, CURLOPT_READFUNCTION, read_callback);
    curl_easy_setopt(m_curlHandle, CURLOPT_READDATA, &sourceFile);
    curl_easy_setopt(m_curlHandle, CURLOPT_INFILESIZE_LARGE, fileSize);

    CURLcode res_perform = curl_easy_perform(m_curlHandle);

    sourceFile.close();
    // It's good practice to reset options that might affect other types of requests if the handle is reused.
    curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 0L);
    curl_easy_setopt(m_curlHandle, CURLOPT_READFUNCTION, NULL); // Reset read callback
    curl_easy_setopt(m_curlHandle, CURLOPT_READDATA, NULL);    // Reset read data pointer

    if (res_perform != CURLE_OK) {
        lastError_ = "SFTP sendFile: Upload failed for " + remoteUrl + ": " + curl_easy_strerror(res_perform);
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return false;
    }
    std::cout << "SftpTarget: File sent successfully." << std::endl;
    return true;
}

bool SftpTarget::deleteFile(const std::string& remotePath) {
    lastError_.clear();
    std::cout << "SftpTarget: deleteFile(" << remotePath << ") called." << std::endl;
    if (!m_curlHandle) {
        lastError_ = "SFTP deleteFile: Session not begun or curl handle not initialized.";
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return false;
    }

    // The URL for QUOTE commands is typically the base SFTP URL sftp://host:port/
    // The path for 'rm' is specified in the command itself and should be absolute on the server.
    std::string contextUrl = buildSftpBaseUrl(m_host, m_port) + "/"; // Context URL for QUOTE commands.

    std::string absolutePathForRm = buildSftpAbsolutePath(m_remoteBasePath, remotePath);
    // Escape quotes within the absolutePathForRm if any, though filenames typically don't have them.
    // For simplicity, assuming paths don't contain double quotes that would break the command string.
    // A more robust solution would escape characters in absolutePathForRm if needed.
    std::string rmCommand = "rm \"" + absolutePathForRm + "\""; // Quoting handles spaces

    struct curl_slist *customCommands = NULL;
    customCommands = curl_slist_append(customCommands, rmCommand.c_str());
    if (!customCommands) {
        lastError_ = "SFTP deleteFile: Failed to create slist for QUOTE command.";
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return false;
    }

    CURLcode res_setopt_url = curl_easy_setopt(m_curlHandle, CURLOPT_URL, contextUrl.c_str());
    if (res_setopt_url != CURLE_OK) {
        lastError_ = "SFTP deleteFile: Failed to set context URL: " + std::string(curl_easy_strerror(res_setopt_url));
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        curl_slist_free_all(customCommands);
        return false;
    }

    curl_easy_setopt(m_curlHandle, CURLOPT_QUOTE, customCommands);
    // Ensure other modes that might conflict with QUOTE are off (e.g. UPLOAD)
    curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 0L);
    curl_easy_setopt(m_curlHandle, CURLOPT_DIRLISTONLY, 0L);

    std::cout << "SftpTarget: Attempting to delete. Context URL: " << contextUrl << ", Command: " << rmCommand << std::endl;
    CURLcode res_perform = curl_easy_perform(m_curlHandle);

    curl_slist_free_all(customCommands);
    curl_easy_setopt(m_curlHandle, CURLOPT_QUOTE, NULL); // Reset QUOTE

    if (res_perform != CURLE_OK) {
        // This error might indicate issues like "file not found", "permission denied", or "is a directory".
        lastError_ = "SFTP deleteFile command failed for path '" + remotePath + "' (absolute: '" + absolutePathForRm + "'): " + curl_easy_strerror(res_perform);
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return false;
    }

    // Note: For SFTP, CURLE_OK for a QUOTE 'rm' command doesn't always guarantee the file was actually deleted
    // or that it existed. Some servers might not return an error code via libcurl for "file not found" on 'rm'.
    // True verification would involve trying to list/stat the file afterwards.
    // For this implementation, CURLE_OK is treated as success.
    std::cout << "SftpTarget: File deletion command successfully executed for " << remotePath << std::endl;
    return true;
}

bool SftpTarget::endSession() {
    lastError_.clear(); // Clearing error on successful endSession or if no session to end.
    std::cout << "SftpTarget: endSession() called." << std::endl;
    if (m_curlHandle) { curl_easy_cleanup(m_curlHandle); m_curlHandle = nullptr; }
    std::cout << "SftpTarget: Session ended." << std::endl;
    return true;
}

static size_t sftpListWriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Updated parseSftpDate function
static std::chrono::system_clock::time_point parseSftpDate(const QString& monthToken, const QString& dayToken, const QString& timeOrYearToken) {
    QLocale cLocale(QLocale::English, QLocale::UnitedStates);
    QString yearStr, timeStr;
    bool hasYear = false;

    if (timeOrYearToken.contains(':')) {
        timeStr = timeOrYearToken;
    } else {
        yearStr = timeOrYearToken;
        hasYear = true;
    }

    QString dateString;
    QDateTime dateTime;

    if (hasYear) {
        dateString = QString("%1 %2 %3").arg(monthToken, dayToken, yearStr);
        dateTime = cLocale.toDateTime(dateString, "MMM d yyyy");
        if (!dateTime.isValid()) {
            dateTime = cLocale.toDateTime(dateString, "MMM dd yyyy");
        }
         if (dateTime.isValid()) { // If year was provided, assume midnight if no time
            dateTime.setTime(QTime(0,0,0));
        }
    } else {
        dateString = QString("%1 %2 %3").arg(monthToken, dayToken, timeStr);
        dateTime = cLocale.toDateTime(dateString, "MMM d hh:mm");
         if (!dateTime.isValid()) {
            dateTime = cLocale.toDateTime(dateString, "MMM dd hh:mm");
        }

        if (dateTime.isValid()) {
            QDate currentDate = QDateTime::currentDateTime().date();
            dateTime.setDate(QDate(currentDate.year(), dateTime.date().month(), dateTime.date().day()));

            if (dateTime > QDateTime::currentDateTime().addDays(1)) {
                dateTime = dateTime.addYears(-1);
            }
        }
    }

    if (!dateTime.isValid()) {
        qWarning() << "SFTP Date Parse: Could not parse date string:" << dateString << "from tokens [" << monthToken << dayToken << timeOrYearToken << "]";
        return std::chrono::system_clock::from_time_t(0);
    }
    return std::chrono::system_clock::from_time_t(dateTime.toSecsSinceEpoch());
}


static size_t fileWriteCallback(void*contents, size_t size, size_t nmemb, void*userp) {
    std::ofstream* outFile = static_cast<std::ofstream*>(userp);
    outFile->write(static_cast<const char*>(contents), size * nmemb);
    return outFile->good() ? size * nmemb : 0;
}

std::vector<FileMetadata> SftpTarget::listFiles(const std::string& remotePath) {
    lastError_.clear();
    std::vector<FileMetadata> resultFiles;
    if (!m_curlHandle) {
        lastError_ = "SFTP listFiles: Session not begun or curl handle not initialized.";
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return resultFiles;
    }

    std::string logical_path_for_listing = joinPaths(m_remoteBasePath, remotePath);
    if (logical_path_for_listing.empty() && remotePath != "/") {
        lastError_ = "SFTP listFiles: Generated logical path is empty for remote item: " + remotePath;
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return resultFiles;
    }
    // Ensure directory paths for listing end with a slash BEFORE escaping,
    // unless it's the root path which is already just "/".
    if (logical_path_for_listing != "/" && logical_path_for_listing.back() != '/') {
        logical_path_for_listing += '/';
    }

    if (!m_curlHandle) {
        lastError_ = "SFTP listFiles: curl handle not initialized before escaping path.";
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return resultFiles;
    }
    char* escaped_dir_path_c = curl_easy_escape(m_curlHandle, logical_path_for_listing.c_str(), 0);
    if (!escaped_dir_path_c) {
        lastError_ = "SFTP listFiles: curl_easy_escape failed for path: " + logical_path_for_listing;
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return resultFiles;
    }
    std::string escaped_dir_path(escaped_dir_path_c);
    curl_free(escaped_dir_path_c);

    std::string dirUrl = buildSftpBaseUrl(m_host, m_port) + escaped_dir_path;
    // std::cout << "SftpTarget: Listing directory URL: " << dirUrl << std::endl; // Can be verbose

    CURLcode res_opt = curl_easy_setopt(m_curlHandle, CURLOPT_URL, dirUrl.c_str());
    if (res_opt != CURLE_OK) {
        lastError_ = "SFTP listFiles: Failed to set URL '" + dirUrl + "': " + std::string(curl_easy_strerror(res_opt));
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return resultFiles;
    }

    std::string listingData;
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, sftpListWriteCallback);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, &listingData);
    // For full listing like 'ls -l', CURLOPT_DIRLISTONLY should be 0L.
    // If only names are needed, it would be 1L.
    curl_easy_setopt(m_curlHandle, CURLOPT_DIRLISTONLY, 0L);
    // Ensure no prior custom request (like for deleteFile's QUOTE) interferes.
    curl_easy_setopt(m_curlHandle, CURLOPT_CUSTOMREQUEST, NULL);
    // Ensure UPLOAD mode is off, as this is a listing operation.
    curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 0L);


    CURLcode res_perform = curl_easy_perform(m_curlHandle);

    // Reset options that were specific to this request for cleanliness,
    // though curl_easy_perform itself should handle state for each call.
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, NULL);
    // No need to reset CURLOPT_DIRLISTONLY or CURLOPT_CUSTOMREQUEST if they are set per-call basis.

    if (res_perform != CURLE_OK) {
        lastError_ = "SFTP listFiles: curl_easy_perform() failed for URL " + dirUrl + ": " + curl_easy_strerror(res_perform);
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return resultFiles;
    }

    // std::cout << "SftpTarget: Raw listing data:\n" << listingData << std::endl; // Very verbose
    std::istringstream iss(listingData); std::string line;
    while (std::getline(iss, line)) {
        if (line.empty() || line[0] == '\r' || line[0] == '\n') continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::vector<std::string> tokens; std::string token; std::istringstream lineStream(line);
        while (lineStream >> token) tokens.push_back(token);
        if (tokens.size() < 9) {
            if (tokens.size() == 1 && tokens[0] != "." && tokens[0] != "..") {
                 resultFiles.emplace_back(tokens[0], 0, std::chrono::system_clock::from_time_t(0), false);
                 qWarning() << "SftpTarget: listFiles - Parsed simple entry (1 token):" << QString::fromStdString(tokens[0]);
            } else qWarning() << "SftpTarget: listFiles - Skipping line due to insufficient tokens:" << QString::fromStdString(line);
            continue;
        }
        FileMetadata meta; char type = tokens[0][0]; meta.isDirectory = (type == 'd');
        std::string filename; for (size_t i = 8; i < tokens.size(); ++i) { if (i > 8) filename += " "; filename += tokens[i]; }
        if (filename == "." || filename == "..") continue;
        meta.name = filename;
        try { meta.size = std::stoull(tokens[4]); }
        catch (const std::exception& e) { qWarning() << "SftpTarget: listFiles - Failed to parse size for" << QString::fromStdString(meta.name) << ". Error:" << e.what(); meta.size = 0; }
        meta.modificationTime = parseSftpDate(QString::fromStdString(tokens[5]), QString::fromStdString(tokens[6]), QString::fromStdString(tokens[7]));
        resultFiles.push_back(meta);
    }
    return resultFiles;
}

bool SftpTarget::downloadFile(const std::string& remotePath, const std::string& localPath) {
    lastError_.clear();
    if (!m_curlHandle) {
        lastError_ = "SFTP downloadFile: Session not begun or curl handle not initialized.";
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return false;
    }

    std::string full_logical_path = joinPaths(m_remoteBasePath, remotePath);
    if (full_logical_path.empty() && remotePath != "/") {
        lastError_ = "SFTP downloadFile: Generated logical path is empty for remote item: " + remotePath;
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return false;
    }

    if (!m_curlHandle) {
        lastError_ = "SFTP downloadFile: curl handle not initialized before escaping path.";
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return false;
    }
    char* escaped_path_c = curl_easy_escape(m_curlHandle, full_logical_path.c_str(), 0);
    if (!escaped_path_c) {
        lastError_ = "SFTP downloadFile: curl_easy_escape failed for path: " + full_logical_path;
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return false;
    }
    std::string escaped_path(escaped_path_c);
    curl_free(escaped_path_c);

    std::string fileUrl = buildSftpBaseUrl(m_host, m_port) + escaped_path;
    std::cout << "SftpTarget: Downloading from URL: " << fileUrl << " to local path: " << localPath << std::endl;

    std::ofstream outFile(localPath, std::ios::binary);
    if (!outFile.is_open()) {
        lastError_ = "SFTP downloadFile: Failed to open local file for writing: " + localPath;
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return false;
    }

    CURLcode res_opt_url = curl_easy_setopt(m_curlHandle, CURLOPT_URL, fileUrl.c_str());
    if (res_opt_url != CURLE_OK) {
        lastError_ = "SFTP downloadFile: Failed to set URL '" + fileUrl + "': " + std::string(curl_easy_strerror(res_opt_url));
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        outFile.close();
        std::remove(localPath.c_str()); // Clean up empty/partially written local file
        return false;
    }

    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, fileWriteCallback);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, &outFile);
    curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 0L); // Ensure UPLOAD is off
    // For SFTP GET, CURLOPT_HTTPGET is not strictly necessary, but doesn't hurt.
    // It's more for HTTP. SFTP direction is inferred from UPLOAD flag.
    // curl_easy_setopt(m_curlHandle, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(m_curlHandle, CURLOPT_DIRLISTONLY, 0L); // Ensure not in dirlist mode
    curl_easy_setopt(m_curlHandle, CURLOPT_CUSTOMREQUEST, NULL); // Ensure no custom request

    CURLcode res_perform = curl_easy_perform(m_curlHandle);

    // Reset options that were specific to this request
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, NULL);

    outFile.close(); // Close file before checking result, to flush buffers.

    if (res_perform != CURLE_OK) {
        lastError_ = "SFTP downloadFile: curl_easy_perform() failed for URL " + fileUrl + ": " + curl_easy_strerror(res_perform);
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        std::remove(localPath.c_str()); // Clean up potentially incomplete local file
        return false;
    }

    // Additionally, check if fileWriteCallback encountered an error (e.g., disk full)
    // This is implicitly handled by outFile.good() in the callback, returning less than requested bytes,
    // which libcurl would typically interpret as an error (CURLE_WRITE_ERROR).
    // So, res_perform should reflect this. Explicit check here might be redundant if callback correctly signals error.

    std::cout << "SftpTarget: File downloaded successfully to " << localPath << std::endl;
    return true;
}

std::string SftpTarget::getLastError() const {
    return lastError_;
}
