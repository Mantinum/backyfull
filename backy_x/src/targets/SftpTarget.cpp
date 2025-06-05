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
#include <QHostInfo>   // For host name resolution check
#include "util/CredentialManager.h" // For createPlatformCredentialManager and CredentialManager class

// For libcurl (SFTP operations)
#include <curl/curl.h>

// Static callback function for libcurl to read data for uploading
static size_t read_callback(char *ptr, size_t size, size_t nmemb, void *stream);
// Static callback function for libcurl to write SFTP listing data
static size_t sftpListWriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
// Generic file write callback for libcurl
static size_t fileWriteCallback(void*contents, size_t size, size_t nmemb, void*userp);
// Static no-op callback function for libcurl to safely discard write data
static size_t noop_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)ptr;      // Unused
    (void)userdata; // Unused
    // Report that all data was "handled" to prevent libcurl from erroring out
    return size * nmemb;
}
// Static no-op debug callback function for libcurl to safely discard verbose/debug information.
static int noop_debug_callback(CURL *handle,
                               curl_infotype type,
                               char *data,
                               size_t size,
                               void *userptr) {
    (void)handle;    // Unused
    (void)type;      // Unused
    (void)data;      // Unused
    (void)size;      // Unused
    (void)userptr;   // Unused
    return 0; // Return 0 as per libcurl documentation for this callback
}
// Helper function to parse date strings from SFTP listing
static std::chrono::system_clock::time_point parseSftpDate(const QString& monthToken, const QString& dayToken, const QString& timeOrYearToken);


// Helper functions for SFTP path and URL construction
static std::string buildSftpAbsolutePath(const std::string& basePath, const std::string& relativePath);
// static std::string buildSftpBaseUrl(const std::string& host, int port); // Definition to be removed

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

// Definition of buildSftpBaseUrl is removed.

