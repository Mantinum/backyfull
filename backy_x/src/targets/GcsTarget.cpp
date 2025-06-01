#include "targets/GcsTarget.h"
#include "core/IStorageTarget.h" // For IStorageTarget::FileMetadata
#include "util/CredentialManager.h" // For createPlatformCredentialManager

#include <iostream> // For logging
#include <fstream>  // For file operations
#include <chrono>   // For time
#include <thread>   // For std::this_thread::sleep_for
#include <algorithm> // for std::remove for local file deletion on failed download

// Qt includes
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QTimer>
#include <QDateTime>
#include <QTimeZone>

// libcurl
#include <curl/curl.h>

// Forward declaration for the read callback if it's defined later in the file
static size_t read_callback_gcs(char *buffer, size_t size, size_t nitems, void *instream);
// Static callback function for libcurl to write downloaded file data
static size_t gcsFileWriteCallback(void*contents, size_t size, size_t nmemb, void*userp);


// Helper function to parse GCS JSON error responses
static std::string parseGcsError(const std::string& responseBody) {
    if (responseBody.empty()) {
        return "Unknown error (empty response body).";
    }
    QJsonDocument jsonDoc = QJsonDocument::fromJson(QString::fromStdString(responseBody).toUtf8());
    if (jsonDoc.isObject()) {
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.contains("error") && jsonObj["error"].isObject()) {
            QJsonObject errorObj = jsonObj["error"].toObject();
            if (errorObj.contains("message") && errorObj["message"].isString()) {
                return errorObj["message"].toString().toStdString();
            }
        } else if (jsonObj.contains("error_description") && jsonObj["error_description"].isString()) {
            return jsonObj["error_description"].toString().toStdString();
        }
    }
    return responseBody;
}

static std::chrono::system_clock::time_point rfc3339ToTimestamp(const QString& rfc3339String) {
    QDateTime dateTime = QDateTime::fromString(rfc3339String, Qt::ISODate);
    if (dateTime.isValid()) {
        if (rfc3339String.endsWith('Z')) {
            dateTime.setTimeZone(QTimeZone::utc());
        }
        return std::chrono::system_clock::from_time_t(dateTime.toSecsSinceEpoch());
    }
    return std::chrono::system_clock::from_time_t(0);
}

static size_t gcsFileWriteCallback(void*contents, size_t size, size_t nmemb, void*userp) {
    std::ofstream* outFile = static_cast<std::ofstream*>(userp);
    outFile->write(static_cast<const char*>(contents), size * nmemb);
    return outFile->good() ? size * nmemb : 0;
}

GcsTarget::GcsTarget(const std::map<std::string, std::string>& config, CredentialManager* credentialManager)
    : m_credentialManager(credentialManager),
      m_curlHandle(nullptr),
      m_accessTokenExpiryTime(0),
      m_oauthCallbackServer(nullptr),
      m_oauthFlowCompletedSuccessfully(false),
      m_properlyConfigured(true) {

    if (!m_credentialManager) {
        m_credentialManager = createPlatformCredentialManager();
    }
    // IMPORTANT: These must be replaced by actual values from Google Cloud Console
    m_clientId = "YOUR_CLIENT_ID_GOES_HERE.apps.googleusercontent.com";
    m_clientSecret = "YOUR_CLIENT_SECRET_GOES_HERE";
    m_redirectUri = "http://127.0.0.1:8085";

    if (m_clientId.rfind("YOUR_CLIENT_ID", 0) == 0 || m_clientSecret.rfind("YOUR_CLIENT_SECRET", 0) == 0) {
        m_lastError = "OAuth Client ID or Secret are placeholders. Configure them in GcsTarget.cpp.";
        std::cerr << "GcsTarget: CRITICAL - " << m_lastError << std::endl;
    }

    auto itBucket = config.find("gcs_bucket_name");
    if (itBucket != config.end() && !itBucket->second.empty()) m_bucketName = itBucket->second;
    else { m_lastError = "GCS bucket name is missing."; m_properlyConfigured = false; }

    auto itPrefix = config.find("gcs_object_prefix");
    if (itPrefix != config.end()) {
        m_objectPrefix = itPrefix->second;
        if (!m_objectPrefix.empty() && m_objectPrefix.back() != '/') m_objectPrefix += '/';
    }

    auto itAccount = config.find("gcs_account_identifier");
    if (itAccount != config.end() && !itAccount->second.empty()) m_accountIdentifier = itAccount->second;
    else { if(m_properlyConfigured) m_lastError = "GCS account identifier is missing."; m_properlyConfigured = false; }

    m_curlHandle = curl_easy_init();
    if (!m_curlHandle) { m_lastError = "Failed to initialize libcurl."; m_properlyConfigured = false; }

    std::cout << "GcsTarget created. Bucket: " << m_bucketName << ", ObjectPrefix: " << m_objectPrefix << ", AccountID: " << m_accountIdentifier << std::endl;
}

