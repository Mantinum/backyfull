#include "targets/SftpTarget.h"
#include <iostream> // For std::cerr/std::cout
#include <fstream>  // For std::ifstream
#include <filesystem> // For std::filesystem::path (C++17)
#include <vector>   // For directory creation components

#include <QString> // For service name and password with CredentialManager

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


SftpTarget::SftpTarget(const std::map<std::string, std::string>& config)
    : m_curlHandle(nullptr), m_port(22), m_credentialManager(nullptr) {
    // Ensure m_curlHandle is null
    m_curlHandle = nullptr;
    m_credentialManager = std::unique_ptr<CredentialManager>(createPlatformCredentialManager());

    auto itHost = config.find("host");
    if (itHost != config.end()) {
        m_host = itHost->second;
    } else {
        std::cerr << "SftpTarget: 'host' not found in configuration. Cannot proceed." << std::endl;
        // Consider throwing an exception or setting an error state
        return;
    }

    auto itUser = config.find("username");
    if (itUser != config.end()) {
        m_username = itUser->second;
    } else {
        std::cerr << "SftpTarget: 'username' not found in configuration. Cannot proceed." << std::endl;
        // Consider throwing an exception or setting an error state
        return;
    }
    
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
    
    auto itPort = config.find("port");
    if (itPort != config.end()) {
        try {
            m_port = std::stoi(itPort->second);
        } catch (const std::exception& e) {
            std::cerr << "SftpTarget: Invalid port number '" << itPort->second << "'. Using default 22. Error: " << e.what() << std::endl;
            m_port = 22;
        }
    }

    // Initialize CURL globally if not already done (for thread safety, once per app)
    // curl_global_init(CURL_GLOBAL_ALL); // This should ideally be in main() or app startup
    std::cout << "SftpTarget configured for host: " << m_host << ", user: " << m_username << ", path: " << m_remoteBasePath << ", port: " << m_port << std::endl;
}

SftpTarget::~SftpTarget() {
    if (m_curlHandle) {
        curl_easy_cleanup(m_curlHandle);
        m_curlHandle = nullptr;
    }
    // Global cleanup if SftpTarget was the only user (ideally in main() or app shutdown)
    // curl_global_cleanup(); 
    std::cout << "SftpTarget destroyed." << std::endl;
}

bool SftpTarget::beginSession() {
    // Initialize CURL globally once. This is ideally done in main().
    // For library code, it's tricky. Assuming it's done elsewhere.
    // curl_global_init(CURL_GLOBAL_DEFAULT);

    std::cout << "SftpTarget: beginSession() called." << std::endl;
    if (m_host.empty() || m_username.empty()) {
        std::cerr << "SftpTarget: Host or username is empty. Cannot begin session." << std::endl;
        return false;
    }

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

    // Enable verbose mode for debugging
    res = curl_easy_setopt(m_curlHandle, CURLOPT_VERBOSE, 1L);
    if (res != CURLE_OK) {
        std::cerr << "SftpTarget: Failed to set CURLOPT_VERBOSE: " << curl_easy_strerror(res) << std::endl;
        // Not a fatal error for functionality, but good to know
    }
    
    // Set a preferred SSH authentication type if needed, e.g., password.
    // This can help if the server offers multiple types and default selection is problematic.
    // res = curl_easy_setopt(m_curlHandle, CURLOPT_SSH_AUTH_TYPES, CURLSSH_AUTH_PASSWORD);
    // if (res != CURLE_OK) {
    //     std::cerr << "SftpTarget: Failed to set CURLOPT_SSH_AUTH_TYPES: " << curl_easy_strerror(res) << std::endl;
    //     goto error;
    // }


    std::cout << "SftpTarget: Session begun successfully." << std::endl;
    return true;

error:
    if (m_curlHandle) {
        curl_easy_cleanup(m_curlHandle);
        m_curlHandle = nullptr;
    }
    return false;
}

