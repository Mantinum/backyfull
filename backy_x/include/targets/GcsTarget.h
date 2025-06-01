#ifndef GCSTARGET_H
#define GCSTARGET_H

#include "core/IStorageTarget.h"
#include "util/CredentialManager.h" // For CredentialManager
#include <string>
#include <vector>
#include <map>
#include <memory> // For std::unique_ptr
#include <curl/curl.h> // For CURL handle

class QTcpServer;

class GcsTarget : public IStorageTarget {
public:
    explicit GcsTarget(const std::map<std::string, std::string>& config, CredentialManager* credentialManager = nullptr);
    ~GcsTarget() override;

    // IStorageTarget interface implementation
    bool beginSession() override;
    bool sendFile(const std::string& localPath, const FileMetadata& remoteObjectName) override;
    std::vector<IStorageTarget::FileMetadata> listFiles(const std::string& /*listPrefix*/) override;
    bool deleteFile(const std::string& /*remoteObjectName*/) override;
    bool endSession() override;

    // Public OAuth method for UI
    bool initiateOAuthAndStoreToken();
    std::string getLastError() const;

    // GCS Specific methods
    bool testConnection(std::string& errorMsg); // Method for testing connection

private:
    // OAuth2 and GCS interaction helper methods
    std::string getAccessToken();
    bool refreshAccessToken();
    bool performInitialOAuthFlow();
    void stopLocalCallbackServer();

    // libcurl helper
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* s);
    CURLcode performCurlRequest(const std::string& url, const std::string& method,
                                const std::vector<std::string>& headers,
                                const std::string& postData,
                                std::ifstream* inFileStream,
                                std::string& responseBody,
                                long& responseCode);

    // Configuration
    std::string m_bucketName;
    std::string m_objectPrefix;
    std::string m_accountIdentifier;
    std::string m_clientId;
    std::string m_clientSecret;
    std::string m_redirectUri;

    // State
    CredentialManager* m_credentialManager;
    CURL* m_curlHandle;
    std::string m_currentAccessToken;
    long long m_accessTokenExpiryTime;

    QTcpServer* m_oauthCallbackServer;
    std::string m_authorizationCode;
    bool m_oauthFlowCompletedSuccessfully;
    std::string m_lastError;

    bool m_properlyConfigured;
};

#endif // GCSTARGET_H