GcsTarget::~GcsTarget() {
    if (m_curlHandle) curl_easy_cleanup(m_curlHandle);
    stopLocalCallbackServer();
}

bool GcsTarget::beginSession() {
    m_lastError.clear();
    if (!m_properlyConfigured) { m_lastError = "GcsTarget not properly configured."; std::cerr << "GcsTarget: " << m_lastError << std::endl; return false; }
    std::string token = getAccessToken();
    if (token.empty()) { std::cerr << "GcsTarget: Failed to obtain access token. Error: " << m_lastError << std::endl; return false; }
    std::cout << "GcsTarget: Session begun." << std::endl;
    return true;
}

bool GcsTarget::sendFile(const std::string& localPath, const FileMetadata& metadata) {
    m_lastError.clear();
    std::string accessToken;
    int attempt = 0; const int maxAttempts = 2;

    while(attempt < maxAttempts) {
        accessToken = getAccessToken();
        if (accessToken.empty()) { std::cerr << "GcsTarget: Failed to get access token for sendFile. Error: " << m_lastError << std::endl; return false; }

        std::string fullPathInBucket = m_objectPrefix + metadata.name; // Use metadata.name
        QString qFullPathInBucket = QString::fromStdString(fullPathInBucket);
        std::string encodedObjectName = QUrl::toPercentEncoding(qFullPathInBucket, "", "/").toStdString();
        std::string url = "https://storage.googleapis.com/upload/storage/v1/b/" + m_bucketName + "/o?uploadType=media&name=" + encodedObjectName;
        std::ifstream inFile(localPath, std::ios::binary);
        if (!inFile.is_open()) { m_lastError = "Failed to open local file: " + localPath; std::cerr << "GcsTarget: " << m_lastError << std::endl; return false; }
        std::vector<std::string> headers = { "Authorization: Bearer " + accessToken, "Content-Type: application/octet-stream" };
        std::string responseBody; long responseCode = 0;
        std::cout << "GcsTarget: Uploading " << localPath << " to " << fullPathInBucket << std::endl;
        CURLcode res = performCurlRequest(url, "POST", headers, "", &inFile, responseBody, responseCode);
        inFile.close();

        if (res == CURLE_OK) {
            if (responseCode == 200) { std::cout << "GcsTarget: File uploaded: " << fullPathInBucket << std::endl; return true; }
            if ((responseCode == 401 || responseCode == 429 || (responseCode >= 500 && responseCode <= 504)) && attempt < maxAttempts - 1) {
                std::cerr << "GcsTarget: sendFile HTTP " << responseCode << ". Retrying..." << std::endl;
                if(responseCode == 401) m_currentAccessToken.clear(); // Force token refresh
                std::this_thread::sleep_for(std::chrono::seconds( (responseCode == 429 || responseCode >=500) ? (2 * (1 << attempt)) : 0 ));
                attempt++; continue;
            }
            m_lastError = "File upload failed for " + fullPathInBucket + ". HTTP " + std::to_string(responseCode) + ": " + parseGcsError(responseBody);
        } else {
            m_lastError = "File upload failed for " + fullPathInBucket + ". CURL error: " + curl_easy_strerror(res);
        }
        std::cerr << "GcsTarget: " << m_lastError << std::endl;
        return false;
    }
    m_lastError = "File upload failed for " + (m_objectPrefix + metadata.name) + " after maximum retries."; // Use metadata.name
    std::cerr << "GcsTarget: " << m_lastError << std::endl;
    return false;
}