// Implementation of the new helper function
bool SftpTarget::setupCurlHandleForOperation() {
    // If handle exists, clean it up first to ensure a fresh state
    if (m_curlHandle) {
        curl_easy_cleanup(m_curlHandle);
        m_curlHandle = nullptr;
    }

    m_curlHandle = curl_easy_init();

static FILE* s_curlNull = []{
    FILE* f = fopen("/dev/null", "w");
    return f ? f : stderr;           // repli de sécurité
}();
curl_easy_setopt(m_curlHandle, CURLOPT_STDERR, s_curlNull);
curl_easy_setopt(m_curlHandle, CURLOPT_DEBUGFUNCTION, nullptr);   // pas de callback custom
curl_easy_setopt(m_curlHandle, CURLOPT_VERBOSE,   0L);            // déjà fait

    if (!m_curlHandle) {
        lastError_ = "curl_easy_init() failed in setupCurlHandleForOperation.";
        std::cerr << "SftpTarget::setupCurlHandleForOperation: " << lastError_ << std::endl;
        return false;
    }

    CURLcode res_setopt; // Variable to store result of setopt calls

    // SSH host key verification logic (non-critical for setup function's success/failure)
    if (m_sshSkipHostVerification) {
        res_setopt = curl_easy_setopt(m_curlHandle, CURLOPT_SSH_KNOWNHOSTS, NULL);
        if (res_setopt != CURLE_OK) { // Try /dev/null as a fallback if NULL is not accepted
            res_setopt = curl_easy_setopt(m_curlHandle, CURLOPT_SSH_KNOWNHOSTS, "/dev/null");
        }
        if (res_setopt != CURLE_OK) {
            std::cerr << "SftpTarget::setupCurlHandleForOperation: Warning - Could not set CURLOPT_SSH_KNOWNHOSTS to NULL or /dev/null to skip host verification: " << curl_easy_strerror(res_setopt) << std::endl;
        }
    } else if (!m_sshKnownHostsPath.empty()) {
        res_setopt = curl_easy_setopt(m_curlHandle, CURLOPT_SSH_KNOWNHOSTS, m_sshKnownHostsPath.c_str());
        if (res_setopt != CURLE_OK) {
            std::cerr << "SftpTarget::setupCurlHandleForOperation: Warning - Failed to set custom CURLOPT_SSH_KNOWNHOSTS path '" << m_sshKnownHostsPath << "': " << curl_easy_strerror(res_setopt) << std::endl;
        }
    }
    // If neither skip nor path is set, libcurl uses its default behavior.

    // Helper lambda for setting critical options.
    // On failure, it sets lastError_, cleans up m_curlHandle, and returns false.
    auto setopt_critical = [&](CURLoption opt, auto val, const char* opt_name) -> bool {
        res_setopt = curl_easy_setopt(m_curlHandle, opt, val);
        if (res_setopt != CURLE_OK) {
            lastError_ = "Failed to set critical SFTP option " + std::string(opt_name) + ": " + curl_easy_strerror(res_setopt);
            std::cerr << "SftpTarget::setupCurlHandleForOperation: CRITICAL - " << lastError_ << std::endl;
            curl_easy_cleanup(m_curlHandle);
            m_curlHandle = nullptr;
            return false;
        }
        return true;
    };

    // Helper lambda for setting non-critical options.
    // On failure, it logs a warning. lastError_ is not set here to avoid overwriting a critical error.
    auto setopt_non_critical = [&](CURLoption opt, auto val, const char* opt_name) {
        res_setopt = curl_easy_setopt(m_curlHandle, opt, val);
        if (res_setopt != CURLE_OK) {
            std::cerr << "SftpTarget::setupCurlHandleForOperation: WARNING - Failed to set non-critical SFTP option " + std::string(opt_name) + ": " << curl_easy_strerror(res_setopt) << std::endl;
        }
    };

    // Set critical options
    if (!setopt_critical(CURLOPT_USERNAME, m_username.c_str(), "CURLOPT_USERNAME")) return false;
    if (!m_password.empty()) { // Password might not be used if public key auth is primary
        if (!setopt_critical(CURLOPT_PASSWORD, m_password.c_str(), "CURLOPT_PASSWORD")) return false;
    }
    if (!setopt_critical(CURLOPT_PORT, (long)m_port, "CURLOPT_PORT")) return false;

    // Set SSH auth type if specified (critical)
    if (m_sshAuthTypes != CURLSSH_AUTH_ANY) {
        if (!setopt_critical(CURLOPT_SSH_AUTH_TYPES, m_sshAuthTypes, "CURLOPT_SSH_AUTH_TYPES")) return false;
    }
    // Set SSH key paths if specified (critical)
    if (!m_sshPrivateKeyPath.empty()) {
        if (!setopt_critical(CURLOPT_SSH_PRIVATE_KEYFILE, m_sshPrivateKeyPath.c_str(), "CURLOPT_SSH_PRIVATE_KEYFILE")) return false;
    }
    if (!m_sshPublicKeyPath.empty()) {
        if (!setopt_critical(CURLOPT_SSH_PUBLIC_KEYFILE, m_sshPublicKeyPath.c_str(), "CURLOPT_SSH_PUBLIC_KEYFILE")) return false;
    }
    // Set SSH key passphrase if specified (critical, but only if private key is also set)
    if (!m_sshKeyPassphrase.empty() && !m_sshPrivateKeyPath.empty()) {
        if (!setopt_critical(CURLOPT_KEYPASSWD, m_sshKeyPassphrase.c_str(), "CURLOPT_KEYPASSWD")) return false;
    }

    // Set non-critical options
    // setopt_non_critical(CURLOPT_VERBOSE, m_verboseLogging ? 1L : 0L, "CURLOPT_VERBOSE"); // This line is intentionally removed/commented.

    // Enable CURLOPT_VERBOSE so that the CURLOPT_DEBUGFUNCTION is called.
    // The noop_debug_callback will then discard the actual verbose output.
    res_setopt = curl_easy_setopt(m_curlHandle, CURLOPT_VERBOSE, 1L);
    if (res_setopt != CURLE_OK) {
        lastError_ = "Failed to enable CURLOPT_VERBOSE (for debug callback): " + std::string(curl_easy_strerror(res_setopt));
        // Log as a warning, as other callbacks might still prevent crashes.
        std::cerr << "SftpTarget: Warning - Could not enable CURLOPT_VERBOSE: " << curl_easy_strerror(res_setopt) << std::endl;
        // Decide if this should be a fatal error. For now, let it continue.
    }

    // Set up a no-op debug callback to intercept and discard all verbose/debug information.
    // This is a comprehensive way to prevent libcurl from writing to potentially invalid streams.
    if (!setopt_critical(CURLOPT_DEBUGFUNCTION, noop_debug_callback, "CURLOPT_DEBUGFUNCTION_NOOP")) return false;
    if (!setopt_critical(CURLOPT_DEBUGDATA, nullptr, "CURLOPT_DEBUGDATA_NOOP")) return false;

    // Set a default no-op write callback to prevent libcurl from using internal fwrite
    // on potentially invalid streams if an operation unexpectedly produces data.
    // Operations that need to capture output (listFiles, downloadFile) will override this.
    if (!setopt_critical(CURLOPT_WRITEFUNCTION, noop_write_callback, "CURLOPT_WRITEFUNCTION_NOOP")) return false;
    if (!setopt_critical(CURLOPT_WRITEDATA, nullptr, "CURLOPT_WRITEDATA_NOOP")) return false;

    // Add Safe Callbacks for Headers
    if (!setopt_critical(CURLOPT_HEADERFUNCTION, noop_write_callback, "CURLOPT_HEADERFUNCTION_NOOP")) return false;
    if (!setopt_critical(CURLOPT_HEADERDATA, nullptr, "CURLOPT_HEADERDATA_NOOP")) return false;

    // If all critical options were set successfully, m_curlHandle is valid and configured.
    return true;
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
    if (itHost != config.end() && !itHost->second.empty()) {
        m_host = itHost->second;
        if (!m_host.empty()) { // Only clean if m_host is not empty after assignment
            // Remove newline characters
            m_host.erase(std::remove(m_host.begin(), m_host.end(), '\n'), m_host.end());
            // Remove carriage return characters
            m_host.erase(std::remove(m_host.begin(), m_host.end(), '\r'), m_host.end());
        }
    } else {
        std::cerr << "SftpTarget: Critical configuration 'host' is missing or empty." << std::endl;
        m_properlyConfigured = false;
    }
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
    // New concise log message regarding verbose logging behavior
    std::cout << "SftpTarget: Initialized. Note: libcurl's CURLOPT_VERBOSE is always enabled to activate a silencing debug callback; all verbose output is discarded by this callback for stability. The 'verbose_logging' configuration flag is parsed but currently does not alter this libcurl behavior." << std::endl;
}

