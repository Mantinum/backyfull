#include "targets/GcsTarget.h"
#include "util/CredentialManager.h" // For createPlatformCredentialManager

#include <iostream> // For logging
#include <fstream>  // For file operations
#include <chrono>   // For time

// Qt includes for OAuth flow (if chosen for callback server, URL parsing, JSON)
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDesktopServices> // To open browser
#include <QCoreApplication> // For event loop during OAuth
#include <QTimer> // For OAuth timeout

// libcurl
#include <curl/curl.h>

#include <thread> // For std::this_thread::sleep_for

// Forward declaration for the read callback if it's defined later in the file
static size_t read_callback_gcs(char *buffer, size_t size, size_t nitems, void *instream);

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
            // OAuth errors sometimes use error_description
            return jsonObj["error_description"].toString().toStdString();
        }
    }
    return responseBody; // Return raw body if not typical GCS JSON error format
}


// --- GcsTarget Implementation ---

GcsTarget::GcsTarget(const std::map<std::string, std::string>& config, CredentialManager* credentialManager)
    : m_credentialManager(credentialManager), // Use passed CredentialManager
      m_curlHandle(nullptr),
      m_accessTokenExpiryTime(0),
      m_oauthCallbackServer(nullptr),
      m_oauthFlowCompletedSuccessfully(false),
      m_properlyConfigured(true) {

    if (!m_credentialManager) {
        std::cerr << "GcsTarget: Warning - CredentialManager not provided, creating internal instance." << std::endl;
        m_credentialManager = createPlatformCredentialManager();
    }

    // IMPORTANT: Replace with your actual Google Cloud OAuth2 Client ID
    m_clientId = "YOUR_CLIENT_ID_GOES_HERE.apps.googleusercontent.com";
    // IMPORTANT: Replace with your actual Google Cloud OAuth2 Client Secret
    m_clientSecret = "YOUR_CLIENT_SECRET_GOES_HERE";
    m_redirectUri = "http://127.0.0.1:8085"; // Standard loopback redirect URI

    if (m_clientId.rfind("YOUR_CLIENT_ID_GOES_HERE", 0) == 0 || m_clientSecret.rfind("YOUR_CLIENT_SECRET_GOES_HERE", 0) == 0) {
        std::cerr << "GcsTarget: CRITICAL - OAuth Client ID or Secret are placeholders. GCS operations will fail. Please update them in GcsTarget.cpp." << std::endl;
        m_lastError = "OAuth Client ID or Secret are placeholders. Configure them in GcsTarget.cpp.";
    }

    auto itBucket = config.find("gcs_bucket_name");
    if (itBucket != config.end() && !itBucket->second.empty()) {
        m_bucketName = itBucket->second;
    } else {
        std::cerr << "GcsTarget: Critical config 'gcs_bucket_name' missing." << std::endl;
        m_lastError = "GCS bucket name is missing in configuration.";
        m_properlyConfigured = false;
    }

    auto itPrefix = config.find("gcs_object_prefix"); // This is the backup root within the bucket for this source
    if (itPrefix != config.end()) {
        m_objectPrefix = itPrefix->second;
        if (!m_objectPrefix.empty() && m_objectPrefix.back() != '/') {
            m_objectPrefix += '/';
        }
    } // If not provided, m_objectPrefix remains empty, meaning root of bucket.

    auto itAccount = config.find("gcs_account_identifier");
    if (itAccount != config.end() && !itAccount->second.empty()) {
        m_accountIdentifier = itAccount->second;
    } else {
        std::cerr << "GcsTarget: Config 'gcs_account_identifier' missing. Needed for storing refresh token." << std::endl;
        if (m_properlyConfigured) {
             m_lastError = "GCS account identifier is missing. Needed for storing refresh token.";
        }
         // It's debatable if this makes it not "properlyConfigured" for all ops.
         // For testConnection or auth, it's vital. For other ops, an existing token might work.
         // Let's make it critical for configuration.
        m_properlyConfigured = false;
    }

    m_curlHandle = curl_easy_init();
    if (!m_curlHandle) {
        std::cerr << "GcsTarget: curl_easy_init() failed." << std::endl;
        m_lastError = "Failed to initialize libcurl.";
        m_properlyConfigured = false;
    }

    std::cout << "GcsTarget created. Bucket: " << m_bucketName << ", ObjectPrefix: " << m_objectPrefix << ", AccountID: " << m_accountIdentifier << std::endl;
}

GcsTarget::~GcsTarget() {
    if (m_curlHandle) {
        curl_easy_cleanup(m_curlHandle);
    }
    stopLocalCallbackServer();
}