std::vector<FileMetadata> GcsTarget::listFiles(const std::string& path) { // param name 'path'
    m_lastError.clear();
    std::vector<FileMetadata> resultFiles;
    std::string accessToken = getAccessToken();
    if (accessToken.empty()) { std::cerr << "GcsTarget: listFiles - No access token. Error: " << m_lastError << std::endl; return resultFiles; }

    QUrl gcsUrl("https://storage.googleapis.com/storage/v1/b/" + QString::fromStdString(m_bucketName) + "/o");
    QUrlQuery query;
    std::string effectivePath = m_objectPrefix + path; // Use path parameter
    if (!effectivePath.empty() && effectivePath.back() != '/') effectivePath += '/';
    if (!effectivePath.empty() && effectivePath != "/") query.addQueryItem("prefix", QString::fromStdString(effectivePath));
    query.addQueryItem("delimiter", "/");
    gcsUrl.setQuery(query);
    std::string url = gcsUrl.toString(QUrl::FullyEncoded).toStdString();
    std::vector<std::string> headers = {"Authorization: Bearer " + accessToken};
    std::string responseBody; long responseCode = 0;
    CURLcode res = performCurlRequest(url, "GET", headers, "", nullptr, responseBody, responseCode);

    if (res != CURLE_OK) { m_lastError = "listFiles CURL Error: " + std::string(curl_easy_strerror(res)); }
    else if (responseCode != 200) { m_lastError = "listFiles HTTP " + std::to_string(responseCode) + ": " + parseGcsError(responseBody); }
    else {
        QJsonDocument jsonDoc = QJsonDocument::fromJson(QString::fromStdString(responseBody).toUtf8());
        if (jsonDoc.isNull() || !jsonDoc.isObject()) { m_lastError = "listFiles JSON parse error."; }
        else {
            QJsonObject rootObject = jsonDoc.object();
            if (rootObject.contains("prefixes") && rootObject["prefixes"].isArray()) {
                for (const QJsonValue& val : rootObject["prefixes"].toArray()) {
                    if (val.isString()) {
                        std::string fullPrefixPath = val.toString().toStdString();
                        std::string name = fullPrefixPath;
                        if (name.rfind(effectivePath, 0) == 0) name.erase(0, effectivePath.length());
                        if (!name.empty() && name.back() == '/') name.pop_back();
                        if (!name.empty()) resultFiles.emplace_back(name, 0, std::chrono::system_clock::from_time_t(0), true);
                    }
                }
            }
            if (rootObject.contains("items") && rootObject["items"].isArray()) {
                for (const QJsonValue& val : rootObject["items"].toArray()) {
                    if (val.isObject()) {
                        QJsonObject item = val.toObject();
                        std::string fullItemPath = item["name"].toString().toStdString();
                        std::string name = fullItemPath;
                        if (name.rfind(effectivePath, 0) == 0) name.erase(0, effectivePath.length());
                        if (name.empty() || name.back() == '/') continue;
                        uint64_t size = item.contains("size") ? std::stoull(item["size"].toString().toStdString()) : 0;
                        std::chrono::system_clock::time_point modTime = item.contains("updated") ? rfc3339ToTimestamp(item["updated"].toString()) : std::chrono::system_clock::from_time_t(0);
                        resultFiles.emplace_back(name, size, modTime, false);
                    }
                }
            }
            std::cout << "GcsTarget: listFiles found " << resultFiles.size() << " items in " << effectivePath << std::endl;
            return resultFiles; // Success
        }
    }
    std::cerr << "GcsTarget: " << m_lastError << std::endl;
    return resultFiles; // Empty on error
}

