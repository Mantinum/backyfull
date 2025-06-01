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
static std::string buildSftpUrl(const std::string& host, int port, const std::string& basePath, const std::string& relativePath);

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

static std::string buildSftpUrl(const std::string& host, int port, const std::string& basePath, const std::string& relativePath) {
    std::string path = joinPaths(basePath, relativePath);
    std::string url = "sftp://" + host + ":" + std::to_string(port);
    if (path == "/" && (relativePath.empty() || relativePath == "/")) {
        url += path; 
        if (url.back() != '/') url += '/';
    } else {
        url += path;
    }
    return url;
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
    if (!m_properlyConfigured) { std::cerr << "SftpTarget: Cannot begin session due to missing critical configuration." << std::endl; return false; }
    if (m_curlHandle) { curl_easy_cleanup(m_curlHandle); m_curlHandle = nullptr; }
    m_curlHandle = curl_easy_init();
    if (!m_curlHandle) { std::cerr << "SftpTarget: curl_easy_init() failed." << std::endl; return false; }
    CURLcode res;
    auto setopt_check = [&](CURLoption opt, auto val, const char* name) {
        res = curl_easy_setopt(m_curlHandle, opt, val);
        if (res != CURLE_OK) std::cerr << "SftpTarget: Failed to set " << name << ": " << curl_easy_strerror(res) << std::endl;
        return res == CURLE_OK;
    };
    if (!setopt_check(CURLOPT_USERNAME, m_username.c_str(), "CURLOPT_USERNAME")) goto error;
    if (!m_password.empty() && !setopt_check(CURLOPT_PASSWORD, m_password.c_str(), "CURLOPT_PASSWORD")) goto error;
    if (!setopt_check(CURLOPT_PORT, (long)m_port, "CURLOPT_PORT")) goto error;
    setopt_check(CURLOPT_VERBOSE, m_verboseLogging ? 1L : 0L, "CURLOPT_VERBOSE");
    if (m_sshAuthTypes != CURLSSH_AUTH_ANY && !setopt_check(CURLOPT_SSH_AUTH_TYPES, m_sshAuthTypes, "CURLOPT_SSH_AUTH_TYPES")) {}
    if (!m_sshPrivateKeyPath.empty() && !setopt_check(CURLOPT_SSH_PRIVATE_KEYFILE, m_sshPrivateKeyPath.c_str(), "CURLOPT_SSH_PRIVATE_KEYFILE")) {}
    if (!m_sshPublicKeyPath.empty() && !setopt_check(CURLOPT_SSH_PUBLIC_KEYFILE, m_sshPublicKeyPath.c_str(), "CURLOPT_SSH_PUBLIC_KEYFILE")) {}
    if (!m_sshKeyPassphrase.empty() && !m_sshPrivateKeyPath.empty() && !setopt_check(CURLOPT_KEYPASSWD, m_sshKeyPassphrase.c_str(), "CURLOPT_KEYPASSWD")) {}
    if (!m_sshSkipHostVerification && !m_sshKnownHostsPath.empty() && !setopt_check(CURLOPT_SSH_KNOWNHOSTS, m_sshKnownHostsPath.c_str(), "CURLOPT_SSH_KNOWNHOSTS")) {}
    else if (m_sshSkipHostVerification) {
         // For disabling host key check, libcurl uses these options with SSH backend:
         // This is insecure and should be used with caution.
        // A more common way if the above internal option isn't available/working is to use:
        // curl_easy_setopt(m_curlHandle, CURLOPT_SSL_VERIFYPEER, 0L); // Not for SSH host key
        // curl_easy_setopt(m_curlHandle, CURLOPT_SSL_VERIFYHOST, 0L); // Not for SSH host key
        // The correct way for SSH host keys if KNOWNHOSTS is not used and strict checking is off,
        // is often to rely on libcurl's default behavior when no known_hosts file is specified
        // or by setting specific options if the libcurl version supports them (like CURLOPT_SSH_VERIFYHOST).
        // Forcing via an "insecure" option if available:
        // curl_easy_setopt(m_curlHandle, CURLOPT_SSH_OPTIONS, CURLSSHOPT_SKIP_HOSTKEY_CHECK); // This is hypothetical
        // The most reliable way to skip host key check if truly needed and supported by underlying libssh2:
        // This often means NOT setting CURLOPT_SSH_KNOWNHOSTS and ensuring strict checking is off.
        // Libcurl's behavior can be complex here. The placeholder above is a common internal one.
        // If specific libcurl versions have a documented way, that should be used.
        // For now, the absence of CURLOPT_SSH_KNOWNHOSTS and strict checks might lead to prompt or failure.
        // The m_sshSkipHostVerification is a hint that we might want to bypass strict checks.
    }

    std::cout << "SftpTarget: Session begun successfully." << std::endl;
    return true;
error:
    if (m_curlHandle) { curl_easy_cleanup(m_curlHandle); m_curlHandle = nullptr; }
    return false;
}