SftpTarget::~SftpTarget() {
    if (m_curlHandle) { curl_easy_cleanup(m_curlHandle); m_curlHandle = nullptr; }
    std::cout << "SftpTarget destroyed." << std::endl;
}

bool SftpTarget::beginSession() {
    lastError_.clear(); // Clear any errors from previous operations/sessions.
    if (!m_properlyConfigured) {
        lastError_ = "SFTP Target not properly configured.";
        std::cerr << "SftpTarget::beginSession: " << lastError_ << std::endl;
        return false;
    }

    // Basic host resolution check so invalid hosts fail early
    QHostInfo info = QHostInfo::fromName(QString::fromStdString(m_host));
    if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
        lastError_ = "Failed to resolve host: " + m_host;
        std::cerr << "SftpTarget::beginSession: " << lastError_ << std::endl;
        return false;
    }

    // setupCurlHandleForOperation will clean up an existing handle if present.
    if (!this->setupCurlHandleForOperation()) {
        // lastError_ is set by setupCurlHandleForOperation if it fails critically.
        // m_curlHandle will be nullptr if setupCurlHandleForOperation failed.
        std::cerr << "SftpTarget::beginSession: Failed to setup CURL handle and options. Error: " << getLastError() << std::endl;
        return false;
    }

    std::cout << "SftpTarget::beginSession: Session options configured. Connection will be established on first operation." << std::endl;
    // If setupCurlHandleForOperation returned true, the handle is ready for use.
    // lastError_ might contain warnings from non-critical options in setup, but the session is considered ready.
    return true;
}