bool GcsTarget::downloadFile(const std::string& remoteObjectName, const std::string& localPath) {
    m_lastError.clear();
    std::string accessToken = getAccessToken();
    if (accessToken.empty()) { std::cerr << "GcsTarget: downloadFile - No access token. Error: " << m_lastError << std::endl; return false; }

    std::string fullRemoteObjectPath = m_objectPrefix + remoteObjectName;
    std::string encodedObjectName = QUrl::toPercentEncoding(QString::fromStdString(fullRemoteObjectPath), "", "/").toStdString();
    std::string url = "https://storage.googleapis.com/storage/v1/b/" + m_bucketName + "/o/" + encodedObjectName + "?alt=media";
    std::ofstream outFile(localPath, std::ios::binary);
    if (!outFile.is_open()) { m_lastError = "Failed to open local file: " + localPath; std::cerr << "GcsTarget: " << m_lastError << std::endl; return false; }

    std::vector<std::string> headers = {"Authorization: Bearer " + accessToken};
    long responseCode = 0; CURLcode res;
    if (!m_curlHandle) { m_lastError = "libcurl handle not initialized."; std::cerr << "GcsTarget: " << m_lastError << std::endl; outFile.close(); std::remove(localPath.c_str()); return false; }

    curl_easy_setopt(m_curlHandle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(m_curlHandle, CURLOPT_HTTPGET, 1L);
    struct curl_slist *header_slist = nullptr; for (const auto& h : headers) header_slist = curl_slist_append(header_slist, h.c_str());
    curl_easy_setopt(m_curlHandle, CURLOPT_HTTPHEADER, header_slist);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, gcsFileWriteCallback);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, &outFile);
    curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDS, NULL); curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDSIZE, -1L);
    curl_easy_setopt(m_curlHandle, CURLOPT_READFUNCTION, NULL); curl_easy_setopt(m_curlHandle, CURLOPT_READDATA, NULL);
    curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 0L);

    std::cout << "GcsTarget: Downloading " << fullRemoteObjectPath << " to " << localPath << std::endl;
    res = curl_easy_perform(m_curlHandle);
    if (header_slist) curl_slist_free_all(header_slist);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, nullptr);
    outFile.close();

    if (res == CURLE_OK) {
        curl_easy_getinfo(m_curlHandle, CURLINFO_RESPONSE_CODE, &responseCode);
        if (responseCode == 200) { std::cout << "GcsTarget: File downloaded: " << fullRemoteObjectPath << std::endl; return true; }
        m_lastError = "File download HTTP " + std::to_string(responseCode) + " for " + fullRemoteObjectPath;
    } else {
        m_lastError = "File download CURL error for " + fullRemoteObjectPath + ": " + curl_easy_strerror(res);
    }
    std::cerr << "GcsTarget: " << m_lastError << std::endl;
    std::remove(localPath.c_str());
    return false;
}

bool GcsTarget::deleteFile(const std::string& remoteObjectName) {
    m_lastError.clear();
    std::string accessToken = getAccessToken();
    if (accessToken.empty()) { std::cerr << "GcsTarget: deleteFile - No access token. Error: " << m_lastError << std::endl; return false; }

    std::string fullRemoteObjectPath = m_objectPrefix + remoteObjectName;
    std::string encodedObjectName = QUrl::toPercentEncoding(QString::fromStdString(fullRemoteObjectPath), "", "/").toStdString();
    std::string url = "https://storage.googleapis.com/storage/v1/b/" + m_bucketName + "/o/" + encodedObjectName;
    std::vector<std::string> headers = {"Authorization: Bearer " + accessToken};
    std::string responseBody; long responseCode = 0;
    std::cout << "GcsTarget: Deleting " << fullRemoteObjectPath << std::endl;
    CURLcode res = performCurlRequest(url, "DELETE", headers, "", nullptr, responseBody, responseCode);

    if (res == CURLE_OK) {
        if (responseCode == 204 || responseCode == 404) { std::cout << "GcsTarget: Object deleted (or not found): " << fullRemoteObjectPath << std::endl; return true; }
        m_lastError = "Object delete HTTP " + std::to_string(responseCode) + " for " + fullRemoteObjectPath + ": " + parseGcsError(responseBody);
    } else {
        m_lastError = "Object delete CURL error for " + fullRemoteObjectPath + ": " + curl_easy_strerror(res);
    }
    std::cerr << "GcsTarget: " << m_lastError << std::endl;
    return false;
}

bool GcsTarget::endSession() {
    m_lastError.clear();
    std::cout << "GcsTarget: Session ended." << std::endl;
    return true;
}