bool SftpTarget::sendFile(const std::string& localPath, const FileMetadata& metadata) {
    std::cout << "SftpTarget: sendFile(" << localPath << ", remote_path: " << metadata.name << ") called." << std::endl;
    if (!m_curlHandle) { std::cerr << "SftpTarget: Session not begun or curl handle not initialized." << std::endl; return false; }
    std::ifstream sourceFile(localPath, std::ios::binary);
    if (!sourceFile.is_open()) { std::cerr << "SftpTarget: Failed to open source file: " << localPath << std::endl; return false; }
    sourceFile.seekg(0, std::ios::end);
    curl_off_t fileSize = sourceFile.tellg();
    sourceFile.seekg(0, std::ios::beg);
    if (fileSize < 0) { std::cerr << "SftpTarget: Failed to get size of source file: " << localPath << std::endl; sourceFile.close(); return false; }
    
    std::string remoteUrl = buildSftpUrl(m_host, m_port, m_remoteBasePath, metadata.name);
    std::cout << "SftpTarget: Uploading to URL: " << remoteUrl << std::endl;

    CURLcode res;
    curl_easy_setopt(m_curlHandle, CURLOPT_URL, remoteUrl.c_str());
    curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(m_curlHandle, CURLOPT_FTP_CREATE_MISSING_DIRS, (long)CURLFTP_CREATE_DIR_RETRY);
    curl_easy_setopt(m_curlHandle, CURLOPT_READFUNCTION, read_callback);
    curl_easy_setopt(m_curlHandle, CURLOPT_READDATA, &sourceFile);
    curl_easy_setopt(m_curlHandle, CURLOPT_INFILESIZE_LARGE, fileSize);
    res = curl_easy_perform(m_curlHandle);
    sourceFile.close();
    curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 0L);
    if (res != CURLE_OK) { std::cerr << "SftpTarget: curl_easy_perform() failed for sendFile: " << curl_easy_strerror(res) << std::endl; return false; }
    std::cout << "SftpTarget: File sent successfully." << std::endl;
    return true;
}

bool SftpTarget::deleteFile(const std::string& remotePath) {
    std::cout << "SftpTarget: deleteFile(" << remotePath << ") called." << std::endl;
    if (!m_curlHandle) { std::cerr << "SftpTarget: Session not begun or curl handle not initialized." << std::endl; return false; }
    std::string connectionUrl = buildSftpUrl(m_host, m_port, m_remoteBasePath, "");
    std::string absolutePathForRm = buildSftpAbsolutePath(m_remoteBasePath, remotePath);
    std::string rmCommand = "rm \"" + absolutePathForRm + "\"";
    struct curl_slist *headerlist = curl_slist_append(NULL, rmCommand.c_str());
    CURLcode res;
    curl_easy_setopt(m_curlHandle, CURLOPT_URL, connectionUrl.c_str());
    curl_easy_setopt(m_curlHandle, CURLOPT_QUOTE, headerlist);
    std::cout << "SftpTarget: Attempting to delete. URL: " << connectionUrl << ", Cmd: " << rmCommand << std::endl;
    res = curl_easy_perform(m_curlHandle);
    curl_slist_free_all(headerlist);
    curl_easy_setopt(m_curlHandle, CURLOPT_QUOTE, NULL);
    if (res != CURLE_OK) { std::cerr << "SftpTarget: curl_easy_perform() failed for deleteFile (" << remotePath << "). Error: " << curl_easy_strerror(res) << std::endl; return false; }
    std::cout << "SftpTarget: File deletion command successfully executed for " << remotePath << std::endl;
    return true;
}

bool SftpTarget::endSession() {
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
    std::vector<FileMetadata> resultFiles;
    if (!m_curlHandle) { std::cerr << "SftpTarget: listFiles - Session not begun or curl handle not initialized." << std::endl; return resultFiles; }
    std::string dirUrl = buildSftpUrl(m_host, m_port, m_remoteBasePath, remotePath);
    if (dirUrl.back() != '/') dirUrl += '/';
    // std::cout << "SftpTarget: Listing directory URL: " << dirUrl << std::endl; // Can be verbose
    curl_easy_setopt(m_curlHandle, CURLOPT_URL, dirUrl.c_str());
    std::string listingData;
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, sftpListWriteCallback);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, &listingData);
    curl_easy_setopt(m_curlHandle, CURLOPT_DIRLISTONLY, 0L);
    CURLcode res = curl_easy_perform(m_curlHandle);
    curl_easy_setopt(m_curlHandle, CURLOPT_CUSTOMREQUEST, NULL);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, NULL);
    if (res != CURLE_OK) { std::cerr << "SftpTarget: listFiles - curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl; return resultFiles; }
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
    if (!m_curlHandle) { std::cerr << "SftpTarget: downloadFile - Session not begun or curl handle not initialized." << std::endl; return false; }
    std::string fileUrl = buildSftpUrl(m_host, m_port, m_remoteBasePath, remotePath);
    std::cout << "SftpTarget: Downloading from URL: " << fileUrl << " to local path: " << localPath << std::endl;
    std::ofstream outFile(localPath, std::ios::binary);
    if (!outFile.is_open()) { std::cerr << "SftpTarget: Failed to open local file for writing: " << localPath << std::endl; return false; }
    CURLcode res;
    curl_easy_setopt(m_curlHandle, CURLOPT_URL, fileUrl.c_str());
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, fileWriteCallback);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, &outFile);
    curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 0L);
    curl_easy_setopt(m_curlHandle, CURLOPT_HTTPGET, 1L); // Should be default for sftp GET
    curl_easy_setopt(m_curlHandle, CURLOPT_DIRLISTONLY, 0L); // Ensure not in dirlist mode
    curl_easy_setopt(m_curlHandle, CURLOPT_CUSTOMREQUEST, NULL); // Ensure no custom request like LIST
    res = curl_easy_perform(m_curlHandle);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, NULL); // Reset
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, NULL);    // Reset
    outFile.close();
    if (res != CURLE_OK) { std::cerr << "SftpTarget: curl_easy_perform() failed for downloadFile: " << curl_easy_strerror(res) << std::endl; std::remove(localPath.c_str()); return false; }
    std::cout << "SftpTarget: File downloaded successfully to " << localPath << std::endl;
    return true;
}
