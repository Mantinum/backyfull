#ifndef GCSTARGET_H
#define GCSTARGET_H

#include "core/IStorageTarget.h"
#include "util/CredentialManager.h" // For CredentialManager
#include <string>
#include <vector>
#include <map>
#include <memory> // For std::unique_ptr
#include <curl/curl.h> // For CURL handle

class GcsTarget : public IStorageTarget {
public:
    explicit GcsTarget(const std::map<std::string, std::string>& config, CredentialManager* credentialManager = nullptr);
    ~GcsTarget() override;

    // IStorageTarget interface implementation
    bool beginSession() override;
    bool sendFile(const std::string& path, const FileMetadata& meta = FileMetadata{}) override; // Corrected
    std::vector<FileMetadata> listFiles(const std::string& path) override; // Corrected param name
    bool deleteFile(const std::string& remoteObjectName) override; // remoteObjectName is the path string
    bool downloadFile(const std::string& remoteObjectName, const std::string& localPath) override; // remoteObjectName is the path string
    bool endSession() override;

    std::string getLastError() const;

    // GCS Specific methods
    bool testConnection(std::string& errorMsg);

private:
    // OAuth2 and GCS interaction helper methods
    std::string getAccessToken();
    bool refreshAccessToken();

    // libcurl helper
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* s);
    // Note: performCurlRequest might need variants if used for both string and file stream outputs directly
    CURLcode performCurlRequest(const std::string& url, const std::string& method,
                                const std::vector<std::string>& headers,
                                const std::string& postData,
                                std::ifstream* inFileStream, // For uploads
                                std::string& responseBody,   // For string responses
                                long& responseCode);
    // Potentially a new one for downloads to ostream if needed, or adapt existing one.
    // For now, download directly configures curl in its method.


    // Configuration
    std::string m_bucketName;
    std::string m_objectPrefix; // Base prefix for this target instance, e.g., "backups/sourceA/"
    std::string m_accountIdentifier;
    std::string m_clientId;
    std::string m_clientSecret;
    std::string m_redirectUri;

    // State
    CredentialManager* m_credentialManager;
    CURL* m_curlHandle;
    std::string m_currentAccessToken;
    long long m_accessTokenExpiryTime;

    std::string m_lastError;

    bool m_properlyConfigured;
};

#endif // GCSTARGET_H