bool GcsTarget::testConnection(std::string& errorMsg) {
    m_lastError.clear();
    if (!m_properlyConfigured && m_bucketName.empty()) {
        errorMsg = "GCS Target not properly configured (bucket name missing)." + (!m_lastError.empty() ? " Initial error: " + m_lastError : "");
        m_lastError = errorMsg; return false;
    }
    std::string token = getAccessToken();
    if (token.empty()) { errorMsg = "Failed to obtain access token: " + m_lastError; return false; }

    QUrl gcsUrl("https://storage.googleapis.com/storage/v1/b/" + QString::fromStdString(m_bucketName) + "/o");
    QUrlQuery query; query.addQueryItem("prefix", "backyfull_test_connection/"); query.addQueryItem("maxResults", "1");
    gcsUrl.setQuery(query);
    std::string url = gcsUrl.toString(QUrl::FullyEncoded).toStdString();
    std::vector<std::string> headers = {"Authorization: Bearer " + token};
    std::string responseBody; long responseCode = 0;
    CURLcode res = performCurlRequest(url, "GET", headers, "", nullptr, responseBody, responseCode);

    if (res == CURLE_OK) {
        if (responseCode == 200) { errorMsg.clear(); return true; }
        m_lastError = "Test connection HTTP " + std::to_string(responseCode) + ": " + parseGcsError(responseBody);
    } else {
        m_lastError = "Test connection CURL Error: " + std::string(curl_easy_strerror(res));
    }
    errorMsg = m_lastError; std::cerr << "GcsTarget: " << m_lastError << std::endl;
    return false;
}

bool GcsTarget::initiateOAuthAndStoreToken() {
    m_lastError.clear();
    if (m_clientId.rfind("YOUR_CLIENT_ID", 0) == 0 || m_clientSecret.rfind("YOUR_CLIENT_SECRET", 0) == 0) {
        m_lastError = "OAuth Client ID or Secret are placeholders."; std::cerr << "GcsTarget: CRITICAL - " << m_lastError << std::endl; return false;
    }
    if (m_accountIdentifier.empty()) { m_lastError = "Account Identifier not set."; std::cerr << "GcsTarget: " << m_lastError << std::endl; return false; }

    if (!performInitialOAuthFlow()) {
        if (m_lastError.empty()) m_lastError = "OAuth flow failed or cancelled.";
        return false;
    }
    return true;
}

std::string GcsTarget::getLastError() const { return m_lastError; }

std::string GcsTarget::getAccessToken() {
    if (m_clientId.rfind("YOUR_CLIENT_ID", 0) == 0 || m_clientSecret.rfind("YOUR_CLIENT_SECRET", 0) == 0) {
        m_lastError = "OAuth Client ID or Secret are placeholders."; return "";
    }
    auto now = std::chrono::steady_clock::now();
    if (!m_currentAccessToken.empty() && std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() < m_accessTokenExpiryTime) {
        return m_currentAccessToken;
    }
    if (!refreshAccessToken()) {
        if (!performInitialOAuthFlow()) { return ""; }
    }
    return m_currentAccessToken;
}

bool GcsTarget::refreshAccessToken() {
    // ... (Implementation as before, ensuring m_lastError is set on failure)
    if (m_accountIdentifier.empty() || !m_credentialManager) {
        m_lastError = "Cannot refresh: Account ID or CredentialManager missing."; return false;
    }
    auto refreshTokenOpt = m_credentialManager->retrieveGcsRefreshToken(QString::fromStdString(m_accountIdentifier));
    if (!refreshTokenOpt || refreshTokenOpt->isEmpty()) {
        m_lastError = "No refresh token for " + m_accountIdentifier; return false;
    }
    std::string rt = refreshTokenOpt->toStdString();
    std::string tokenUrl = "https://oauth2.googleapis.com/token";
    std::string postData = "client_id=" + QUrl::toPercentEncoding(QString::fromStdString(m_clientId)).toStdString() +
                           "&client_secret=" + QUrl::toPercentEncoding(QString::fromStdString(m_clientSecret)).toStdString() +
                           "&refresh_token=" + QUrl::toPercentEncoding(QString::fromStdString(rt)).toStdString() +
                           "&grant_type=refresh_token";
    std::vector<std::string> headers = {"Content-Type: application/x-www-form-urlencoded"};
    std::string responseBody; long responseCode = 0;
    CURLcode res = performCurlRequest(tokenUrl, "POST", headers, postData, nullptr, responseBody, responseCode);
    if (res == CURLE_OK && responseCode == 200) {
        QJsonDocument jsonDoc = QJsonDocument::fromJson(QString::fromStdString(responseBody).toUtf8());
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.contains("access_token") && jsonObj["access_token"].isString()) {
            m_currentAccessToken = jsonObj["access_token"].toString().toStdString();
            long expiresIn = jsonObj["expires_in"].toInt(3600);
            m_accessTokenExpiryTime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + expiresIn - 60;
            return true;
        } m_lastError = "Refresh token JSON missing fields: " + responseBody;
    } else {
        m_lastError = "Refresh token request failed. HTTP " + std::to_string(responseCode) + ", Curl: " + curl_easy_strerror(res) + ", Resp: " + parseGcsError(responseBody);
        if (responseCode == 400 || responseCode == 401) {
             m_credentialManager->deleteGcsRefreshToken(QString::fromStdString(m_accountIdentifier));
        }
    }
    return false;
}