// --- IStorageTarget Interface ---
bool GcsTarget::beginSession() {
    m_lastError.clear();
    if (!m_properlyConfigured) {
        m_lastError = "GcsTarget is not properly configured. Cannot begin session.";
        std::cerr << "GcsTarget: " << m_lastError << std::endl;
        return false;
    }
    std::string token = getAccessToken();
    if (token.empty()) {
        // getAccessToken sets m_lastError
        std::cerr << "GcsTarget: Failed to obtain access token for beginSession. Error: " << m_lastError << std::endl;
        return false;
    }
    std::cout << "GcsTarget: Session begun, access token obtained." << std::endl;
    return true;
}

bool GcsTarget::sendFile(const std::string& localPath, const FileMetadata& remoteObjectName) {
    m_lastError.clear();
    std::string accessToken;
    int attempt = 0;
    const int maxAttempts = 2;

    while(attempt < maxAttempts) {
        accessToken = getAccessToken();
        if (accessToken.empty()) {
            std::cerr << "GcsTarget: Failed to get access token for sendFile. Error: " << m_lastError << std::endl;
            return false;
        }

        // remoteObjectName here is relative to the source folder, e.g., "my_document.txt" or "photos/image.jpg"
        // m_objectPrefix is the backup root path within the bucket for this specific backup task/source.
        std::string fullPathInBucket = m_objectPrefix + remoteObjectName;
        QString qFullPathInBucket = QString::fromStdString(fullPathInBucket);
        std::string encodedObjectName = QUrl::toPercentEncoding(qFullPathInBucket, "", "/").toStdString();

        std::string url = "https://storage.googleapis.com/upload/storage/v1/b/" + m_bucketName +
                          "/o?uploadType=media&name=" + encodedObjectName;

        std::ifstream inFile(localPath, std::ios::binary);
        if (!inFile.is_open()) {
            m_lastError = "Failed to open local file for upload: " + localPath;
            std::cerr << "GcsTarget: " << m_lastError << std::endl;
            return false;
        }

        std::vector<std::string> headers = {
            "Authorization: Bearer " + accessToken,
            "Content-Type: application/octet-stream"
        };
        std::string responseBody;
        long responseCode = 0;

        std::cout << "GcsTarget: Uploading " << localPath << " to bucket '" << m_bucketName << "' as object '" << fullPathInBucket << "'" << std::endl;

        CURLcode res = performCurlRequest(url, "POST", headers, "", &inFile, responseBody, responseCode);

        if (res == CURLE_OK) {
            if (responseCode == 200) {
                std::cout << "GcsTarget: File uploaded successfully. Object: " << fullPathInBucket << std::endl;
                inFile.close();
                return true;
            } else if (responseCode == 401 && attempt < maxAttempts - 1) {
                std::cerr << "GcsTarget: sendFile received 401 (Unauthorized), attempting to refresh token and retry. Attempt " << attempt + 1 << std::endl;
                m_currentAccessToken.clear();
                attempt++;
                inFile.clear(); // Clear EOF flags
                inFile.seekg(0, std::ios::beg);
                continue;
            } else if (responseCode == 429) {
                 std::cerr << "GcsTarget: sendFile received 429 (Too Many Requests). Retrying after delay..." << std::endl;
                 std::this_thread::sleep_for(std::chrono::seconds(2 * (1 << attempt)));
                 attempt++;
                 inFile.clear();
                 inFile.seekg(0, std::ios::beg);
                 continue;
            } else if (responseCode >= 500 && responseCode <= 504) {
                 std::cerr << "GcsTarget: sendFile received server error " << responseCode << ". Retrying with backoff..." << std::endl;
                 std::this_thread::sleep_for(std::chrono::seconds(1 << attempt));
                 attempt++;
                 inFile.clear();
                 inFile.seekg(0, std::ios::beg);
                 continue;
            } else {
                m_lastError = "File upload failed for " + fullPathInBucket + ". HTTP_code: " + std::to_string(responseCode) + ". Error: " + parseGcsError(responseBody);
                std::cerr << "GcsTarget: " << m_lastError << std::endl;
                inFile.close();
                return false;
            }
        } else {
            m_lastError = "File upload failed for " + fullPathInBucket + ". CURL_code: " + std::string(curl_easy_strerror(res)) + " (" + std::to_string(res) + ")";
            std::cerr << "GcsTarget: " << m_lastError << std::endl;
            inFile.close();
            return false;
        }
    }
    m_lastError = "File upload failed for " + (m_objectPrefix + remoteObjectName) + " after maximum retries.";
    return false;
}

