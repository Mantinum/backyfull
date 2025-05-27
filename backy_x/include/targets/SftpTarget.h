#ifndef SFTPTARGET_H
#define SFTPTARGET_H

#include "core/IStorageTarget.h"
#include <string>
#include <vector>
#include <map>    // For configuration
#include <memory> // For std::unique_ptr

#include "util/CredentialManager.h" // For CredentialManager

// Forward declaration for CURL handle
struct CURL;

class SftpTarget : public IStorageTarget {
public:
    SftpTarget(const std::map<std::string, std::string>& config);
    ~SftpTarget() override;

    // Inherited via IStorageTarget
    bool beginSession() override;
    bool sendFile(const std::string& relativePath, const FileMetadata& metadata) override;
    bool deleteFile(const std::string& relativePath) override;
    bool endSession() override;

private:
    std::string m_host;
    std::string m_username;
    std::string m_password; // Alternatively, path to a key file
    std::string m_remoteBasePath;
    int m_port;

    CURL* m_curlHandle; // For libcurl operations
    std::unique_ptr<CredentialManager> m_credentialManager;
    // Add other necessary members, e.g., for libssh2 if used directly
};

#endif // SFTPTARGET_H