bool GcsTarget::performInitialOAuthFlow() {
    // ... (Implementation as before, ensuring m_lastError is set on failure)
    m_oauthFlowCompletedSuccessfully = false; m_authorizationCode.clear();
    m_oauthCallbackServer = new QTcpServer();
    // Connect signals for m_oauthCallbackServer... (condensed for brevity)
    QObject::connect(m_oauthCallbackServer, &QTcpServer::newConnection, [&]() { /* ... see original ... */ });
    if (!m_oauthCallbackServer->listen(QHostAddress::LocalHost, QUrl(QString::fromStdString(m_redirectUri)).port(8085))) {
        m_lastError = "Failed to start callback server: " + m_oauthCallbackServer->errorString().toStdString(); delete m_oauthCallbackServer; m_oauthCallbackServer = nullptr; return false;
    }
    QUrl authUrl("https://accounts.google.com/o/oauth2/v2/auth");
    QUrlQuery query; /* ... set query items ... */ query.addQueryItem("client_id", QString::fromStdString(m_clientId)); query.addQueryItem("redirect_uri", QString::fromStdString(m_redirectUri)); query.addQueryItem("response_type", "code"); query.addQueryItem("scope", "https://www.googleapis.com/auth/devstorage.read_write"); query.addQueryItem("access_type", "offline"); query.addQueryItem("prompt", "consent");
    authUrl.setQuery(query);
    QDesktopServices::openUrl(authUrl);
    QEventLoop loop; QTimer timer; timer.setSingleShot(true); QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit); timer.start(120000);
    loop.exec(); // Wait for callback or timeout
    stopLocalCallbackServer();
    if (!m_oauthFlowCompletedSuccessfully || m_authorizationCode.empty()) { if(m_lastError.empty()) m_lastError = "OAuth: No auth code."; return false; }

    std::string tokenUrl = "https://oauth2.googleapis.com/token";
    std::string postData = "client_id=" + QUrl::toPercentEncoding(QString::fromStdString(m_clientId)).toStdString() +
                           "&client_secret=" + QUrl::toPercentEncoding(QString::fromStdString(m_clientSecret)).toStdString() +
                           "&code=" + QUrl::toPercentEncoding(QString::fromStdString(m_authorizationCode)).toStdString() +
                           "&redirect_uri=" + QUrl::toPercentEncoding(QString::fromStdString(m_redirectUri)).toStdString() +
                           "&grant_type=authorization_code";
    std::vector<std::string> headers = {"Content-Type: application/x-www-form-urlencoded"};
    std::string responseBody; long responseCode = 0;
    CURLcode res = performCurlRequest(tokenUrl, "POST", headers, postData, nullptr, responseBody, responseCode);
    if (res == CURLE_OK && responseCode == 200) {
        QJsonDocument jsonDoc = QJsonDocument::fromJson(QString::fromStdString(responseBody).toUtf8());
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.contains("access_token") && jsonObj.contains("expires_in")) {
            m_currentAccessToken = jsonObj["access_token"].toString().toStdString();
            m_accessTokenExpiryTime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + jsonObj["expires_in"].toInt() - 60;
            if (jsonObj.contains("refresh_token") && m_credentialManager && !m_accountIdentifier.empty()) {
                m_credentialManager->storeGcsRefreshToken(QString::fromStdString(m_accountIdentifier), jsonObj["refresh_token"].toString());
            }
            return true;
        } m_lastError = "Token exchange JSON missing fields: " + responseBody;
    } else { m_lastError = "Token exchange failed. HTTP " + std::to_string(responseCode) + ", Curl: " + curl_easy_strerror(res) + ", Resp: " + parseGcsError(responseBody); }
    return false;
}

