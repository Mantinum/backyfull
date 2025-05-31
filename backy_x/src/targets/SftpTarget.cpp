#include "targets/SftpTarget.h"
#include <iostream> // For std::cerr/std::cout
#include <fstream>  // For std::ifstream
#include <filesystem> // For std::filesystem::path (C++17)
#include <vector>   // For directory creation components
#include <sstream>  // For std::istringstream (used in parseSshAuthTypes)

#include <QString> // For service name and password with CredentialManager
#include <QDebug> // For qDebug, qWarning
#include <QByteArray> // For QByteArray and toUtf8()
#include <QUrl> // For QUrl and QUrl::toPercentEncoding
#include "util/CredentialManager.h" // For createPlatformCredentialManager and CredentialManager class

// For libcurl (SFTP operations)
// Ensure libcurl is installed (e.g., sudo apt-get install libcurl4-openssl-dev)
#include <curl/curl.h>

// Note: libssh2 is often a dependency of libcurl when built with SFTP support.
// Direct usage of libssh2 might not be needed if libcurl handles SFTP adequately.
// If direct libssh2 usage is intended:
// #include <libssh2.h>
// #include <libssh2_sftp.h>
// Ensure libssh2 is installed (e.g., sudo apt-get install libssh2-1-dev)

// Static callback function for libcurl to read data for uploading
static size_t read_callback(char *ptr, size_t size, size_t nmemb, void *stream);

// Helper functions for SFTP path and URL construction
static std::string buildSftpAbsolutePath(const std::string& basePath, const std::string& relativePath);
static std::string buildSftpUrl(const std::string& host, int port, const std::string& basePath, const std::string& relativePath);


// Implementation of read_callback
static size_t read_callback(char *ptr, size_t size, size_t nmemb, void *stream); // Forward declaration

// Helper function to parse SSH authentication types string
static long parseSshAuthTypes(const std::string& authTypesStr) {
    if (authTypesStr.empty() || authTypesStr == "any") {
        return CURLSSH_AUTH_ANY;
    }
    long types = 0;
    std::string token;
    std::istringstream tokenStream(authTypesStr);
    while (std::getline(tokenStream, token, ',')) {
        // Basic trim for leading/trailing whitespace
        token.erase(0, token.find_first_not_of(" \t\n\r\f\v"));
        token.erase(token.find_last_not_of(" \t\n\r\f\v") + 1);

        if (token == "password") types |= CURLSSH_AUTH_PASSWORD;
        else if (token == "publickey") types |= CURLSSH_AUTH_PUBLICKEY;
        else if (token == "agent") types |= CURLSSH_AUTH_AGENT;
        else if (token == "keyboard-interactive") types |= CURLSSH_AUTH_KEYBOARD;
        else if (token == "any") {
            types |= CURLSSH_AUTH_ANY; 
            // If "any" is specified, it often implies all others, but libcurl's bitmask suggests combining is fine.
            // If 'any' alone is desired, the input string should just be 'any'.
        } else if (!token.empty()) { // Avoid warning for empty tokens if input is e.g. "publickey,"
            std::cerr << "SftpTarget: Unknown or empty SSH auth type specified: '" << token << "'" << std::endl;
        }
    }
    // If types is 0 after parsing (e.g., all unknown types or empty string after trimming), default to ANY.
    return types == 0 ? CURLSSH_AUTH_ANY : types;
}


static size_t read_callback(char *ptr, size_t size, size_t nmemb, void *stream) {
    std::ifstream *fileStream = static_cast<std::ifstream*>(stream);
    fileStream->read(ptr, size * nmemb);
    size_t bytesRead = fileStream->gcount();
    if (bytesRead == 0 && fileStream->eof()) {
        // No more data to read
    } else if (bytesRead < size * nmemb && fileStream->fail() && !fileStream->eof()) {
        // Handle read error if necessary
        std::cerr << "SftpTarget::read_callback: Read error from input file." << std::endl;
        return CURL_READFUNC_ABORT; // Abort transfer on read error
    }
    return bytesRead;
}