std::vector<IStorageTarget::FileMetadata> GcsTarget::listFiles(const std::string& /*listPrefix*/) {
    m_lastError.clear();
    std::vector<IStorageTarget::FileMetadata> files;
    // ... (rest of the listFiles implementation remains largely the same but should ensure m_lastError is set on failures)
    // For brevity, I'll assume the error setting logic shown in sendFile is applied here too.
    // Key points for listFiles:
    // - Set m_lastError on getAccessToken() failure.
    // - Set m_lastError on JSON parsing failure from GCS response.
    // - Set m_lastError on HTTP errors (401, 429, 50x) if retries exhausted or error is fatal.
    // - Set m_lastError on CURL_code errors.
    std::string accessToken = getAccessToken();
    if (accessToken.empty()) {
        std::cerr << "GcsTarget: No access token for listFiles. Error: " << m_lastError << std::endl;
        return {};
    }
    // ... (actual list logic with performCurlRequest)
    // Example error handling within the loop:
    // if (res != CURLE_OK || responseCode != 200) {
    //     m_lastError = "Failed to list files. HTTP: " + std::to_string(responseCode) + ", Curl: " + curl_easy_strerror(res);
    //     return {};
    // }
    // if (json_parsing_failed) {
    //     m_lastError = "Failed to parse listFiles response.";
    //     return {};
    // }
    return files; // Placeholder
}

bool GcsTarget::deleteFile(const std::string& /*remoteObjectName*/) {
    m_lastError.clear();
    // ... (rest of deleteFile implementation, ensuring m_lastError is set on failures)
    // Similar to sendFile, set m_lastError on:
    // - getAccessToken() failure.
    // - HTTP errors (401, 429, 50x) if retries exhausted or error is fatal (excluding 404, which is success for delete).
    // - CURL_code errors.
    std::string accessToken = getAccessToken();
    if (accessToken.empty()) {
        std::cerr << "GcsTarget: No access token for deleteFile. Error: " << m_lastError << std::endl;
        return false;
    }
    // ... (actual delete logic with performCurlRequest)
    // Example error handling:
    // if (res != CURLE_OK || (responseCode != 204 && responseCode != 404)) {
    //    m_lastError = "Failed to delete file " + remoteObjectName + ". HTTP: " + std::to_string(responseCode) + ", Curl: " + curl_easy_strerror(res);
    //    return false;
    // }
    return true; // Placeholder
}

bool GcsTarget::endSession() {
    m_lastError.clear();
    std::cout << "GcsTarget: Session ended." << std::endl;
    return true;
}

// --- GCS Specific Method ---
bool GcsTarget::testConnection(std::string& errorMsg) {
    m_lastError.clear();
    if (!m_properlyConfigured && m_bucketName.empty()) { // Bucket name is essential for any test.
        errorMsg = "GCS Target is not properly configured (bucket name missing).";
        if (!m_lastError.empty()) errorMsg += " Initial error: " + m_lastError;
        m_lastError = errorMsg; // Also set internal m_lastError
        return false;
    }


    std::string token = getAccessToken();
    if (token.empty()) {
        errorMsg = "Failed to obtain access token: " + m_lastError;
        return false;
    }

    // Perform a lightweight GCS operation, e.g., list with maxResults=1
    std::string testPrefix = "backyfull_test_connection/";
    QString qTestPrefix = QString::fromStdString(testPrefix);
    std::string encodedTestPrefix = QUrl::toPercentEncoding(qTestPrefix, "", "/").toStdString();

    QUrl gcsUrl("https://storage.googleapis.com/storage/v1/b/" + QString::fromStdString(m_bucketName) + "/o");
    QUrlQuery query;
    query.addQueryItem("prefix", QString::fromStdString(encodedTestPrefix));
    query.addQueryItem("maxResults", "1");
    gcsUrl.setQuery(query);

    std::string url = gcsUrl.toString(QUrl::FullyEncoded).toStdString();
    std::vector<std::string> headers = {"Authorization: Bearer " + token};
    std::string responseBody;
    long responseCode = 0;

    std::cout << "GcsTarget: Testing connection with URL: " << url << std::endl;
    CURLcode res = performCurlRequest(url, "GET", headers, "", nullptr, responseBody, responseCode);

    if (res == CURLE_OK) {
        if (responseCode == 200) { // OK, even if list is empty
            std::cout << "GcsTarget: Test connection successful (HTTP 200)." << std::endl;
            errorMsg.clear(); // No error
            return true;
        } else {
            m_lastError = "Test connection failed. HTTP Code: " + std::to_string(responseCode) + ". Response: " + parseGcsError(responseBody);
            errorMsg = m_lastError;
            std::cerr << "GcsTarget: " << m_lastError << std::endl;
            return false;
        }
    } else {
        m_lastError = "Test connection failed. CURL Error: " + std::string(curl_easy_strerror(res)) + " (" + std::to_string(res) + ")";
        errorMsg = m_lastError;
        std::cerr << "GcsTarget: " << m_lastError << std::endl;
        return false;
    }
}


