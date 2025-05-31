#ifndef SFTPTARGET_H
#define SFTPTARGET_H

#include "core/IStorageTarget.h"
#include <string>
#include <vector>
#include <map>    // For configuration
#include <memory> // For std::unique_ptr

#include "util/CredentialManager.h" // For CredentialManager

#include <curl/curl.h> // For CURL handle type definition

class SftpTarget : public IStorageTarget {
public:
    SftpTarget(const std::map<std::string, std::string>& config);
    ~SftpTarget() override;

    // Inherited via IStorageTarget
    bool beginSession() override;
    // Changed second parameter name from metadata to remoteRelativePath for clarity
    bool sendFile(const std::string& relativePath, const FileMetadata& remoteRelativePath) override;
    bool deleteFile(const std::string& relativePath) override;
    std::vector<std::string> listFiles(const std::string& prefix) override;
    bool endSession() override;

private:
    std::string m_host;
    std::string m_username;
    std::string m_password; // Alternatively, path to a key file
    std::string m_remoteBasePath;
    int m_port;

    CURL* m_curlHandle; // For libcurl operations
    std::unique_ptr<CredentialManager> m_credentialManager;

    // New SSH configuration members
    std::string m_sshPublicKeyPath;
    std::string m_sshPrivateKeyPath;
    std::string m_sshKeyPassphrase; // Potentially sensitive, handle with care
    std::string m_sshKnownHostsPath;
    bool m_sshSkipHostVerification;
    long m_sshAuthTypes; 
    bool m_verboseLogging;
    bool m_properlyConfigured; // True if essential configuration (host, user) is present
};

#endif // SFTPTARGET_H