// Helper function to join base and relative paths carefully
static std::string joinPaths(std::string basePath, std::string relativePath) {
    // Normalize basePath: remove trailing slashes if not root
    while (basePath.length() > 1 && basePath.back() == '/') {
        basePath.pop_back();
    }
    // If basePath is empty or "/", and relativePath starts with "/", remove one slash
    if ((basePath.empty() || basePath == "/") && !relativePath.empty() && relativePath.front() == '/') {
        relativePath = relativePath.substr(1);
    } else if (!basePath.empty() && basePath != "/" && !relativePath.empty() && relativePath.front() != '/') {
        // Add a slash if basePath is not root and relativePath doesn't start with one
        basePath += "/";
    } else if (!basePath.empty() && basePath != "/" && !relativePath.empty() && relativePath.front() == '/'){
        // basePath is like /foo, relativePath is /bar.txt -> /foo/bar.txt
        // This case is tricky. If basePath is /foo, and rel is /bar, we want /foo/bar, not /foo//bar
        // However, if basepath is / and rel is /bar, we want /bar.
        // The above conditions should mostly handle it. Let's ensure no double slashes from this join.
        // The primary rule: if relPath starts with '/', it's absolute *from the perspective of basePath's components*
        // if basePath is /a/b and relPath is /c/d, result is /a/b/c/d (after stripping relPath's leading /)
        if (relativePath.front() == '/') {
            relativePath = relativePath.substr(1);
        }
         basePath += "/";

    } else if (basePath == "/" && relativePath.empty()){
        // Special case: basePath is "/" and relativePath is empty, result should be "/"
        return "/";
    }


    std::string fullPath = basePath + relativePath;

    // Final check for "//" not at the start (e.g. sftp://)
    size_t doubleSlashPos = fullPath.find("//");
    if (fullPath.length() > 0 && fullPath.front() == '/' && doubleSlashPos == 0) { // like //foo
        fullPath = fullPath.substr(1); // -> /foo
    }
    
    // Ensure it starts with a slash if it's not empty
    if (!fullPath.empty() && fullPath.front() != '/') {
        fullPath = "/" + fullPath;
    }
    if (fullPath.empty()){ // if both base and relative were empty or just "/" and ""
        return "/";
    }

    // Consolidate multiple slashes into one, except for protocol part (e.g. sftp://)
    std::string result_path;
    bool last_was_slash = false;
    for (char ch : fullPath) {
        if (ch == '/') {
            if (!last_was_slash) {
                result_path += ch;
            }
            last_was_slash = true;
        } else {
            result_path += ch;
            last_was_slash = false;
        }
    }
    // If it became empty and original fullPath was like "/", restore it
    if (result_path.empty() && !fullPath.empty()) return "/";
    // If it's longer than "/" and ends with a slash, remove it (unless it's just "/")
    if (result_path.length() > 1 && result_path.back() == '/') {
        result_path.pop_back();
    }


    return result_path;
}


static std::string buildSftpAbsolutePath(const std::string& basePath, const std::string& relativePath) {
    return joinPaths(basePath, relativePath);
}