bool SftpTarget::sendFile(const std::string& sourceAbsolutePath, const FileMetadata& remoteRelativePathAndSize) {
    // Assuming remoteRelativePathAndSize is a string "path:size" or just "path" and we get size from source.
    // For this subtask, let's assume remoteRelativePathAndSize IS the remoteRelativePath.
    // We'll get size from sourceAbsolutePath.
    const std::string& remoteRelativePath = remoteRelativePathAndSize; // Treat FileMetadata as remote relative path string

    std::cout << "SftpTarget: sendFile(" << sourceAbsolutePath << ", remote_path: " << remoteRelativePath << ") called." << std::endl;
    if (!m_curlHandle) {
        std::cerr << "SftpTarget: Session not begun or curl handle not initialized." << std::endl;
        return false;
    }

    std::ifstream sourceFile(sourceAbsolutePath, std::ios::binary);
    if (!sourceFile.is_open()) {
        std::cerr << "SftpTarget: Failed to open source file: " << sourceAbsolutePath << std::endl;
        return false;
    }

    sourceFile.seekg(0, std::ios::end);
    curl_off_t fileSize = sourceFile.tellg();
    sourceFile.seekg(0, std::ios::beg);

    if (fileSize < 0) { // Or == -1, depending on platform for tellg error
        std::cerr << "SftpTarget: Failed to get size of source file: " << sourceAbsolutePath << std::endl;
        sourceFile.close();
        return false;
    }
    
    std::string remoteUrl = "sftp://" + m_host + ":" + std::to_string(m_port);
    // Ensure m_remoteBasePath always ends with a slash if it's not empty
    if (!m_remoteBasePath.empty() && m_remoteBasePath.back() != '/') {
        remoteUrl += "/" + m_remoteBasePath + "/";
    } else if (m_remoteBasePath.empty() || m_remoteBasePath == "/") {
        remoteUrl += "/"; // Root path
         // Remove leading slash from remoteRelativePath if m_remoteBasePath is effectively root or empty
        if (!remoteRelativePath.empty() && remoteRelativePath.front() == '/') {
             remoteUrl += remoteRelativePath.substr(1);
        } else {
             remoteUrl += remoteRelativePath;
        }
    } else { // m_remoteBasePath is not empty and ends with /
        remoteUrl += m_remoteBasePath;
        // Remove leading slash from remoteRelativePath if present, as m_remoteBasePath already provides trailing slash
        if (!remoteRelativePath.empty() && remoteRelativePath.front() == '/') {
             remoteUrl += remoteRelativePath.substr(1);
        } else {
             remoteUrl += remoteRelativePath;
        }
    }
    
    // Normalize URL: remove double slashes if any, except in "sftp://"
    size_t doubleSlashPos = remoteUrl.find("//", 6); // Start search after "sftp://"
    while(doubleSlashPos != std::string::npos) {
        remoteUrl.replace(doubleSlashPos, 2, "/");
        doubleSlashPos = remoteUrl.find("//", 6);
    }


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

bool SftpTarget::deleteFile(const std::string& remoteRelativePath) {
    std::cout << "SftpTarget: deleteFile(" << remoteRelativePath << ") called (stub)" << std::endl;
    if (!m_curlHandle) {
        std::cerr << "SftpTarget: Session not begun or curl handle not initialized." << std::endl;
        return false;
    }
    // Actual SFTP delete logic using libcurl (e.g., custom RM command or quote)
    
    // Construct full URL
    std::string remoteUrl = "sftp://" + m_host + ":" + std::to_string(m_port);
    if (!m_remoteBasePath.empty() && m_remoteBasePath.back() != '/') {
        remoteUrl += "/" + m_remoteBasePath + "/";
    } else if (m_remoteBasePath.empty() || m_remoteBasePath == "/"){
        remoteUrl += "/";
        if (!remoteRelativePath.empty() && remoteRelativePath.front() == '/') {
             remoteUrl += remoteRelativePath.substr(1);
        } else {
             remoteUrl += remoteRelativePath;
        }
    } else {
        remoteUrl += m_remoteBasePath;
         if (!remoteRelativePath.empty() && remoteRelativePath.front() == '/') {
             remoteUrl += remoteRelativePath.substr(1);
        } else {
             remoteUrl += remoteRelativePath;
        }
    }
     // Normalize URL: remove double slashes if any, except in "sftp://"
    size_t doubleSlashPos = remoteUrl.find("//", 6); // Start search after "sftp://"
    while(doubleSlashPos != std::string::npos) {
        remoteUrl.replace(doubleSlashPos, 2, "/");
        doubleSlashPos = remoteUrl.find("//", 6);
    }

    std::cout << "SftpTarget: Attempting to delete URL: " << remoteUrl << std::endl;

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
    // The URL path component is what we need for RM.
    std::string remoteFilePathForRm = remoteUrl.substr(remoteUrl.find('/', 7) + 1); // Path after sftp://host:port/
    // Ensure it doesn't have leading slash if m_remoteBasePath was /
    if (m_remoteBasePath == "/" || m_remoteBasePath.empty()){
         if(remoteFilePathForRm.front() == '/') remoteFilePathForRm = remoteFilePathForRm.substr(1);
    }


    std::string rmCommand = "rm \"" + remoteFilePathForRm + "\"";
    std::cout << "SftpTarget: Delete command: " << rmCommand << std::endl;

    struct curl_slist *headerlist = NULL;
    headerlist = curl_slist_append(headerlist, rmCommand.c_str());

    CURLcode res;
    // We must set a URL for the connection to be established. The command acts on this context.
    // We are not actually transferring data, so a "dummy" operation is needed.
    // Setting NOBODY is good for this.
    res = curl_easy_setopt(m_curlHandle, CURLOPT_URL, ("sftp://" + m_host + ":" + std::to_string(m_port) + "/").c_str()); // Connect to root
    if (res != CURLE_OK) {
         std::cerr << "SftpTarget: Failed to set CURLOPT_URL for delete: " << curl_easy_strerror(res) << std::endl;
         curl_slist_free_all(headerlist);
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

    res = curl_easy_perform(m_curlHandle);
    curl_slist_free_all(headerlist); // Clean up the command list
    curl_easy_setopt(m_curlHandle, CURLOPT_QUOTE, NULL); // Reset for next operation
    // curl_easy_setopt(m_curlHandle, CURLOPT_NOBODY, 0L); 


    if (res != CURLE_OK) {
        // Note: Deleting a non-existent file might also return an error.
        // Check specific error codes if finer-grained handling is needed.
        // For example, CURLE_REMOTE_FILE_NOT_FOUND (for FTP, SFTP might differ)
        std::cerr << "SftpTarget: curl_easy_perform() failed for deleteFile: " << curl_easy_strerror(res) << std::endl;
        // Try to check specific SFTP errors if available from libcurl/libssh2
        // For now, any error is a failure.
        return false;
    }
    
    std::cout << "SftpTarget: File deletion command sent successfully for " << remoteRelativePath << std::endl;
    return true;
}

bool SftpTarget::endSession() {
    std::cout << "SftpTarget: endSession() called." << std::endl;
    if (m_curlHandle) {
        curl_easy_cleanup(m_curlHandle);
        m_curlHandle = nullptr;
    }
    // Global cleanup if this was the only user, ideally in main.
    // curl_global_cleanup();
    std::cout << "SftpTarget: Session ended." << std::endl;
    return true;
}