bool SftpTarget::sendFile(const std::string& localPath, const FileMetadata& metadata) {
    lastError_.clear(); // Clear previous errors for this new operation
    if (!this->setupCurlHandleForOperation()) {
        // lastError_ is set by setupCurlHandleForOperation
        std::cerr << "SftpTarget: sendFile setup failed: " << getLastError() << std::endl; // Or use qWarning
        // sourceFile is not open yet, so no need to close it here
        return false;
    }
    std::cout << "SftpTarget: sendFile(" << localPath << ", remote_path: " << metadata.name << ") called." << std::endl;
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
    // Note: empty check for full_logical_path is implicitly handled by buildSftpUrl,
    // as it defaults empty input to "/" or ensures a leading "/".
    // If buildSftpUrl returns empty, it means an error occurred there (e.g. m_curlHandle null or escape failed)

    std::string remoteUrl = this->buildSftpUrl(full_logical_path);
    if (remoteUrl.empty()) {
        // lastError_ might be set by buildSftpUrl if m_curlHandle was null or curl_easy_escape failed.
        // If it's not set, provide a generic error.
        if (lastError_.empty()) {
            lastError_ = "SFTP sendFile: Failed to construct remote URL for path: " + full_logical_path;
        }
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        sourceFile.close();
        return false;
    }
    std::cout << "SftpTarget: Uploading to URL: " << remoteUrl << std::endl;

    CURLcode res_setopt_url = curl_easy_setopt(m_curlHandle, CURLOPT_URL, remoteUrl.c_str());
    if (res_setopt_url != CURLE_OK) {
        // Note: buildSftpUrl might have already set lastError_ if it failed.
        // Overwrite if this specific error is more relevant.
        lastError_ = "SFTP sendFile: Failed to set CURLOPT_URL for '" + remoteUrl + "': " + std::string(curl_easy_strerror(res_setopt_url));
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
    lastError_.clear(); // Clear previous operational errors.
    qDebug() << "SftpTarget: deleteFile(" << QString::fromStdString(remotePath) << ") called.";

    // Ensure m_curlHandle is (re)initialized with all necessary common options.
    if (!this->setupCurlHandleForOperation()) {
        // lastError_ is set by setupCurlHandleForOperation if it fails critically.
        // m_curlHandle will be nullptr if setupCurlHandleForOperation failed.
        qWarning() << "SftpTarget: deleteFile call aborted due to setupCurlHandleForOperation failure: " << QString::fromStdString(getLastError());
        return false;
    }

    // The URL for QUOTE commands is typically the base SFTP URL sftp://host:port/
    // The path for 'rm' is specified in the command itself and should be absolute on the server.
    std::string contextUrl = this->buildSftpUrl("/"); // Generate sftp://host:port/
    if (contextUrl.empty()) {
        if (lastError_.empty()) { // Check if buildSftpUrl set it
             lastError_ = "SFTP deleteFile: Failed to construct context URL.";
        }
        qWarning() << "SftpTarget:" << QString::fromStdString(lastError_);
        return false;
    }

    std::string absolutePathForRm = buildSftpAbsolutePath(m_remoteBasePath, remotePath);
    // Escape quotes within the absolutePathForRm if any, though filenames typically don't have them.
    // For simplicity, assuming paths don't contain double quotes that would break the command string.
    // A more robust solution would escape characters in absolutePathForRm if needed.
    std::string rmCommand = "rm \"" + absolutePathForRm + "\""; // Quoting handles spaces

    struct curl_slist *customCommands = NULL;
    customCommands = curl_slist_append(customCommands, rmCommand.c_str());
    if (!customCommands) {
        lastError_ = "SFTP deleteFile: Failed to create slist for QUOTE command.";
        qWarning() << "SftpTarget:" << QString::fromStdString(lastError_);
        return false;
    }

    CURLcode res_setopt_url = curl_easy_setopt(m_curlHandle, CURLOPT_URL, contextUrl.c_str());
    if (res_setopt_url != CURLE_OK) {
        lastError_ = "SFTP deleteFile: Failed to set context URL: " + std::string(curl_easy_strerror(res_setopt_url));
        qWarning() << "SftpTarget:" << QString::fromStdString(lastError_);
        curl_slist_free_all(customCommands);
        return false;
    }

    curl_easy_setopt(m_curlHandle, CURLOPT_QUOTE, customCommands);
    // Ensure other modes that might conflict with QUOTE are off (e.g. UPLOAD)
    curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 0L);
    curl_easy_setopt(m_curlHandle, CURLOPT_DIRLISTONLY, 0L);

    // Ensure options that expect a data response body are reset or off,
    // as QUOTE commands typically don't return one.
    curl_easy_setopt(m_curlHandle, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(m_curlHandle, CURLOPT_HTTPGET, 0L);
    curl_easy_setopt(m_curlHandle, CURLOPT_POST, 0L);

    // Reset write/read functions and data. We keep our no-op callbacks instead of
    // using libcurl's defaults to avoid writing to stdout/stderr, which may be
    // invalid in the GUI environment.
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, noop_write_callback);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, nullptr);
    curl_easy_setopt(m_curlHandle, CURLOPT_READFUNCTION, NULL);
    curl_easy_setopt(m_curlHandle, CURLOPT_READDATA, NULL);

    qDebug() << "SftpTarget: Attempting to delete. Context URL:" << QString::fromStdString(contextUrl) << ", Command:" << QString::fromStdString(rmCommand);
    CURLcode res_perform = curl_easy_perform(m_curlHandle);

    curl_slist_free_all(customCommands);
    curl_easy_setopt(m_curlHandle, CURLOPT_QUOTE, NULL); // Reset QUOTE

    if (res_perform != CURLE_OK) {
        // This error might indicate issues like "file not found", "permission denied", or "is a directory".
        lastError_ = "SFTP deleteFile command failed for path '" + remotePath + "' (absolute: '" + absolutePathForRm + "'): " + curl_easy_strerror(res_perform);
        qWarning() << "SftpTarget:" << QString::fromStdString(lastError_);
        return false;
    }

    // Note: For SFTP, CURLE_OK for a QUOTE 'rm' command doesn't always guarantee the file was actually deleted
    // or that it existed. Some servers might not return an error code via libcurl for "file not found" on 'rm'.
    // True verification would involve trying to list/stat the file afterwards.
    // For this implementation, CURLE_OK is treated as success.
    qDebug() << "SftpTarget: File deletion command successfully executed for" << QString::fromStdString(remotePath);
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
    bool parsingErrorOccurred = false;
    std::vector<FileMetadata> resultFiles;
    // Clear previous errors for this new operation (already done by setupCurlHandleForOperation if it was called, but good practice)
    // lastError_.clear();
    // No, setupCurlHandleForOperation does not clear lastError_ at its start.
    // It only sets it on failure. The caller should clear it.
    lastError_.clear();
    if (!this->setupCurlHandleForOperation()) {
        // lastError_ is set by setupCurlHandleForOperation
        std::cerr << "SftpTarget: listFiles setup failed: " << getLastError() << std::endl; // Or use qWarning
        return resultFiles; // resultFiles is empty at this point
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

    // The m_curlHandle check for buildSftpUrl itself is inside buildSftpUrl.
    std::string dirUrl = this->buildSftpUrl(logical_path_for_listing);
    if (dirUrl.empty()) {
        if (lastError_.empty()) { // Check if buildSftpUrl set it
            lastError_ = "SFTP listFiles: Failed to construct remote URL for path: " + logical_path_for_listing;
        }
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return resultFiles;
    }
    // std::cout << "SftpTarget: Listing directory URL: " << dirUrl << std::endl; // Can be verbose

    CURLcode res_opt = curl_easy_setopt(m_curlHandle, CURLOPT_URL, dirUrl.c_str());
    if (res_opt != CURLE_OK) {
        lastError_ = "SFTP listFiles: Failed to set URL '" + dirUrl + "': " + std::string(curl_easy_strerror(res_opt));
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        return resultFiles;
    }

    std::string listingData;
    // Specific callbacks for listFiles, overriding the noop_write_callback set in setupCurlHandleForOperation
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
    // Reset write function to default (noop) after use, good practice if handle is reused by other logic not expecting this specific callback.
    // However, with setupCurlHandleForOperation called each time, this might be redundant.
    // For now, let's keep it to ensure no leftover state if setupCurlHandleForOperation logic changes.
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, noop_write_callback); // Reset to default
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, nullptr);     // Reset data pointer for default
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
                 // This case is a simple parse, might not be an "error" unless it's unexpected.
                 // Not setting parsingErrorOccurred = true; for this specific simple parse.
            } else {
                qWarning() << "SftpTarget: listFiles - Skipping line due to insufficient tokens:" << QString::fromStdString(line);
                if (!line.empty() && line.find_first_not_of(" \t\r\n") != std::string::npos) { // Check if line wasn't just whitespace
                    parsingErrorOccurred = true;
                }
            }
            continue;
        }
        FileMetadata meta; char type = tokens[0][0]; meta.isDirectory = (type == 'd');
        std::string filename; for (size_t i = 8; i < tokens.size(); ++i) { if (i > 8) filename += " "; filename += tokens[i]; }
        if (filename == "." || filename == "..") continue;
        meta.name = filename;
        try { meta.size = std::stoull(tokens[4]); }
        catch (const std::exception& e) {
            qWarning() << "SftpTarget: listFiles - Failed to parse size for" << QString::fromStdString(meta.name) << ". Error:" << e.what();
            meta.size = 0;
            parsingErrorOccurred = true;
        }
        // Assuming parseSftpDate also uses qWarning internally on failure and returns time_t(0)
        meta.modificationTime = parseSftpDate(QString::fromStdString(tokens[5]), QString::fromStdString(tokens[6]), QString::fromStdString(tokens[7]));
        if (meta.modificationTime == std::chrono::system_clock::from_time_t(0) && tokens[0] != "total") { // if parseSftpDate failed (returned epoch 0)
             // and it's not a "total" line which might not have a date
            parsingErrorOccurred = true; // Count date parsing failure as a parsing error
        }
        resultFiles.push_back(meta);
    }

    if (resultFiles.empty() && !listingData.empty() && parsingErrorOccurred && lastError_.empty()) {
        lastError_ = "Failed to parse any entries from the directory listing. The server's `ls` format might be incompatible or the listing was unusual.";
    }
    return resultFiles;
}