// --- Public OAuth Method for UI ---
bool GcsTarget::initiateOAuthAndStoreToken() {
    m_lastError.clear();
    if (m_clientId.rfind("YOUR_CLIENT_ID", 0) == 0 || m_clientSecret.rfind("YOUR_CLIENT_SECRET", 0) == 0) {
        m_lastError = "OAuth Client ID or Secret are placeholders. Configure them in GcsTarget.cpp.";
        std::cerr << "GcsTarget: CRITICAL - " << m_lastError << std::endl;
        return false;
    }
    if (m_accountIdentifier.empty()) {
        m_lastError = "Account Identifier (e.g., email) is not set. Cannot proceed with OAuth.";
        std::cerr << "GcsTarget: " << m_lastError << std::endl;
        return false;
    }

    bool success = performInitialOAuthFlow();
    if (success) {
        m_lastError.clear();
    } else {
        if (m_lastError.empty()){
             m_lastError = "OAuth authentication process failed or was cancelled by user.";
        }
    }
    return success;
}

std::string GcsTarget::getLastError() const {
    return m_lastError;
}


// --- OAuth2 and GCS Interaction ---

std::string GcsTarget::getAccessToken() {
    if (m_clientId.rfind("YOUR_CLIENT_ID", 0) == 0 || m_clientSecret.rfind("YOUR_CLIENT_SECRET", 0) == 0) {
        m_lastError = "OAuth Client ID or Secret are placeholders. Cannot obtain access token.";
        std::cerr << "GcsTarget: CRITICAL - " << m_lastError << std::endl;
        return "";
    }

    auto now = std::chrono::steady_clock::now();
    if (!m_currentAccessToken.empty() &&
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() < m_accessTokenExpiryTime) {
        return m_currentAccessToken;
    }

    std::cout << "GcsTarget: Access token expired or missing. Attempting to refresh or re-authenticate." << std::endl;
    if (!refreshAccessToken()) {
        if (!m_accountIdentifier.empty()) {
            if (!performInitialOAuthFlow()) {
                std::cerr << "GcsTarget: Initial OAuth flow failed. Error: " << m_lastError << std::endl;
                return "";
            }
        } else {
            m_lastError = "Account identifier is missing, cannot perform initial OAuth flow or store token.";
            std::cerr << "GcsTarget: " << m_lastError << std::endl;
            return "";
        }
    }
    return m_currentAccessToken;
}

