#ifndef GCSTARGET_H
#define GCSTARGET_H

#include "core/IStorageTarget.h"
#include "util/CredentialManager.h" // For CredentialManager
#include <string>
#include <vector>
#include <map>
#include <memory> // For std::unique_ptr
#include <curl/curl.h> // For CURL handle

// Forward declarations for Qt types if used in header (e.g. QTcpServer, QNetworkAccessManager)
// For now, keep Qt includes in .cpp if possible
class QTcpServer; // Example if it were needed in header

class GcsTarget : public IStorageTarget {
public:
    // Constructor might take config map or specific GCS params
    explicit GcsTarget(const std::map<std::string, std::string>& config);
    ~GcsTarget() override;

    // IStorageTarget interface implementation
    bool beginSession() override;
    bool sendFile(const std::string& localPath, const FileMetadata& remoteObjectName) override;
    std::vector<IStorageTarget::FileMetadata> listFiles(const std::string& listPrefix) override; // Signature changed
    bool deleteFile(const std::string& remoteObjectName) override;
    bool endSession() override;

    // GCS Specific methods (if any needed publicly)
    // bool testConnection(); // Example

private:
    // OAuth2 and GCS interaction helper methods
    std::string getAccessToken();
    bool refreshAccessToken();
    bool performInitialOAuthFlow(); // Triggers browser, local server for callback
    void stopLocalCallbackServer();

    // libcurl helper
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* s);
    CURLcode performCurlRequest(const std::string& url, const std::string& method,
                                const std::vector<std::string>& headers,
                                const std::string& postData, // For POST/PUT
                                std::ifstream* inFileStream, // For file uploads
                                std::string& responseBody,
                                long& responseCode);

    // Configuration
    std::string m_bucketName;
    std::string m_objectPrefix;
    std::string m_accountIdentifier; // Used as key for CredentialManager
    std::string m_clientId;          // OAuth2 Client ID
    std::string m_clientSecret;      // OAuth2 Client Secret
    std::string m_redirectUri;       // OAuth2 Redirect URI

    // State
    std::unique_ptr<CredentialManager> m_credentialManager;
    CURL* m_curlHandle;
    std::string m_currentAccessToken;
    long long m_accessTokenExpiryTime; // e.g., seconds since epoch

    QTcpServer* m_oauthCallbackServer; // For listening to OAuth redirect
    std::string m_authorizationCode;   // Temp storage for auth code
    bool m_oauthFlowCompletedSuccessfully;

    bool m_properlyConfigured;
};

#endif // GCSTARGET_H