bool SftpTarget::downloadFile(const std::string& remotePath, const std::string& localPath) {
    lastError_.clear(); // Clear previous errors for this new operation
    if (!this->setupCurlHandleForOperation()) {
        // lastError_ is set by setupCurlHandleForOperation
        std::cerr << "SftpTarget: downloadFile setup failed: " << getLastError() << std::endl; // Or use qWarning
        // outFile is not open yet, so no need to close/remove
        return false;
    }

    std::string full_logical_path = joinPaths(m_remoteBasePath, remotePath);
    // Note: empty check for full_logical_path is implicitly handled by buildSftpUrl

    std::string fileUrl = this->buildSftpUrl(full_logical_path);
    if (fileUrl.empty()) {
        if (lastError_.empty()) { // Check if buildSftpUrl set it
            lastError_ = "SFTP downloadFile: Failed to construct remote URL for path: " + full_logical_path;
        }
        std::cerr << "SftpTarget: " << lastError_ << std::endl;
        // No local file opened yet, so no need to close/remove
        return false;
    }
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

    // Specific callbacks for downloadFile, overriding noop_write_callback
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
    // Reset write function to default (noop) after use.
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, noop_write_callback); // Reset to default
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, nullptr);    // Reset data pointer for default

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

bool SftpTarget::isSessionOpen() const {
    // A session is considered "open" or "active" if the CURL handle 
    // has been initialized (by beginSession()) and not yet cleaned up (by endSession() or destructor).
    return m_curlHandle != nullptr;
}