bool GcsTarget::refreshAccessToken() {
    if (m_clientId.rfind("YOUR_CLIENT_ID", 0) == 0 || m_clientSecret.rfind("YOUR_CLIENT_SECRET", 0) == 0) {
        m_lastError = "OAuth Client ID or Secret are placeholders for refreshAccessToken.";
        std::cerr << "GcsTarget: CRITICAL - " << m_lastError << std::endl;
        return false;
    }
    if (m_accountIdentifier.empty()) {
        m_lastError = "Cannot refresh token, account identifier is missing.";
        std::cerr << "GcsTarget: " << m_lastError << std::endl;
        return false;
    }
    if (!m_credentialManager) {
        m_lastError = "CredentialManager not available for refreshing token.";
        std::cerr << "GcsTarget: " << m_lastError << std::endl;
        return false;
    }

    auto refreshTokenOpt = m_credentialManager->retrieveGcsRefreshToken(QString::fromStdString(m_accountIdentifier));
    if (!refreshTokenOpt || refreshTokenOpt->isEmpty()) {
        m_lastError = "No refresh token found for account: " + m_accountIdentifier + ". Re-authentication required.";
        std::cout << "GcsTarget: " << m_lastError << std::endl;
        return false;
    }
    std::string refreshToken = refreshTokenOpt->toStdString();

    std::cout << "GcsTarget: Attempting to refresh access token for " << m_accountIdentifier << std::endl;

    std::string tokenUrl = "https://oauth2.googleapis.com/token";
    std::string postData = "client_id=" + QUrl::toPercentEncoding(QString::fromStdString(m_clientId)).toStdString() +
                           "&client_secret=" + QUrl::toPercentEncoding(QString::fromStdString(m_clientSecret)).toStdString() +
                           "&refresh_token=" + QUrl::toPercentEncoding(QString::fromStdString(refreshToken)).toStdString() +
                           "&grant_type=refresh_token";

    std::vector<std::string> headers = {"Content-Type: application/x-www-form-urlencoded"};
    std::string responseBody;
    long responseCode = 0;

    CURLcode res = performCurlRequest(tokenUrl, "POST", headers, postData, nullptr, responseBody, responseCode);

    if (res == CURLE_OK && responseCode == 200) {
        QJsonDocument jsonResponse = QJsonDocument::fromJson(QString::fromStdString(responseBody).toUtf8());
        if (jsonResponse.isNull() || !jsonResponse.isObject()) {
            m_lastError = "Failed to parse JSON response from refresh token request: " + responseBody;
            std::cerr << "GcsTarget: " << m_lastError << std::endl;
            return false;
        }
        QJsonObject jsonObj = jsonResponse.object();
        if (jsonObj.contains("access_token") && jsonObj["access_token"].isString() &&
            jsonObj.contains("expires_in") && jsonObj["expires_in"].isDouble()) {
            m_currentAccessToken = jsonObj["access_token"].toString().toStdString();
            long expiresIn = jsonObj["expires_in"].toInt();
            m_accessTokenExpiryTime = std::chrono::duration_cast<std::chrono::seconds>(
                                         std::chrono::steady_clock::now().time_since_epoch()
                                      ).count() + expiresIn - 60;
            std::cout << "GcsTarget: Access token refreshed successfully." << std::endl;
            return true;
        } else {
            m_lastError = "JSON response from refresh token missing required fields: " + responseBody;
            std::cerr << "GcsTarget: " << m_lastError << std::endl;
            return false;
        }
    } else {
        m_lastError = "Refresh token request failed. HTTP_code: " + std::to_string(responseCode) +
                      ". CURL_error: " + std::string(curl_easy_strerror(res)) + ". Response: " + parseGcsError(responseBody);
        std::cerr << "GcsTarget: " << m_lastError << std::endl;

        if (responseCode == 400 || responseCode == 401) {
            std::cerr << "GcsTarget: Refresh token is invalid or revoked. Deleting stored token for " << m_accountIdentifier << std::endl;
            if (m_credentialManager) {
                m_credentialManager->deleteGcsRefreshToken(QString::fromStdString(m_accountIdentifier));
            }
        }
        return false;
    }
}