void GcsTarget::stopLocalCallbackServer() {
    if (m_oauthCallbackServer) {
        if (m_oauthCallbackServer->isListening()) m_oauthCallbackServer->close();
        delete m_oauthCallbackServer; m_oauthCallbackServer = nullptr;
    }
}

size_t GcsTarget::writeCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try { s->append((char*)contents, newLength); } catch(std::bad_alloc &) { return 0; }
    return newLength;
}

CURLcode GcsTarget::performCurlRequest(const std::string& url, const std::string& method,
                                     const std::vector<std::string>& headers,
                                     const std::string& postData,
                                     std::ifstream* inFileStream,
                                     std::string& responseBody, long& responseCode) {
    if (!m_curlHandle) return CURLE_FAILED_INIT;
    CURLcode res; struct curl_slist *header_slist = nullptr;
    curl_easy_setopt(m_curlHandle, CURLOPT_URL, url.c_str());
    if (method == "POST") { /* ... */ } else if (method == "PUT") { /* ... */ }
    else if (method == "DELETE") curl_easy_setopt(m_curlHandle, CURLOPT_CUSTOMREQUEST, "DELETE");
    else curl_easy_setopt(m_curlHandle, CURLOPT_HTTPGET, 1L);
    for (const auto& h : headers) header_slist = curl_slist_append(header_slist, h.c_str());
    if (header_slist) curl_easy_setopt(m_curlHandle, CURLOPT_HTTPHEADER, header_slist);
    responseBody.clear();
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, writeCallback); // This is for string responseBody
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, &responseBody);
    if ((method == "POST" || method == "PUT") && inFileStream) {
        // ... (Upload specific read_callback_gcs setup) ...
         inFileStream->seekg(0, std::ios::end); curl_off_t fileSize = inFileStream->tellg(); inFileStream->seekg(0, std::ios::beg);
         if(method == "POST") curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDSIZE_LARGE, fileSize);
         else curl_easy_setopt(m_curlHandle, CURLOPT_INFILESIZE_LARGE, fileSize);
         curl_easy_setopt(m_curlHandle, CURLOPT_READFUNCTION, read_callback_gcs);
         curl_easy_setopt(m_curlHandle, CURLOPT_READDATA, inFileStream);
    } else if ((method == "POST" || method == "PUT") && !postData.empty()){
         curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDS, postData.c_str());
         if(method == "POST") curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDSIZE, (long)postData.length());
         else curl_easy_setopt(m_curlHandle, CURLOPT_INFILESIZE_LARGE, (curl_off_t)postData.length());
    }
    // SSL options, timeouts etc.
    curl_easy_setopt(m_curlHandle, CURLOPT_SSL_VERIFYPEER, 1L); curl_easy_setopt(m_curlHandle, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(m_curlHandle, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
    // ... (timeout logic based on inFileStream)
    res = curl_easy_perform(m_curlHandle);
    if (res == CURLE_OK) curl_easy_getinfo(m_curlHandle, CURLINFO_RESPONSE_CODE, &responseCode);
    else responseCode = 0;
    if (header_slist) curl_slist_free_all(header_slist);
    // Minimal reset
    curl_easy_setopt(m_curlHandle, CURLOPT_HTTPGET, 1L); curl_easy_setopt(m_curlHandle, CURLOPT_CUSTOMREQUEST, NULL);
    curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDS, NULL); curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDSIZE, (long)-1);
    curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 0L); curl_easy_setopt(m_curlHandle, CURLOPT_HTTPHEADER, NULL);
    curl_easy_setopt(m_curlHandle, CURLOPT_READFUNCTION, NULL); curl_easy_setopt(m_curlHandle, CURLOPT_READDATA, NULL);
    // Ensure write function is reset if it was changed for direct file download elsewhere
    // curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, NULL);
    // curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, NULL);
    return res;
}

static size_t read_callback_gcs(char *buffer, size_t size, size_t nitems, void *instream) {
    std::ifstream *fileStream = static_cast<std::ifstream*>(instream);
    if (!fileStream || !fileStream->is_open() || fileStream->eof()) return 0;
    fileStream->read(buffer, size * nitems);
    size_t bytesRead = fileStream->gcount();
    if (bytesRead == 0 && fileStream->eof()) {}
    else if (fileStream->fail() && !fileStream->eof()) { return CURL_READFUNC_ABORT; }
    return bytesRead;
}