// New method for segment-wise path escaping
std::string SftpTarget::buildSftpUrl(const std::string& relativePath) const {
    if (!m_curlHandle) {
        // lastError_ cannot be set directly in a const method unless mutable.
        // Logging to cerr and returning empty string is an alternative.
        std::cerr << "SftpTarget::buildSftpUrl: m_curlHandle is null. Cannot escape path." << std::endl;
        // Consider setting a global error or having a non-const version if lastError_ must be set.
        return ""; // Indicates error
    }

    std::stringstream url_ss;
    url_ss << "sftp://" << m_host << ":" << m_port;

    std::string path_to_process = relativePath;
    if (path_to_process.empty()) {
        path_to_process = "/"; // Default to root if empty
    }
    // Ensure path starts with a slash if it's not already (unless it's empty, handled above)
    if (path_to_process.front() != '/') {
        path_to_process = "/" + path_to_process;
    }

    if (path_to_process == "/") {
        url_ss << "/";
    } else {
        // Path is like "/foo/bar" or "/foo/bar/"
        // We process segments between slashes.
        // The first character is '/', so we start processing from the char after it.
        std::string content_to_segment = path_to_process.substr(1);
        std::stringstream segment_stream(content_to_segment);
        std::string segment;

        // Keep track if the original path (after initial normalization) had a trailing slash
        bool has_trailing_slash = (content_to_segment.length() > 0 && content_to_segment.back() == '/');

        while(std::getline(segment_stream, segment, '/')) {
            url_ss << '/'; // Append segment separator
            if (!segment.empty()) {
                char *escaped_segment = curl_easy_escape(m_curlHandle, segment.c_str(), 0);
                if (escaped_segment) {
                    url_ss << escaped_segment;
                    curl_free(escaped_segment);
                } else {
                    std::cerr << "SftpTarget::buildSftpUrl: curl_easy_escape failed for segment: " << segment << std::endl;
                    return ""; // Error case
                }
            }
            // If segment is empty (e.g. from path "//" or "/path//seg/"), it means consecutive slashes.
            // An empty segment is appended after the '/', effectively preserving multiple slashes if needed (e.g. /foo//bar -> /foo//bar).
            // curl_easy_escape("") results in "", so `url_ss << ""` is a no-op, which is fine for empty segments.
        }

        // If the original path (content_to_segment) had a trailing slash,
        // and std::getline consumed the last segment *before* that slash,
        // the loop finishes. We need to add that trailing slash back.
        // This happens if the path was like "segment/" -> getline gives "segment", then loop ends.
        // If path was "segment//", getline gives "segment", then "", then loop ends.
        if (has_trailing_slash && url_ss.str().back() != '/') {
            url_ss << '/';
        }
    }
    return url_ss.str();
}