static std::string buildSftpUrl(const std::string& host, int port, const std::string& basePath, const std::string& relativePath) {
    std::string path = joinPaths(basePath, relativePath);
    // Ensure path does not start with / if it's empty or just / for root, to avoid sftp://host:port//file
    // The joinPaths should return "/" for root, or "/file" for file at root.
    // So if path is "/", it means root of sftp server.
    // If path is "/file.txt", it means sftp://host:port/file.txt
    // If path is "/dir/file.txt", it means sftp://host:port/dir/file.txt
    // The joinPaths function is designed to return a path that starts with a single slash if it's not empty.
    // So, we just prepend sftp://host:port
    
    // Special case: if path from joinPaths is "/" (root), we might want sftp://host:port/
    // or if it's for a file like "/file.txt", then sftp://host:port/file.txt
    // The joinPaths is expected to return "/something" or just "/"
    // We do not want sftp://host:port// (double slash after port) if path is "/"
    // So, if path is indeed just "/", we might append nothing more or ensure it's handled.
    // Let's assume joinPaths gives a path like "/foo/bar" or "/" for root.
    // The string concatenation `std::string url = "sftp://" + host + ":" + std::to_string(port) + path;` should be fine.
    // as path will be "/p" or "/"
    
    std::string url = "sftp://" + host + ":" + std::to_string(port);
    if (path == "/" && relativePath.empty()) { // If asking for base path URL explicitly
        url += path; 
        if (url.back() != '/') url += '/'; // Ensure sftp://host:port/
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
      m_properlyConfigured(true) { // Initialize m_properlyConfigured to true
    // Initialize m_curlHandle to nullptr (already done by initializer list but good for clarity)
    m_curlHandle = nullptr; 
    m_credentialManager = std::unique_ptr<CredentialManager>(createPlatformCredentialManager());

    auto itHost = config.find("host");
    if (itHost != config.end() && !itHost->second.empty()) {
        m_host = itHost->second;
    } else {
        std::cerr << "SftpTarget: Critical configuration 'host' is missing or empty." << std::endl;
        m_properlyConfigured = false;
        // No return here, let constructor finish, beginSession will guard
    }

    auto itUser = config.find("username");
    if (itUser != config.end() && !itUser->second.empty()) {
        m_username = itUser->second;
    } else {
        std::cerr << "SftpTarget: Critical configuration 'username' is missing or empty." << std::endl;
        m_properlyConfigured = false;
        // No return here
    }
    
    // If not properly configured at this point, we can skip further non-critical parsing or decisions
    // but for now, let it proceed as host/user are the primary critical ones for this change.

    auto itPort = config.find("port");
    if (itPort != config.end()) {
        try {
            m_port = std::stoi(itPort->second);
        } catch (const std::exception& e) {
            std::cerr << "SftpTarget: Invalid port number '" << itPort->second << "'. Using default 22. Error: " << e.what() << std::endl;
            m_port = 22;
        }
    } else {
        // Default port is 22, already set in member initializer list
         std::cout << "SftpTarget: 'port' not found in configuration, defaulting to 22." << std::endl;
    }

    QString serviceName = QString("sftp_%1_%2").arg(QString::fromStdString(m_host)).arg(m_port);
    QString qUsername = QString::fromStdString(m_username);

    auto itPass = config.find("password");
    if (itPass != config.end() && !itPass->second.empty()) {
        m_password = itPass->second; // Store for current session
        QString qPassword = QString::fromStdString(m_password);
        if (m_credentialManager) {
            std::cout << "SftpTarget: Password provided in config. Storing with CredentialManager for service: " << serviceName.toStdString() << " user: " << qUsername.toStdString() << std::endl;
            if (m_credentialManager->storeSecret(serviceName, qUsername, qPassword)) {
                std::cout << "SftpTarget: Password stored successfully via CredentialManager." << std::endl;
            } else {
                std::cerr << "SftpTarget: Failed to store password via CredentialManager." << std::endl;
            }
        }
    } else {
        if (m_credentialManager) {
            std::cout << "SftpTarget: No password in config. Attempting to retrieve from CredentialManager for service: " << serviceName.toStdString() << " user: " << qUsername.toStdString() << std::endl;
            std::optional<QString> retrievedPassword = m_credentialManager->retrieveSecret(serviceName, qUsername);
            if (retrievedPassword) {
                m_password = retrievedPassword->toStdString();
                std::cout << "SftpTarget: Password retrieved successfully from CredentialManager." << std::endl;
            } else {
                std::cout << "SftpTarget: Failed to retrieve password from CredentialManager or no password stored." << std::endl;
                // m_password remains empty, key-based auth or password-less auth might be attempted
            }
        } else {
             std::cerr << "SftpTarget: CredentialManager not available. Cannot store or retrieve password." << std::endl;
        }
    }

    auto itPath = config.find("remoteBasePath");
    if (itPath != config.end()) {
        m_remoteBasePath = itPath->second;
    } else {
        m_remoteBasePath = "/"; // Default to root if not specified
        std::cout << "SftpTarget: 'remoteBasePath' not found, defaulting to '/'." << std::endl;
    }
    
    std::cout << "SftpTarget configured for host: " << m_host << ", user: " << m_username << ", path: " << m_remoteBasePath << ", port: " << m_port << std::endl;

    // Read SSH specific configurations
    auto itSshPubKey = config.find("ssh_public_key_path");
    if (itSshPubKey != config.end()) m_sshPublicKeyPath = itSshPubKey->second;

    auto itSshPrivKey = config.find("ssh_private_key_path");
    if (itSshPrivKey != config.end()) m_sshPrivateKeyPath = itSshPrivKey->second;
    
    auto itSshKeyPass = config.find("ssh_key_passphrase");
    if (itSshKeyPass != config.end()) m_sshKeyPassphrase = itSshKeyPass->second;

    auto itSshKnownHosts = config.find("ssh_known_hosts_path");
    if (itSshKnownHosts != config.end()) m_sshKnownHostsPath = itSshKnownHosts->second;

    auto itSshSkipVerify = config.find("ssh_skip_host_verification");
    if (itSshSkipVerify != config.end()) {
        std::string val = itSshSkipVerify->second;
        std::transform(val.begin(), val.end(), val.begin(), ::tolower);
        if (val == "true" || val == "1") {
            m_sshSkipHostVerification = true;
        }
    }

    auto itSshAuthTypes = config.find("ssh_auth_types");
    if (itSshAuthTypes != config.end() && !itSshAuthTypes->second.empty()) {
        m_sshAuthTypes = parseSshAuthTypes(itSshAuthTypes->second);
    } else {
        m_sshAuthTypes = CURLSSH_AUTH_ANY; // Default if not specified or empty
    }
    
    auto itVerboseLogging = config.find("verbose_logging");
    if (itVerboseLogging != config.end()) {
        std::string val = itVerboseLogging->second;
        std::transform(val.begin(), val.end(), val.begin(), ::tolower);
        if (val == "true" || val == "1") {
            m_verboseLogging = true;
        }
    }
    if (m_verboseLogging) {
        std::cout << "SftpTarget: Verbose logging enabled." << std::endl;
    }
    if (m_sshSkipHostVerification) {
        std::cout << "SftpTarget: SSH skip host verification enabled." << std::endl;
    }
     // Log other SSH settings if needed for debugging
    if (!m_sshPrivateKeyPath.empty()) {
        std::cout << "SftpTarget: SSH Private Key Path: " << m_sshPrivateKeyPath << std::endl;
    }
     if (m_sshAuthTypes != CURLSSH_AUTH_ANY) {
        std::cout << "SftpTarget: Custom SSH Auth Types configured: " << m_sshAuthTypes << std::endl;
    }
}

SftpTarget::~SftpTarget() {
    if (m_curlHandle) {
        curl_easy_cleanup(m_curlHandle);
        m_curlHandle = nullptr;
    }
    std::cout << "SftpTarget destroyed." << std::endl;
}

bool SftpTarget::beginSession() {
    if (!m_properlyConfigured) {
        std::cerr << "SftpTarget: Cannot begin session due to missing critical configuration (host or username)." << std::endl;
        return false;
    }

    std::cout << "SftpTarget: beginSession() called." << std::endl;
    // The explicit m_host.empty() || m_username.empty() check might seem redundant now
    // if m_properlyConfigured implies they are not empty. However, it's a good safeguard
    // if m_properlyConfigured logic were to change or if those members could be emptied by other means.
    // For now, let's keep it as m_properlyConfigured should cover this.
    // if (m_host.empty() || m_username.empty()) {
    //     std::cerr << "SftpTarget: Host or username is empty. Cannot begin session." << std::endl;
    //     return false; // Should be caught by m_properlyConfigured
    // }


    if (m_curlHandle) { // Clean up existing handle if any (e.g. from previous failed session)
        curl_easy_cleanup(m_curlHandle);
        m_curlHandle = nullptr;
    }

    m_curlHandle = curl_easy_init();
    if (!m_curlHandle) {
        std::cerr << "SftpTarget: curl_easy_init() failed." << std::endl;
        return false;
    }

    CURLcode res;

    // Set username
    res = curl_easy_setopt(m_curlHandle, CURLOPT_USERNAME, m_username.c_str());
    if (res != CURLE_OK) {
        std::cerr << "SftpTarget: Failed to set CURLOPT_USERNAME: " << curl_easy_strerror(res) << std::endl;
        goto error;
    }

    // Set password (consider using CURLOPT_SSH_KEYFUNCTION for key-based auth in future)
    if (!m_password.empty()) {
        res = curl_easy_setopt(m_curlHandle, CURLOPT_PASSWORD, m_password.c_str());
        if (res != CURLE_OK) {
            std::cerr << "SftpTarget: Failed to set CURLOPT_PASSWORD: " << curl_easy_strerror(res) << std::endl;
            goto error;
        }
    } else {
        // Allow password-less auth (e.g. public key via ssh-agent or known_hosts)
        // Or set CURLOPT_SSH_AUTH_TYPES to CURLSSH_AUTH_PUBLICKEY, CURLSSH_AUTH_AGENT etc.
        // For now, rely on libcurl's default behavior if password is not provided.
        std::cout << "SftpTarget: No password provided. Attempting key-based or agent-based auth." << std::endl;
    }
    
    // Set port
    res = curl_easy_setopt(m_curlHandle, CURLOPT_PORT, (long)m_port);
    if (res != CURLE_OK) {
        std::cerr << "SftpTarget: Failed to set CURLOPT_PORT: " << curl_easy_strerror(res) << std::endl;
        goto error;
    }

    // Enable verbose mode for debugging based on member variable
    res = curl_easy_setopt(m_curlHandle, CURLOPT_VERBOSE, m_verboseLogging ? 1L : 0L);
    if (res != CURLE_OK) {
        std::cerr << "SftpTarget: Failed to set CURLOPT_VERBOSE: " << curl_easy_strerror(res) << std::endl;
        // Not critical, continue
    }

    // Set SSH Authentication Types
    if (m_sshAuthTypes != CURLSSH_AUTH_ANY) { // Only set if not default or explicitly configured
        res = curl_easy_setopt(m_curlHandle, CURLOPT_SSH_AUTH_TYPES, m_sshAuthTypes);
        if (res != CURLE_OK) {
            std::cerr << "SftpTarget: Failed to set CURLOPT_SSH_AUTH_TYPES to " << m_sshAuthTypes << ": " << curl_easy_strerror(res) << std::endl;
            // Depending on how critical this is, you might goto error.
            // If AUTH_ANY is acceptable fallback, maybe not. For now, log and continue.
        } else {
             std::cout << "SftpTarget: Successfully set CURLOPT_SSH_AUTH_TYPES to " << m_sshAuthTypes << std::endl;
        }
    }
    
    // Public/Private Key Authentication
    if (!m_sshPrivateKeyPath.empty()) {
        res = curl_easy_setopt(m_curlHandle, CURLOPT_SSH_PRIVATE_KEYFILE, m_sshPrivateKeyPath.c_str());
        if (res != CURLE_OK) {
            std::cerr << "SftpTarget: Failed to set CURLOPT_SSH_PRIVATE_KEYFILE '" << m_sshPrivateKeyPath << "': " << curl_easy_strerror(res) << std::endl;
            // This could be a critical error if publickey auth is the only method intended.
            // goto error; // Consider this if key auth is essential and fails
        } else {
            std::cout << "SftpTarget: Set CURLOPT_SSH_PRIVATE_KEYFILE to " << m_sshPrivateKeyPath << std::endl;
        }
    }
    if (!m_sshPublicKeyPath.empty()) {
        res = curl_easy_setopt(m_curlHandle, CURLOPT_SSH_PUBLIC_KEYFILE, m_sshPublicKeyPath.c_str());
        if (res != CURLE_OK) {
            std::cerr << "SftpTarget: Failed to set CURLOPT_SSH_PUBLIC_KEYFILE '" << m_sshPublicKeyPath << "': " << curl_easy_strerror(res) << std::endl;
            // goto error; // Similar consideration
        } else {
            std::cout << "SftpTarget: Set CURLOPT_SSH_PUBLIC_KEYFILE to " << m_sshPublicKeyPath << std::endl;
        }
    }
    if (!m_sshKeyPassphrase.empty() && !m_sshPrivateKeyPath.empty()) { // Only makes sense if private key is set
        res = curl_easy_setopt(m_curlHandle, CURLOPT_KEYPASSWD, m_sshKeyPassphrase.c_str());
        if (res != CURLE_OK) {
            std::cerr << "SftpTarget: Failed to set CURLOPT_KEYPASSWD: " << curl_easy_strerror(res) << std::endl;
            // Potentially sensitive, but failure here might mean key is unusable.
            // goto error; // Similar consideration
        }
        // Do not log successful setting of passphrase for security.
    }

    // Host Key Verification
    if (m_sshSkipHostVerification) {
        std::cerr << "SftpTarget: WARNING - SSH host key verification is disabled via m_sshSkipHostVerification. This is insecure and should only be used for testing." << std::endl;
        // The options CURLOPT_SSH_VERIFYHOST and CURLOPT_SSH_VERIFYPEER do not exist in standard libcurl.
        // Host key verification is typically managed via CURLOPT_SSH_KNOWNHOSTS.
        // If m_sshSkipHostVerification is true, one might avoid setting CURLOPT_SSH_KNOWNHOSTS
        // or use other mechanisms if available and truly needed, though it's insecure.
        // For now, commenting out the problematic lines that cause a compile error.
        // res = curl_easy_setopt(m_curlHandle, CURLOPT_SSH_VERIFYHOST, 0L); // Erroneous option
        // if (res != CURLE_OK) {
        //     std::cerr << "SftpTarget: Failed to disable CURLOPT_SSH_VERIFYHOST: " << curl_easy_strerror(res) << std::endl;
        //     // goto error; // Security setting failure could be critical
        // }
        // res = curl_easy_setopt(m_curlHandle, CURLOPT_SSH_VERIFYPEER, 0L); // Erroneous option
        // if (res != CURLE_OK) {
        //     std::cerr << "SftpTarget: Failed to disable CURLOPT_SSH_VERIFYPEER: " << curl_easy_strerror(res) << std::endl;
        //     // goto error;
        // }
    } else if (!m_sshKnownHostsPath.empty()) {
        res = curl_easy_setopt(m_curlHandle, CURLOPT_SSH_KNOWNHOSTS, m_sshKnownHostsPath.c_str());
        if (res != CURLE_OK) {
            std::cerr << "SftpTarget: Failed to set CURLOPT_SSH_KNOWNHOSTS '" << m_sshKnownHostsPath << "': " << curl_easy_strerror(res) << std::endl;
            // If known_hosts is specified but fails, it might be a configuration error.
            // goto error; // Consider this
        } else {
            std::cout << "SftpTarget: Set CURLOPT_SSH_KNOWNHOSTS to " << m_sshKnownHostsPath << std::endl;
        }
    }
    // If neither skip nor specific known_hosts, libcurl uses default behavior (e.g. ~/.ssh/known_hosts)

    std::cout << "SftpTarget: Session begun successfully." << std::endl;
    return true;

error:
    if (m_curlHandle) {
        curl_easy_cleanup(m_curlHandle);
        m_curlHandle = nullptr;
    }
    return false;
}

bool SftpTarget::sendFile(const std::string& relativePath, const FileMetadata& remoteRelativePath) {
    // Assuming remoteRelativePathAndSize is a string "path:size" or just "path" and we get size from source.
    // For this subtask, let's assume remoteRelativePathAndSize IS the remoteRelativePath.
    // We'll get size from relativePath.
    // const std::string& remoteRelativePath = remoteRelativePathAndSize; // Treat FileMetadata as remote relative path string
    // Per instructions: remoteRelativePathAndSize is the remoteRelativePath.
    // const std::string& remoteRelativePath = remoteRelativePath; // This line is removed as parameter name matches


    std::cout << "SftpTarget: sendFile(" << relativePath << ", remote_path: " << remoteRelativePath << ") called." << std::endl;
    if (!m_curlHandle) {
        std::cerr << "SftpTarget: Session not begun or curl handle not initialized." << std::endl;
        return false;
    }

    std::ifstream sourceFile(relativePath, std::ios::binary);
    if (!sourceFile.is_open()) {
        std::cerr << "SftpTarget: Failed to open source file: " << relativePath << std::endl;
        return false;
    }

    sourceFile.seekg(0, std::ios::end);
    curl_off_t fileSize = sourceFile.tellg();
    sourceFile.seekg(0, std::ios::beg);

    if (fileSize < 0) { // Or == -1, depending on platform for tellg error
        std::cerr << "SftpTarget: Failed to get size of source file: " << relativePath << std::endl;
        sourceFile.close();
        return false;
    }
    
    // Use the new helper function to build the remote URL
    // std::string remoteUrl = buildSftpUrl(m_host, m_port, m_remoteBasePath, remoteRelativePath);

    // New URL construction logic with QUrl encoding
    // Nettoyer les chaînes
    QString host_qs = QString::fromStdString(m_host).trimmed();
    // remoteRelativePath is the second parameter of sendFile (const FileMetadata&, which is std::string)
    QString path_qs = QString::fromStdString(remoteRelativePath).trimmed();
    if (path_qs.startsWith("/")) {
        path_qs.remove(0, 1);  // éviter les "//" for the QString::arg based construction if it were used,
                               // but for QUrl path setting, a leading '/' is usually expected for absolute paths.
                               // The logic below re-adds it if necessary.
    }

    // Encode the path component for URL safety
    // We want to encode spaces and special characters, but keep '/' for directory structure.
    // Using QUrl::toPercentEncoding(stringToEncode, excludeChars, includeChars)
    // Exclude nothing by default, include '/' to ensure it's NOT encoded if present.
    QString encoded_path_qs = QUrl::toPercentEncoding(path_qs, QByteArray(), QByteArray("/"));

    QUrl url;
    url.setScheme("sftp");
    url.setHost(host_qs);
    url.setPort(m_port);
    // Set the path using StrictMode to prevent QUrl from re-interpreting percent signs in encoded_path_qs
    // and to ensure it correctly handles the path starting with '/' if encoded_path_qs has it.
    if (encoded_path_qs.isEmpty() || encoded_path_qs == "/") {
        url.setPath("/"); // Explicitly set to root if path is empty or just a slash
    } else if (encoded_path_qs.startsWith("/")) {
        // If encoded_path_qs already starts with a slash (e.g. if original path_qs was "/" and toPercentEncoding preserved it)
        url.setPath(encoded_path_qs, QUrl::StrictMode);
    } else {
        // Prepend slash if encoded_path_qs is like "foo/bar" or "file%20name.txt"
        url.setPath("/" + encoded_path_qs, QUrl::StrictMode);
    }

    QString url_qstring = url.toString(QUrl::FullyEncoded); // Use FullyEncoded to ensure percent-encodings are preserved

    // Updated Debug Logs
    qDebug() << "SFTP URL construction: Original raw path for QUrl processing: " << path_qs;
    qDebug() << "SFTP URL construction: Percent-encoded path component: " << encoded_path_qs;
    qDebug() << "Final URL passed to libcurl: " << url_qstring;

    // Vérification minimale (optional, as QUrl should construct valid ones)
    if (!url_qstring.startsWith("sftp://")) {
        qWarning() << "Malformed URL generated by QUrl logic: " << url_qstring;
    }

    std::string remoteUrl = url_qstring.toUtf8().constData(); // Convert to std::string for existing variable

    // This cout is more for general operation, qDebug above is for URL specific debugging
    std::cout << "SftpTarget: Uploading to URL: " << remoteUrl << std::endl;

    CURLcode res;
    res = curl_easy_setopt(m_curlHandle, CURLOPT_URL, remoteUrl.c_str());
    if (res != CURLE_OK) {
        std::cerr << "SftpTarget: Failed to set CURLOPT_URL: " << curl_easy_strerror(res) << std::endl;
        sourceFile.close();
        return false;
    }

    res = curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 1L);
    if (res != CURLE_OK) {
        std::cerr << "SftpTarget: Failed to set CURLOPT_UPLOAD: " << curl_easy_strerror(res) << std::endl;
        sourceFile.close();
        return false;
    }

    // Option to create remote directories if they don't exist.
    // CURLFTP_CREATE_DIR_RETRY (2L) will attempt to create dirs and retry if simple MKD fails.
    res = curl_easy_setopt(m_curlHandle, CURLOPT_FTP_CREATE_MISSING_DIRS, (long)CURLFTP_CREATE_DIR_RETRY);
    if (res != CURLE_OK) {
        std::cerr << "SftpTarget: Failed to set CURLOPT_FTP_CREATE_MISSING_DIRS: " << curl_easy_strerror(res) << std::endl;
        // This might not be fatal, server might already have dirs or not support this. Continue.
    }
    
    res = curl_easy_setopt(m_curlHandle, CURLOPT_READFUNCTION, read_callback);
    if (res != CURLE_OK) {
        std::cerr << "SftpTarget: Failed to set CURLOPT_READFUNCTION: " << curl_easy_strerror(res) << std::endl;
        sourceFile.close();
        return false;
    }

    res = curl_easy_setopt(m_curlHandle, CURLOPT_READDATA, &sourceFile);
    if (res != CURLE_OK) {
        std::cerr << "SftpTarget: Failed to set CURLOPT_READDATA: " << curl_easy_strerror(res) << std::endl;
        sourceFile.close();
        return false;
    }

    res = curl_easy_setopt(m_curlHandle, CURLOPT_INFILESIZE_LARGE, fileSize);
    if (res != CURLE_OK) {
        std::cerr << "SftpTarget: Failed to set CURLOPT_INFILESIZE_LARGE: " << curl_easy_strerror(res) << std::endl;
        sourceFile.close();
        return false;
    }

    res = curl_easy_perform(m_curlHandle);
    sourceFile.close(); // Close file regardless of performance outcome

    if (res != CURLE_OK) {
        std::cerr << "SftpTarget: curl_easy_perform() failed for sendFile: " << curl_easy_strerror(res) << std::endl;
        // Reset options that might interfere with next call on same handle, or rely on setting them fresh
        curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 0L); // Important to reset after an upload
        return false;
    }

    std::cout << "SftpTarget: File sent successfully." << std::endl;
    // Reset options for next potential call, or ensure they are set explicitly each time.
    // For safety, reset upload mode.
    curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 0L);
    return true;
}