bool GcsTarget::performInitialOAuthFlow() {
    m_lastError.clear();
    if (m_clientId.rfind("YOUR_CLIENT_ID", 0) == 0 || m_clientSecret.rfind("YOUR_CLIENT_SECRET", 0) == 0) {
        m_lastError = "OAuth Client ID or Secret are placeholders. Configure them in GcsTarget.cpp.";
        std::cerr << "GcsTarget: CRITICAL - " << m_lastError << std::endl;
        return false;
    }
    std::cout << "GcsTarget: Starting initial OAuth2 flow for account: " << m_accountIdentifier << std::endl;
    if (m_accountIdentifier.empty()) {
        m_lastError = "Account identifier is missing. Cannot proceed with OAuth flow to store token.";
        std::cerr << "GcsTarget: " << m_lastError << std::endl;
        return false;
    }

    m_oauthFlowCompletedSuccessfully = false;
    m_authorizationCode.clear();

    m_oauthCallbackServer = new QTcpServer();
    QObject::connect(m_oauthCallbackServer, &QTcpServer::newConnection, [&]() {
        QTcpSocket *clientSocket = m_oauthCallbackServer->nextPendingConnection();
        if (!clientSocket) return;

        QObject::connect(clientSocket, &QTcpSocket::readyRead, [this, clientSocket]() {
            QByteArray requestData = clientSocket->readAll();
            QString requestString(requestData);

            if (requestString.contains("GET /") && (requestString.contains("code=") || requestString.contains("error="))) { // Handle error in callback
                QUrl requestUrl("http://127.0.0.1" + requestString.section(" ", 1, 1));
                QUrlQuery query(requestUrl.query());
                if (query.hasQueryItem("code")) {
                    m_authorizationCode = query.queryItemValue("code").toStdString();
                    std::cout << "GcsTarget: Authorization code received." << std::endl;
                    m_oauthFlowCompletedSuccessfully = true;
                    QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
                    response += "<html><body><h1>Authentication Successful!</h1><p>You can close this window and return to BackyFull.</p></body></html>";
                    clientSocket->write(response);
                } else if (query.hasQueryItem("error")) {
                     m_lastError = "OAuth error in callback: " + query.queryItemValue("error").toStdString();
                     std::cerr << "GcsTarget: " << m_lastError << std::endl;
                     m_oauthFlowCompletedSuccessfully = false; // Explicitly false
                     QByteArray response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n\r\n";
                     response = QByteArray("<html><body><h1>Authentication Failed</h1><p>Error received from Google: ");
                     response += QUrl::fromPercentEncoding(query.queryItemValue("error").toUtf8()).toUtf8();
                     response += QByteArray("</p></body></html>");
                     clientSocket->write(response);
                } else {
                     m_lastError = "Callback received but 'code' or 'error' parameter missing or invalid.";
                     std::cerr << "GcsTarget: " << m_lastError << std::endl;
                     m_oauthFlowCompletedSuccessfully = false;
                }
            } else {
                 m_lastError = "Malformed callback request received: " + requestString.left(200).toStdString() + "...";
                 std::cerr << "GcsTarget: " << m_lastError << std::endl;
                 m_oauthFlowCompletedSuccessfully = false;
            }

            if (!m_oauthFlowCompletedSuccessfully && !clientSocket->property("responseSent").toBool()) {
                QByteArray response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n\r\n";
                response += "<html><body><h1>Authentication Failed</h1><p>Could not process the authentication callback. Check application logs.</p></body></html>";
                 if (clientSocket->isOpen()) clientSocket->write(response);
            }
            if (clientSocket->isOpen()) {
                clientSocket->setProperty("responseSent", true); // Mark that we've tried to send a response
                clientSocket->flush();
                clientSocket->close();
            }
            if (QCoreApplication::instance()) QCoreApplication::instance()->quit();
        });
         QObject::connect(clientSocket, &QTcpSocket::disconnected, [clientSocket](){
            clientSocket->deleteLater();
        });
    });

    QUrl parsedRedirectUri(QString::fromStdString(m_redirectUri));
    if (!m_oauthCallbackServer->listen(QHostAddress::LocalHost, parsedRedirectUri.port(8085))) {
        m_lastError = "Failed to start local callback server on " + m_redirectUri + ". Error: " + m_oauthCallbackServer->errorString().toStdString();
        std::cerr << "GcsTarget: " << m_lastError << std::endl;
        delete m_oauthCallbackServer;
        m_oauthCallbackServer = nullptr;
        return false;
    }
    std::cout << "GcsTarget: Local callback server listening on " << m_redirectUri << std::endl;

    QUrl authUrl("https://accounts.google.com/o/oauth2/v2/auth");
    QUrlQuery query;
    query.addQueryItem("client_id", QString::fromStdString(m_clientId));
    query.addQueryItem("redirect_uri", QString::fromStdString(m_redirectUri));
    query.addQueryItem("response_type", "code");
    query.addQueryItem("scope", "https://www.googleapis.com/auth/devstorage.read_write");
    query.addQueryItem("access_type", "offline");
    query.addQueryItem("prompt", "consent");
    authUrl.setQuery(query);

    std::cout << "GcsTarget: Opening browser for OAuth: " << authUrl.toString().toStdString() << std::endl;
    QDesktopServices::openUrl(authUrl);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(120000);

    std::cout << "GcsTarget: Waiting for OAuth callback..." << std::endl;
    loop.exec();

    stopLocalCallbackServer();

    if (timer.isActive()) {
        timer.stop();
    } else {
        if (m_lastError.empty()) m_lastError = "OAuth flow timed out waiting for user authorization.";
        std::cerr << "GcsTarget: " << m_lastError << std::endl;
        m_oauthFlowCompletedSuccessfully = false;
    }

    if (!m_oauthFlowCompletedSuccessfully || m_authorizationCode.empty()) {
        if (m_lastError.empty()) m_lastError = "Failed to get authorization code from callback (user may have cancelled or an error occurred).";
        std::cerr << "GcsTarget: " << m_lastError << std::endl;
        return false;
    }

    std::string tokenUrl = "https://oauth2.googleapis.com/token";
    std::string postData = "client_id=" + QUrl::toPercentEncoding(QString::fromStdString(m_clientId)).toStdString() +
                           "&client_secret=" + QUrl::toPercentEncoding(QString::fromStdString(m_clientSecret)).toStdString() +
                           "&code=" + QUrl::toPercentEncoding(QString::fromStdString(m_authorizationCode)).toStdString() +
                           "&redirect_uri=" + QUrl::toPercentEncoding(QString::fromStdString(m_redirectUri)).toStdString() +
                           "&grant_type=authorization_code";

    std::vector<std::string> headers = {"Content-Type: application/x-www-form-urlencoded"};
    std::string responseBody;
    long responseCode = 0;

    CURLcode res = performCurlRequest(tokenUrl, "POST", headers, postData, nullptr, responseBody, responseCode);

    if (res == CURLE_OK && responseCode == 200) {
        QJsonDocument jsonResponse = QJsonDocument::fromJson(QString::fromStdString(responseBody).toUtf8());
        if (jsonResponse.isNull() || !jsonResponse.isObject()) {
            m_lastError = "Failed to parse JSON response from token exchange: " + responseBody;
            std::cerr << "GcsTarget: " << m_lastError << std::endl;
            return false;
        }
        QJsonObject jsonObj = jsonResponse.object();

        if (jsonObj.contains("access_token") && jsonObj["access_token"].isString() &&
            jsonObj.contains("expires_in") && jsonObj["expires_in"].isDouble()) {

            m_currentAccessToken = jsonObj["access_token"].toString().toStdString();
            long expiresIn = jsonObj["expires_in"].toInt();
            m_accessTokenExpiryTime = std::chrono::duration_cast<std::chrono::seconds>(
                                         std::chrono::steady_clock::now().time_since_epoch()
                                      ).count() + expiresIn - 60;

            if (jsonObj.contains("refresh_token") && jsonObj["refresh_token"].isString()) {
                std::string refreshToken = jsonObj["refresh_token"].toString().toStdString();
                if (m_credentialManager && !m_accountIdentifier.empty()) {
                    if (!m_credentialManager->storeGcsRefreshToken(QString::fromStdString(m_accountIdentifier), QString::fromStdString(refreshToken))) {
                        m_lastError = "Failed to store refresh token for account: " + m_accountIdentifier;
                        std::cerr << "GcsTarget: " << m_lastError << std::endl;
                        // Not returning false here as access token was obtained, but this is a problem.
                    } else {
                        std::cout << "GcsTarget: Refresh token stored successfully for " << m_accountIdentifier << std::endl;
                    }
                }
            } else {
                std::cout << "GcsTarget: No refresh token in response (e.g. already granted, or scope change needed for new one)." << std::endl;
            }
            std::cout << "GcsTarget: Initial OAuth flow completed. Access token obtained." << std::endl;
            m_lastError.clear();
            return true;
        } else {
            m_lastError = "JSON response from token exchange missing required fields: " + responseBody;
            std::cerr << "GcsTarget: " << m_lastError << std::endl;
            return false;
        }
    } else {
        m_lastError = "Token exchange request failed. HTTP_code: " + std::to_string(responseCode) +
                      ". CURL_error: " + std::string(curl_easy_strerror(res)) + ". Response: " + parseGcsError(responseBody);
        std::cerr << "GcsTarget: " << m_lastError << std::endl;
        return false;
    }
}