bool SftpTarget::deleteFile(const std::string& relativePath) {
    std::cout << "SftpTarget: deleteFile(" << relativePath << ") called (stub)" << std::endl;
    if (!m_curlHandle) {
        std::cerr << "SftpTarget: Session not begun or curl handle not initialized." << std::endl;
        return false;
    }
    // Actual SFTP delete logic using libcurl (e.g., custom RM command or quote)
   
    // The `remoteUrl` for connecting. For delete, this can be the base path or root.
    // Let's use the base path of the configured target.
    std::string connectionUrl = buildSftpUrl(m_host, m_port, m_remoteBasePath, "");
    // Or, connect to root: std::string connectionUrl = buildSftpUrl(m_host, m_port, "/", "");

    std::cout << "SftpTarget: Establishing connection via URL for delete: " << connectionUrl << std::endl;

    // The path for the RM command needs to be an absolute path on the server.
    std::string absolutePathForRm = buildSftpAbsolutePath(m_remoteBasePath, relativePath);
    
    std::cout << "SftpTarget: Absolute path for RM command: " << absolutePathForRm << std::endl;

    // To delete a file with SFTP using libcurl, you typically issue a "RM" command.
    // This is done using CURLOPT_QUOTE or CURLOPT_CUSTOMREQUEST.
    // For SFTP, it's usually a list of post-transfer QUOTE commands.
    // A simpler way might be to use a custom request "RM /path/to/file" but this is FTP like.
    // SFTP delete command is typically "rm \"filepath\""
    // Let's try with CURLOPT_POSTQUOTE. The commands are executed *after* a transfer.
    // We need to perform a dummy operation like getting info of the file.
    // Or, if libcurl supports it directly for SFTP (less common).

    // A common way for SFTP:
    // 1. Set URL to the file: curl_easy_setopt(m_curlHandle, CURLOPT_URL, remoteUrl.c_str());
    // 2. Add "RM <remote_file_path_absolute_on_server>" to post-quote list.
    // The path for RM should be absolute from the SFTP server's root, not relative to current dir.
    // The path for RM should be absolute from the SFTP server's root.
    // buildSftpAbsolutePath should provide this.
    std::string rmCommand = "rm \"" + absolutePathForRm + "\"";
    // std::cout << "SftpTarget: Delete command: " << rmCommand << std::endl; // Will be logged before perform

    struct curl_slist *headerlist = NULL;
    headerlist = curl_slist_append(headerlist, rmCommand.c_str());

    CURLcode res;
    // We must set a URL for the connection to be established. The command acts on this context.
    // Using the connectionUrl (base path or root)
    res = curl_easy_setopt(m_curlHandle, CURLOPT_URL, connectionUrl.c_str());
    if (res != CURLE_OK) {
         std::cerr << "SftpTarget: Failed to set CURLOPT_URL for delete context: " << curl_easy_strerror(res) << std::endl;
         if(headerlist) curl_slist_free_all(headerlist);
         return false;
    }
    // res = curl_easy_setopt(m_curlHandle, CURLOPT_NOBODY, 1L); // Do not expect a body
    // if (res != CURLE_OK) {
    //      std::cerr << "SftpTarget: Failed to set CURLOPT_NOBODY for delete: " << curl_easy_strerror(res) << std::endl;
    //      curl_slist_free_all(headerlist);
    //      return false;
    // }

    // Using CURLOPT_QUOTE for SFTP delete. These are commands executed on the server after the main request.
    // Since there is no main data transfer for delete, this is a bit of a workaround.
    // Some SFTP servers might not support this well or require specific syntax.
    res = curl_easy_setopt(m_curlHandle, CURLOPT_QUOTE, headerlist);
    if (res != CURLE_OK) {
        std::cerr << "SftpTarget: Failed to set CURLOPT_QUOTE for delete: " << curl_easy_strerror(res) << std::endl;
        curl_slist_free_all(headerlist);
        return false;
    }

    std::cout << "SftpTarget: Attempting to delete file. Target URL for operation: " << connectionUrl 
              << ", Command: " << rmCommand << std::endl;

    res = curl_easy_perform(m_curlHandle);
    curl_slist_free_all(headerlist); // Clean up the command list
    curl_easy_setopt(m_curlHandle, CURLOPT_QUOTE, NULL); // Reset for next operation
    // curl_easy_setopt(m_curlHandle, CURLOPT_NOBODY, 0L); 


    if (res != CURLE_OK) {
        // Note: Deleting a non-existent file might also return an error.
        // Check specific error codes if finer-grained handling is needed.
        std::cerr << "SftpTarget: curl_easy_perform() failed for deleteFile (" << relativePath
                  << "). Error: " << curl_easy_strerror(res) << std::endl;
        return false;
    }
    
    std::cout << "SftpTarget: File deletion command successfully executed for " << relativePath << std::endl;
    return true;
}

bool SftpTarget::endSession() {
    std::cout << "SftpTarget: endSession() called." << std::endl;
    if (m_curlHandle) {
        curl_easy_cleanup(m_curlHandle);
        m_curlHandle = nullptr;
    }
    std::cout << "SftpTarget: Session ended." << std::endl;
    return true;
}

std::vector<std::string> SftpTarget::listFiles(const std::string& prefix) {
    std::cerr << "SftpTarget: listFiles is not implemented." << std::endl;
    // Return an empty vector as per the requirement
    return {};
}