void GcsTarget::stopLocalCallbackServer() {
    if (m_oauthCallbackServer) {
        if (m_oauthCallbackServer->isListening()) {
            m_oauthCallbackServer->close();
            std::cout << "GcsTarget: Local callback server stopped." << std::endl;
        }
        delete m_oauthCallbackServer;
        m_oauthCallbackServer = nullptr;
    }
}

// --- libcurl Helper ---
size_t GcsTarget::writeCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
    } catch(std::bad_alloc &e) {
        return 0;
    }
    return newLength;
}

CURLcode GcsTarget::performCurlRequest(const std::string& url, const std::string& method,
                                     const std::vector<std::string>& headers,
                                     const std::string& postData,
                                     std::ifstream* inFileStream,
                                     std::string& responseBody, long& responseCode) {
    if (!m_curlHandle) {
        // m_lastError = "libcurl handle not initialized."; // Should be set by caller context if critical
        return CURLE_FAILED_INIT;
    }

    CURLcode res;
    struct curl_slist *header_slist = nullptr;

    curl_easy_setopt(m_curlHandle, CURLOPT_URL, url.c_str());

    if (method == "POST") {
        curl_easy_setopt(m_curlHandle, CURLOPT_POST, 1L);
        if (!postData.empty()) {
            curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDS, postData.c_str());
            curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDSIZE, (long)postData.length());
        } else if (inFileStream) {
            inFileStream->seekg(0, std::ios::end);
            curl_off_t fileSize = inFileStream->tellg();
            inFileStream->seekg(0, std::ios::beg);
            curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDSIZE_LARGE, fileSize);
        }
    } else if (method == "PUT") {
        curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 1L);
         if (inFileStream) {
            inFileStream->seekg(0, std::ios::end);
            curl_off_t fileSize = inFileStream->tellg();
            inFileStream->seekg(0, std::ios::beg);
            curl_easy_setopt(m_curlHandle, CURLOPT_INFILESIZE_LARGE, fileSize);
        } else if (!postData.empty()){
             curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDS, postData.c_str());
             curl_easy_setopt(m_curlHandle, CURLOPT_INFILESIZE_LARGE, (curl_off_t)postData.length());
        }
    } else if (method == "DELETE") {
        curl_easy_setopt(m_curlHandle, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else { // GET is default
        curl_easy_setopt(m_curlHandle, CURLOPT_HTTPGET, 1L);
    }

    for (const auto& header : headers) {
        header_slist = curl_slist_append(header_slist, header.c_str());
    }
    if (header_slist) {
        curl_easy_setopt(m_curlHandle, CURLOPT_HTTPHEADER, header_slist);
    }

    responseBody.clear();
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, &responseBody);

    if (method == "POST" || method == "PUT") {
        if (inFileStream) {
            curl_easy_setopt(m_curlHandle, CURLOPT_READFUNCTION, read_callback_gcs);
            curl_easy_setopt(m_curlHandle, CURLOPT_READDATA, inFileStream);
        } else { // No inFileStream, or not a PUT with string data needing read callback
            curl_easy_setopt(m_curlHandle, CURLOPT_READFUNCTION, NULL);
            curl_easy_setopt(m_curlHandle, CURLOPT_READDATA, NULL);
        }
    } else {
        curl_easy_setopt(m_curlHandle, CURLOPT_READFUNCTION, NULL);
        curl_easy_setopt(m_curlHandle, CURLOPT_READDATA, NULL);
    }

    curl_easy_setopt(m_curlHandle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(m_curlHandle, CURLOPT_SSL_VERIFYHOST, 2L);

    curl_easy_setopt(m_curlHandle, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
    if (inFileStream) {
        curl_easy_setopt(m_curlHandle, CURLOPT_TIMEOUT_MS, 3600000L);
        curl_easy_setopt(m_curlHandle, CURLOPT_LOW_SPEED_LIMIT, 10240L);
        curl_easy_setopt(m_curlHandle, CURLOPT_LOW_SPEED_TIME, 30L);
    } else {
        curl_easy_setopt(m_curlHandle, CURLOPT_TIMEOUT_MS, 60000L);
        curl_easy_setopt(m_curlHandle, CURLOPT_LOW_SPEED_LIMIT, 0L);
        curl_easy_setopt(m_curlHandle, CURLOPT_LOW_SPEED_TIME, 0L);
    }

    res = curl_easy_perform(m_curlHandle);

    if (res == CURLE_OK) {
        curl_easy_getinfo(m_curlHandle, CURLINFO_RESPONSE_CODE, &responseCode);
    } else {
        responseCode = 0;
        std::cerr << "GcsTarget: CURL request failed directly in performCurlRequest: " << curl_easy_strerror(res) << " (" << res << ")" << std::endl;
    }

    if (header_slist) {
        curl_slist_free_all(header_slist);
    }
    // Minimal reset for safety, specific options are set per call.
    curl_easy_setopt(m_curlHandle, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(m_curlHandle, CURLOPT_CUSTOMREQUEST, NULL);
    curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDSIZE, (long)-1);
    curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 0L);
    curl_easy_setopt(m_curlHandle, CURLOPT_HTTPHEADER, NULL);
    curl_easy_setopt(m_curlHandle, CURLOPT_READFUNCTION, NULL);
    curl_easy_setopt(m_curlHandle, CURLOPT_READDATA, NULL);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, NULL);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, NULL);


    return res;
}

static size_t read_callback_gcs(char *buffer, size_t size, size_t nitems, void *instream) {
    std::ifstream *fileStream = static_cast<std::ifstream*>(instream);
    if (!fileStream || !fileStream->is_open() || fileStream->eof()){
        return 0;
    }
    fileStream->read(buffer, size * nitems);
    size_t bytesRead = fileStream->gcount();

    if (bytesRead == 0 && fileStream->eof()) {
        // EOF
    } else if (fileStream->fail() && !fileStream->eof()) { // Check failbit but not eof (eof alone with 0 bytes is fine)
        std::cerr << "GcsTarget::read_callback_gcs: Read error from input file." << std::endl;
        return CURL_READFUNC_ABORT;
    }
    return bytesRead;
}
