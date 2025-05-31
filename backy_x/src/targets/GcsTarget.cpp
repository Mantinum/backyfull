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

GcsTarget::GcsTarget(const std::map<std::string, std::string>& config)
    : m_curlHandle(nullptr),
      m_accessTokenExpiryTime(0),
      m_oauthCallbackServer(nullptr),
      m_oauthFlowCompletedSuccessfully(false),
      m_properlyConfigured(true) {

    m_credentialManager = std::unique_ptr<CredentialManager>(createPlatformCredentialManager());

    // Default OAuth parameters (should be configurable or constants)
    // IMPORTANT: These are placeholders. Real values must be obtained from Google Cloud Console.
    // These should be actual client IDs and secrets for the application.
    m_clientId = "YOUR_CLIENT_ID.apps.googleusercontent.com"; // Placeholder - MUST BE REPLACED
    m_clientSecret = "YOUR_CLIENT_SECRET"; // Placeholder - MUST BE REPLACED
    m_redirectUri = "http://127.0.0.1:8085"; // Example, ensure port is free

    if (m_clientId.rfind("YOUR_CLIENT_ID", 0) == 0 || m_clientSecret.rfind("YOUR_CLIENT_SECRET", 0) == 0) {
        std::cerr << "GcsTarget: CRITICAL - OAuth Client ID or Secret are placeholders. GCS operations will fail." << std::endl;
        // m_properlyConfigured could be set to false here if desired, but getAccessToken will fail anyway.
    }

    // Parse config map
    auto itBucket = config.find("gcs_bucket_name");
    if (itBucket != config.end() && !itBucket->second.empty()) {
        m_bucketName = itBucket->second;
    } else {
        std::cerr << "GcsTarget: Critical config 'gcs_bucket_name' missing." << std::endl;
        m_properlyConfigured = false;
    }

    auto itPrefix = config.find("gcs_object_prefix");
    if (itPrefix != config.end()) {
        m_objectPrefix = itPrefix->second;
        // Ensure prefix ends with / if not empty
        if (!m_objectPrefix.empty() && m_objectPrefix.back() != '/') {
            m_objectPrefix += '/';
        }
    }

    auto itAccount = config.find("gcs_account_identifier"); // e.g., user's email or a unique ID
    if (itAccount != config.end() && !itAccount->second.empty()) {
        m_accountIdentifier = itAccount->second;
    } else {
        std::cerr << "GcsTarget: Config 'gcs_account_identifier' missing. Needed for storing refresh token." << std::endl;
        // This might not make it "improperly configured" for all operations,
        // but auth will fail if we need to store/retrieve a token.
        // For now, let's consider it non-critical for basic instantiation if other parts are fine.
    }

    // Initialize libcurl easy handle
    m_curlHandle = curl_easy_init();
    if (!m_curlHandle) {
        std::cerr << "GcsTarget: curl_easy_init() failed." << std::endl;
        m_properlyConfigured = false;
    }

    std::cout << "GcsTarget created. Bucket: " << m_bucketName << ", Prefix: " << m_objectPrefix << ", AccountID: " << m_accountIdentifier << std::endl;
}

GcsTarget::~GcsTarget() {
    if (m_curlHandle) {
        curl_easy_cleanup(m_curlHandle);
    }
    stopLocalCallbackServer(); // Ensure server is stopped
    // curl_global_cleanup(); // If GcsTarget is the only user. Usually done once per app.
}

// --- IStorageTarget Interface ---
bool GcsTarget::beginSession() {
    if (!m_properlyConfigured) {
        std::cerr << "GcsTarget: Not properly configured. Cannot begin session." << std::endl;
        return false;
    }
    std::string token = getAccessToken(); // This will handle initial auth or refresh
    if (token.empty()) {
        std::cerr << "GcsTarget: Failed to obtain access token for beginSession." << std::endl;
        return false;
    }
    std::cout << "GcsTarget: Session begun, access token obtained." << std::endl;
    return true;
}

bool GcsTarget::sendFile(const std::string& localPath, const FileMetadata& remoteObjectName) {
    // TODO: Consider resumable uploads for large files for future enhancement.
    std::string accessToken;
    int attempt = 0;
    const int maxAttempts = 2; // Allow one retry if token expires mid-operation

    while(attempt < maxAttempts) {
        accessToken = getAccessToken();
        if (accessToken.empty()) {
            std::cerr << "GcsTarget: Failed to get access token for sendFile." << std::endl;
            return false;
        }

        std::string fullObjectName = m_objectPrefix + remoteObjectName;
    // QUrl::toPercentEncoding requires a QString. Exclude '/' from encoding.
    QString qFullObjectName = QString::fromStdString(fullObjectName);
    std::string encodedObjectName = QUrl::toPercentEncoding(qFullObjectName, "", "/").toStdString();

    std::string url = "https://storage.googleapis.com/upload/storage/v1/b/" + m_bucketName +
                      "/o?uploadType=media&name=" + encodedObjectName;

    std::ifstream inFile(localPath, std::ios::binary);
    if (!inFile.is_open()) {
        std::cerr << "GcsTarget: Failed to open local file for upload: " << localPath << std::endl;
        return false;
    }

    std::vector<std::string> headers = {
        "Authorization: Bearer " + token,
        "Content-Type: application/octet-stream" // Or derive based on file type if necessary
    };
    std::string responseBody;
    long responseCode = 0;

    std::cout << "GcsTarget: Uploading " << localPath << " to " << url << std::endl;

        CURLcode res = performCurlRequest(url, "POST", headers, "", &inFile, responseBody, responseCode);
        // inFile is closed within the loop for each attempt if opened

        if (res == CURLE_OK) {
            if (responseCode == 200) {
                std::cout << "GcsTarget: File uploaded successfully. Object: " << fullObjectName << std::endl;
                inFile.close();
                return true;
            } else if (responseCode == 401 && attempt < maxAttempts -1) { // Unauthorized, token might have just expired
                std::cerr << "GcsTarget: sendFile received 401, attempting to refresh token and retry. Attempt " << attempt + 1 << std::endl;
                m_currentAccessToken.clear(); // Force token refresh
                attempt++;
                inFile.seekg(0, std::ios::beg); // Reset file stream for retry
                continue;
            } else if (responseCode == 429) { // Too Many Requests
                 std::cerr << "GcsTarget: sendFile received 429 Too Many Requests. Retrying after delay..." << std::endl;
                 std::this_thread::sleep_for(std::chrono::seconds(2)); // Simple fixed delay
                 attempt++; // Count as a general attempt
                 inFile.seekg(0, std::ios::beg);
                 continue;
            } else if (responseCode >= 500 && responseCode <= 504) { // Server errors
                 std::cerr << "GcsTarget: sendFile received server error " << responseCode << ". Retrying with backoff..." << std::endl;
                 std::this_thread::sleep_for(std::chrono::seconds(1 << attempt)); // Exponential backoff (1s, 2s)
                 attempt++; // Count as a general attempt
                 inFile.seekg(0, std::ios::beg);
                 continue;
            } else {
                std::cerr << "GcsTarget: File upload failed. HTTP_code: " << responseCode << ". Error: " << parseGcsError(responseBody) << std::endl;
                inFile.close();
                return false;
            }
        } else { // Curl error
            std::cerr << "GcsTarget: File upload failed. CURL_code: " << curl_easy_strerror(res) << " (" << res << ")" << std::endl;
            inFile.close();
            return false;
        }
    } // end while loop
    inFile.close(); // Should be closed if loop finishes due to max attempts
    return false; // Failed after retries
}

std::vector<IStorageTarget::FileMetadata> GcsTarget::listFiles(const std::string& listPrefix) {
    std::vector<IStorageTarget::FileMetadata> files;
    std::string accessToken;
    int attempt = 0;
    const int maxAttempts = 2; // For token expiry
    int serverErrorRetries = 0;
    const int maxServerErrorRetries = 3;

    std::string currentFullPrefix = m_objectPrefix + listPrefix;
    QString qFullListPrefix = QString::fromStdString(currentFullPrefix);
    std::string encodedListPrefix = QUrl::toPercentEncoding(qFullListPrefix, "", "/").toStdString();

    std::string nextPageToken;

    do {
        QUrl gcsUrl("https://storage.googleapis.com/storage/v1/b/" + QString::fromStdString(m_bucketName) + "/o");
        QUrlQuery query;
        if (!encodedListPrefix.empty()) {
            query.addQueryItem("prefix", QString::fromStdString(encodedListPrefix));
        }
        if (!nextPageToken.empty()) {
            query.addQueryItem("pageToken", QString::fromStdString(nextPageToken));
        }
        // query.addQueryItem("delimiter", "/"); // If hierarchical listing is needed later
        gcsUrl.setQuery(query);

        std::string url = gcsUrl.toString(QUrl::FullyEncoded).toStdString();
        std::vector<std::string> headers = {"Authorization: Bearer " + token};
        std::string responseBody;
        long responseCode = 0;

        accessToken = getAccessToken();
        if (accessToken.empty()) {
            std::cerr << "GcsTarget: No access token available for listFiles." << std::endl;
            return files; // Return empty vector
        }

        // Construct URL and headers inside the loop for retries if token changes
        QUrl gcsUrl("https://storage.googleapis.com/storage/v1/b/" + QString::fromStdString(m_bucketName) + "/o");
        QUrlQuery query;
        if (!encodedListPrefix.empty()) {
            query.addQueryItem("prefix", QString::fromStdString(encodedListPrefix));
        }
        if (!nextPageToken.empty()) {
            query.addQueryItem("pageToken", QString::fromStdString(nextPageToken));
        }
        gcsUrl.setQuery(query);
        std::string url = gcsUrl.toString(QUrl::FullyEncoded).toStdString();
        std::vector<std::string> headers = {"Authorization: Bearer " + accessToken};
        std::string responseBodyStr; // Changed name to avoid conflict with outer scope
        long responseCode = 0;

        std::cout << "GcsTarget: Listing files with URL: " << url << std::endl;
        CURLcode res = performCurlRequest(url, "GET", headers, "", nullptr, responseBodyStr, responseCode);

        if (res == CURLE_OK) {
            if (responseCode == 200) {
                QJsonDocument jsonResponse = QJsonDocument::fromJson(QString::fromStdString(responseBodyStr).toUtf8());
        if (jsonResponse.isNull() || !jsonResponse.isObject()) {
                if (jsonResponse.isNull() || !jsonResponse.isObject()) {
                    std::cerr << "GcsTarget: Failed to parse JSON response from listFiles: " << parseGcsError(responseBodyStr) << std::endl;
                    return {}; // Return empty on error
                }
                QJsonObject jsonObj = jsonResponse.object();
                if (jsonObj.contains("items") && jsonObj["items"].isArray()) {
                    QJsonArray itemsArray = jsonObj["items"].toArray();
            for (const QJsonValue& itemValue : itemsArray) {
                if (itemValue.isObject()) {
                    QJsonObject itemObj = itemValue.toObject();
                        if (itemValue.isObject()) {
                            QJsonObject itemObj = itemValue.toObject();
                            if (itemObj.contains("name") && itemObj["name"].isString()) {
                                files.push_back(itemObj["name"].toString().toStdString());
                            }
                        }
                    }
                }
                if (jsonObj.contains("nextPageToken") && jsonObj["nextPageToken"].isString()) {
                    nextPageToken = jsonObj["nextPageToken"].toString().toStdString();
                } else {
                    nextPageToken.clear();
                }
                serverErrorRetries = 0; // Reset server error retries on success
            } else if (responseCode == 401 && attempt < maxAttempts -1) {
                std::cerr << "GcsTarget: listFiles received 401, attempting to refresh token and retry. Attempt " << attempt + 1 << std::endl;
                m_currentAccessToken.clear(); // Force token refresh
                attempt++;
                // files vector is already accumulating, nextPageToken will ensure we continue correctly
                continue;
            } else if (responseCode == 429 && serverErrorRetries < maxServerErrorRetries) {
                 std::cerr << "GcsTarget: listFiles received 429 Too Many Requests. Retrying after delay... (" << serverErrorRetries + 1 << ")" << std::endl;
                 std::this_thread::sleep_for(std::chrono::seconds(2 * (1 << serverErrorRetries))); // Backoff: 2s, 4s, 8s
                 serverErrorRetries++;
                 continue;
            } else if (responseCode >= 500 && responseCode <= 504 && serverErrorRetries < maxServerErrorRetries) {
                 std::cerr << "GcsTarget: listFiles received server error " << responseCode << ". Retrying with backoff... (" << serverErrorRetries + 1 << ")" << std::endl;
                 std::this_thread::sleep_for(std::chrono::seconds(1 << serverErrorRetries)); // Backoff: 1s, 2s, 4s
                 serverErrorRetries++;
                 continue;
            } else {
                std::cerr << "GcsTarget: listFiles request failed. HTTP_code: " << responseCode << ". Error: " << parseGcsError(responseBodyStr) << std::endl;
                return {}; // Return empty on unrecoverable error
            }
        } else { // Curl error
            std::cerr << "GcsTarget: listFiles request failed. CURL_code: " << curl_easy_strerror(res) << " (" << res << ")" << std::endl;
            return {}; // Return empty on curl error
        }
    } while (!nextPageToken.empty()); // Loop for pagination

    std::cout << "GcsTarget: Found " << files.size() << " files/objects with prefix '" << currentFullPrefix << "'." << std::endl;
    return files;
}

bool GcsTarget::deleteFile(const std::string& remoteObjectName) {
    std::string accessToken;
    int attempt = 0;
    const int maxAttempts = 2; // For token expiry
    int serverErrorRetries = 0;
    const int maxServerErrorRetries = 3;

    std::string fullObjectName = m_objectPrefix + remoteObjectName;
    QString qFullObjectName = QString::fromStdString(fullObjectName);
    std::string encodedObjectName = QUrl::toPercentEncoding(qFullObjectName, "", "/").toStdString();

    std::string url = "https://storage.googleapis.com/storage/v1/b/" + m_bucketName + "/o/" + encodedObjectName;

    std::vector<std::string> headers = {"Authorization: Bearer " + token};
    std::string responseBody;
    long responseCode = 0;

    std::cout << "GcsTarget: Deleting object " << url << std::endl;
    while(attempt < maxAttempts) {
        accessToken = getAccessToken();
        if (accessToken.empty()) {
            std::cerr << "GcsTarget: No access token available for deleteFile." << std::endl;
            return false;
        }

        QString qFullObjectName = QString::fromStdString(fullObjectName);
        std::string encodedObjectName = QUrl::toPercentEncoding(qFullObjectName, "", "/").toStdString();
        std::string url = "https://storage.googleapis.com/storage/v1/b/" + m_bucketName + "/o/" + encodedObjectName;
        std::vector<std::string> headers = {"Authorization: Bearer " + accessToken};
        std::string responseBody;
        long responseCode = 0;

        std::cout << "GcsTarget: Deleting object " << url << std::endl;
        CURLcode res = performCurlRequest(url, "DELETE", headers, "", nullptr, responseBody, responseCode);

        if (res == CURLE_OK) {
            if (responseCode == 204) { // Success
                std::cout << "GcsTarget: File deleted successfully: " << fullObjectName << std::endl;
                return true;
            } else if (responseCode == 404) { // Not found - consider success for delete
                std::cout << "GcsTarget: File to delete not found (considered success): " << fullObjectName << std::endl;
                return true;
            } else if (responseCode == 401 && attempt < maxAttempts -1) {
                std::cerr << "GcsTarget: deleteFile received 401, attempting to refresh token and retry. Attempt " << attempt + 1 << std::endl;
                m_currentAccessToken.clear(); // Force token refresh
                attempt++;
                continue;
            } else if (responseCode == 429 && serverErrorRetries < maxServerErrorRetries) {
                 std::cerr << "GcsTarget: deleteFile received 429 Too Many Requests. Retrying after delay... (" << serverErrorRetries + 1 << ")" << std::endl;
                 std::this_thread::sleep_for(std::chrono::seconds(2 * (1 << serverErrorRetries)));
                 serverErrorRetries++;
                 attempt++; // Also count as a general attempt
                 continue;
            } else if (responseCode >= 500 && responseCode <= 504 && serverErrorRetries < maxServerErrorRetries) {
                 std::cerr << "GcsTarget: deleteFile received server error " << responseCode << ". Retrying with backoff... (" << serverErrorRetries + 1 << ")" << std::endl;
                 std::this_thread::sleep_for(std::chrono::seconds(1 << serverErrorRetries));
                 serverErrorRetries++;
                 attempt++; // Also count as a general attempt
                 continue;
            } else {
                std::cerr << "GcsTarget: File deletion failed. HTTP_code: " << responseCode << ". Error: " << parseGcsError(responseBody) << std::endl;
                return false;
            }
        } else { // Curl error
            std::cerr << "GcsTarget: File deletion failed. CURL_code: " << curl_easy_strerror(res) << " (" << res << ")" << std::endl;
            return false;
        }
    } // end while
    return false; // Failed after retries
}

bool GcsTarget::endSession() {
    std::cout << "GcsTarget: Session ended." << std::endl;
    // Any cleanup specific to a session, if m_curlHandle is reused across sessions, reset some opts.
    return true;
}

// --- OAuth2 and GCS Interaction ---

std::string GcsTarget::getAccessToken() {
    if (m_clientId.rfind("YOUR_CLIENT_ID", 0) == 0 || m_clientSecret.rfind("YOUR_CLIENT_SECRET", 0) == 0) {
        std::cerr << "GcsTarget: CRITICAL - OAuth Client ID or Secret are placeholders. Cannot obtain access token." << std::endl;
        return "";
    }

    // Check if current token is valid
    auto now = std::chrono::steady_clock::now();
    // Using seconds directly from time_since_epoch for m_accessTokenExpiryTime for simplicity with long long
    if (!m_currentAccessToken.empty() &&
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() < m_accessTokenExpiryTime) {
        // std::cout << "GcsTarget: Using existing valid access token." << std::endl; // Too verbose for normal operation
        return m_currentAccessToken;
    }

    std::cout << "GcsTarget: Access token expired or missing. Attempting to refresh or re-authenticate." << std::endl;
    if (!refreshAccessToken()) {
        if (!m_accountIdentifier.empty()) {
            if (!performInitialOAuthFlow()) {
                std::cerr << "GcsTarget: Initial OAuth flow failed." << std::endl;
                return "";
            }
        } else {
            std::cerr << "GcsTarget: Account identifier is missing, cannot perform initial OAuth flow or store token." << std::endl;
            return "";
        }
    }
    return m_currentAccessToken;
}

bool GcsTarget::refreshAccessToken() {
    if (m_clientId.rfind("YOUR_CLIENT_ID", 0) == 0 || m_clientSecret.rfind("YOUR_CLIENT_SECRET", 0) == 0) {
         std::cerr << "GcsTarget: CRITICAL - OAuth Client ID or Secret are placeholders for refreshAccessToken." << std::endl;
        return false;
    }
    if (m_accountIdentifier.empty()) {
        std::cerr << "GcsTarget: Cannot refresh token, account identifier is missing." << std::endl;
        return false;
    }
    if (!m_credentialManager) {
        std::cerr << "GcsTarget: CredentialManager not available for refreshing token." << std::endl;
        return false;
    }

    auto refreshTokenOpt = m_credentialManager->retrieveGcsRefreshToken(QString::fromStdString(m_accountIdentifier));
    if (!refreshTokenOpt || refreshTokenOpt->isEmpty()) {
        std::cerr << "GcsTarget: No refresh token found for account: " << m_accountIdentifier << std::endl;
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
            std::cerr << "GcsTarget: Failed to parse JSON response from refresh token request: " << responseBody << std::endl;
            return false;
        }
        QJsonObject jsonObj = jsonResponse.object();
        if (jsonObj.contains("access_token") && jsonObj["access_token"].isString() &&
            jsonObj.contains("expires_in") && jsonObj["expires_in"].isDouble()) {
            m_currentAccessToken = jsonObj["access_token"].toString().toStdString();
            long expiresIn = jsonObj["expires_in"].toInt();
            // Store expiry as seconds since epoch
            m_accessTokenExpiryTime = std::chrono::duration_cast<std::chrono::seconds>(
                                         std::chrono::steady_clock::now().time_since_epoch()
                                      ).count() + expiresIn - 60; // 60s buffer
            std::cout << "GcsTarget: Access token refreshed successfully." << std::endl;
            return true;
        } else {
            std::cerr << "GcsTarget: JSON response from refresh token missing required fields: " << responseBody << std::endl;
            return false;
        }
    } else { // Request failed
        std::cerr << "GcsTarget: Refresh token request failed. CURL_code: " << curl_easy_strerror(res) << " (" << res << ")"
                  << ", HTTP_code: " << responseCode << std::endl;
        std::cerr << "Response: " << parseGcsError(responseBody) << std::endl;
        if (responseCode == 400 || responseCode == 401) { // e.g., "invalid_grant" if refresh token is revoked/expired
            std::cerr << "GcsTarget: Refresh token is invalid or revoked. Deleting stored token for " << m_accountIdentifier << std::endl;
            if (m_credentialManager) {
                m_credentialManager->deleteGcsRefreshToken(QString::fromStdString(m_accountIdentifier));
            }
        }
        return false;
    }
}

bool GcsTarget::performInitialOAuthFlow() {
    if (m_clientId.rfind("YOUR_CLIENT_ID", 0) == 0 || m_clientSecret.rfind("YOUR_CLIENT_SECRET", 0) == 0) {
         std::cerr << "GcsTarget: CRITICAL - OAuth Client ID or Secret are placeholders for performInitialOAuthFlow." << std::endl;
        return false;
    }
    std::cout << "GcsTarget: Starting initial OAuth2 flow for account: " << m_accountIdentifier << std::endl;
    if (m_accountIdentifier.empty()) {
         std::cerr << "GcsTarget: Account identifier is missing. Cannot proceed with OAuth flow to store token." << std::endl;
        return false;
    }

    m_oauthFlowCompletedSuccessfully = false;
    m_authorizationCode.clear();

    // Start local HTTP server to listen for the callback
    m_oauthCallbackServer = new QTcpServer();
    QObject::connect(m_oauthCallbackServer, &QTcpServer::newConnection, [&]() {
        QTcpSocket *clientSocket = m_oauthCallbackServer->nextPendingConnection();
        if (!clientSocket) return;

        QObject::connect(clientSocket, &QTcpSocket::readyRead, [this, clientSocket]() {
            QByteArray requestData = clientSocket->readAll();
            QString requestString(requestData);

            // Very basic HTTP request parsing to find the 'code' query parameter
            if (requestString.contains("GET /") && requestString.contains("code=")) {
                QUrl requestUrl("http://127.0.0.1" + requestString.section(" ", 1, 1)); // Construct a QUrl from GET path
                QUrlQuery query(requestUrl.query());
                if (query.hasQueryItem("code")) {
                    m_authorizationCode = query.queryItemValue("code").toStdString();
                    // It's better not to log the raw authorization code extensively.
                    // std::cout << "GcsTarget: Authorization code received: " << m_authorizationCode << std::endl;
                    std::cout << "GcsTarget: Authorization code received." << std::endl;
                    m_oauthFlowCompletedSuccessfully = true;

                    QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
                    response += "<html><body><h1>Authentication Successful!</h1><p>You can close this window and return to BackyFull.</p></body></html>";
                    clientSocket->write(response);
                    clientSocket->flush();
                    clientSocket->close(); // Will trigger disconnected if not already
                } else {
                     std::cerr << "GcsTarget: Callback received but 'code' parameter missing or invalid." << std::endl;
                }
            } else {
                 std::cerr << "GcsTarget: Malformed callback request received: " << requestString.left(200) << "..." << std::endl;
            }

            if (!m_oauthFlowCompletedSuccessfully) {
                QByteArray response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n\r\n";
                response += "<html><body><h1>Authentication Failed</h1><p>Could not retrieve authorization code from the request.</p></body></html>";
                if (clientSocket->isOpen()) {
                    clientSocket->write(response);
                    clientSocket->flush();
                    clientSocket->close();
                }
            }
            // clientSocket->deleteLater(); // Moved to disconnected slot
            if (QCoreApplication::instance()) QCoreApplication::instance()->quit(); // Quit event loop for this flow
        });
         QObject::connect(clientSocket, &QTcpSocket::disconnected, [this, clientSocket](){
            // std::cout << "GcsTarget: OAuth callback client socket disconnected." << std::endl;
            clientSocket->deleteLater();
            // Ensure server stops if it was meant to handle one request.
            // stopLocalCallbackServer(); // This might be too early if loop isn't quit yet.
                                      // The loop.exec() below handles this.
        });
    });

    QUrl parsedRedirectUri(QString::fromStdString(m_redirectUri));
    if (!m_oauthCallbackServer->listen(QHostAddress::LocalHost, parsedRedirectUri.port(8085))) {
        std::cerr << "GcsTarget: Failed to start local callback server on " << m_redirectUri << ". Error: " << m_oauthCallbackServer->errorString().toStdString() << std::endl;
        delete m_oauthCallbackServer;
        m_oauthCallbackServer = nullptr;
        return false;
    }
    std::cout << "GcsTarget: Local callback server listening on " << m_redirectUri << std::endl;

    // Construct Google OAuth URL
    QUrl authUrl("https://accounts.google.com/o/oauth2/v2/auth");
    QUrlQuery query;
    query.addQueryItem("client_id", QString::fromStdString(m_clientId));
    query.addQueryItem("redirect_uri", QString::fromStdString(m_redirectUri));
    query.addQueryItem("response_type", "code");
    query.addQueryItem("scope", "https://www.googleapis.com/auth/devstorage.read_write"); // GCS read/write
    query.addQueryItem("access_type", "offline"); // To get a refresh token
    query.addQueryItem("prompt", "consent"); // Force consent screen to ensure refresh token is issued
    authUrl.setQuery(query);

    std::cout << "GcsTarget: Opening browser for OAuth: " << authUrl.toString().toStdString() << std::endl;
    QDesktopServices::openUrl(authUrl);

    // Start a local event loop to keep the server running until callback is received or timeout
    // This is a simplified event loop. In a full Qt app, the main event loop would handle this.
    // For a library component, this might need to be rethought or rely on the main app's event loop.
    // For now, create a local one for this blocking operation.
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(120000); // 2 minutes timeout for user interaction

    std::cout << "GcsTarget: Waiting for OAuth callback..." << std::endl;
    loop.exec(); // Blocks until quit() is called or timeout

    stopLocalCallbackServer(); // Ensure server is stopped if loop exited by timeout

    if (timer.isActive()) { // Loop exited by quit() from callback
        timer.stop();
    } else { // Loop exited by timeout
        std::cerr << "GcsTarget: OAuth flow timed out." << std::endl;
        m_oauthFlowCompletedSuccessfully = false;
    }

    if (!m_oauthFlowCompletedSuccessfully || m_authorizationCode.empty()) {
        std::cerr << "GcsTarget: Failed to get authorization code from callback." << std::endl;
        return false;
    }

    // Exchange authorization code for tokens
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
            std::cerr << "GcsTarget: Failed to parse JSON response from token exchange: " << responseBody << std::endl;
            return false;
        }
        QJsonObject jsonObj = jsonResponse.object();

        if (jsonObj.contains("access_token") && jsonObj["access_token"].isString() &&
            jsonObj.contains("expires_in") && jsonObj["expires_in"].isDouble()) {

            m_currentAccessToken = jsonObj["access_token"].toString().toStdString();
            long expiresIn = jsonObj["expires_in"].toInt();
            m_accessTokenExpiryTime = std::chrono::duration_cast<std::chrono::seconds>(
                                         std::chrono::steady_clock::now().time_since_epoch()
                                      ).count() + expiresIn - 60; // 60s buffer

            if (jsonObj.contains("refresh_token") && jsonObj["refresh_token"].isString()) {
                std::string refreshToken = jsonObj["refresh_token"].toString().toStdString();
                if (m_credentialManager && !m_accountIdentifier.empty()) {
                    if (!m_credentialManager->storeGcsRefreshToken(QString::fromStdString(m_accountIdentifier), QString::fromStdString(refreshToken))) {
                        std::cerr << "GcsTarget: Failed to store refresh token for account: " << m_accountIdentifier << std::endl;
                    } else {
                        std::cout << "GcsTarget: Refresh token stored successfully for " << m_accountIdentifier << std::endl;
                    }
                }
            } else {
                std::cout << "GcsTarget: No refresh token in response (e.g. already granted, or scope change needed for new one)." << std::endl;
            }
            std::cout << "GcsTarget: Initial OAuth flow completed. Access token obtained." << std::endl;
            return true;
        } else {
            std::cerr << "GcsTarget: JSON response from token exchange missing required fields: " << responseBody << std::endl;
            return false;
        }
    } else {
        std::cerr << "GcsTarget: Token exchange request failed. CURL_code: " << curl_easy_strerror(res) << " (" << res << ")"
                  << ", HTTP_code: " << responseCode << std::endl;
        std::cerr << "Response: " << parseGcsError(responseBody) << std::endl;
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
        // Handle memory problem
        return 0;
    }
    return newLength;
}

CURLcode GcsTarget::performCurlRequest(const std::string& url, const std::string& method,
                                     const std::vector<std::string>& headers,
                                     const std::string& postData,
                                     std::ifstream* inFileStream, // For uploads
                                     std::string& responseBody, long& responseCode) {
    if (!m_curlHandle) return CURLE_FAILED_INIT;

    CURLcode res;
    struct curl_slist *header_slist = nullptr;

    // Set URL
    curl_easy_setopt(m_curlHandle, CURLOPT_URL, url.c_str());

    // Set method
    if (method == "POST") {
        curl_easy_setopt(m_curlHandle, CURLOPT_POST, 1L);
        if (!postData.empty()) {
            curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDS, postData.c_str());
            curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDSIZE, (long)postData.length());
        } else if (inFileStream) { // File upload
            inFileStream->seekg(0, std::ios::end);
            curl_off_t fileSize = inFileStream->tellg();
            inFileStream->seekg(0, std::ios::beg);
            curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDSIZE_LARGE, fileSize);
        }
    } else if (method == "PUT") { // Typically for resumable uploads, or if GCS API prefers PUT for simple
        curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 1L); // Using UPLOAD for PUT semantics with read callback
         if (inFileStream) {
            inFileStream->seekg(0, std::ios::end);
            curl_off_t fileSize = inFileStream->tellg();
            inFileStream->seekg(0, std::ios::beg);
            curl_easy_setopt(m_curlHandle, CURLOPT_INFILESIZE_LARGE, fileSize);
        } else if (!postData.empty()){ // PUT with data in string
             curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDS, postData.c_str());
             curl_easy_setopt(m_curlHandle, CURLOPT_INFILESIZE_LARGE, (curl_off_t)postData.length());
        }
    } else if (method == "DELETE") {
        curl_easy_setopt(m_curlHandle, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else { // GET is default
        curl_easy_setopt(m_curlHandle, CURLOPT_HTTPGET, 1L);
    }

    // Set headers
    for (const auto& header : headers) {
        header_slist = curl_slist_append(header_slist, header.c_str());
    }
    if (header_slist) {
        curl_easy_setopt(m_curlHandle, CURLOPT_HTTPHEADER, header_slist);
    }

    // Set write callback for response body
    responseBody.clear();
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, &responseBody);

    // Set read callback for file uploads (POST or PUT)
    if (method == "POST" || method == "PUT") { // Only set read callback for uploads
        if (inFileStream) {
            curl_easy_setopt(m_curlHandle, CURLOPT_READFUNCTION, read_callback_gcs);
            curl_easy_setopt(m_curlHandle, CURLOPT_READDATA, inFileStream);
        } else if (!postData.empty() && method == "PUT") { // PUT with data from string
            // For PUT from string, libcurl expects data via CURLOPT_POSTFIELDS and CURLOPT_INFILESIZE_LARGE.
            // No separate read callback needed if postData is used directly for PUT.
            // If CURLOPT_UPLOAD is 1L and CURLOPT_POSTFIELDS is set, libcurl might try to use POSTFIELDS as data.
            // However, for clarity, PUT with string data is usually done by setting CURLOPT_READFUNCTION
            // to read from a string buffer if that's the desired pattern.
            // The current setup for PUT with postData and UPLOAD=1L might be okay if INFILESIZE_LARGE is set.
            // Let's rely on the existing logic for PUT with postData for now.
             curl_easy_setopt(m_curlHandle, CURLOPT_READFUNCTION, NULL);
             curl_easy_setopt(m_curlHandle, CURLOPT_READDATA, NULL);
        } else {
            curl_easy_setopt(m_curlHandle, CURLOPT_READFUNCTION, NULL);
            curl_easy_setopt(m_curlHandle, CURLOPT_READDATA, NULL);
        }
    } else { // For GET, DELETE etc.
        curl_easy_setopt(m_curlHandle, CURLOPT_READFUNCTION, NULL);
        curl_easy_setopt(m_curlHandle, CURLOPT_READDATA, NULL);
    }

    // SSL options
    curl_easy_setopt(m_curlHandle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(m_curlHandle, CURLOPT_SSL_VERIFYHOST, 2L);
    // For production, consider providing a CA bundle or using system's default:
    // curl_easy_setopt(m_curlHandle, CURLOPT_CAINFO, "/path/to/ca-bundle.crt");

    // Timeout options
    curl_easy_setopt(m_curlHandle, CURLOPT_CONNECTTIMEOUT_MS, 10000L); // 10 seconds for connection
    if (inFileStream) { // Longer timeout for uploads
        curl_easy_setopt(m_curlHandle, CURLOPT_TIMEOUT_MS, 3600000L); // 1 hour for upload
        curl_easy_setopt(m_curlHandle, CURLOPT_LOW_SPEED_LIMIT, 10240L); // 10 KB/s
        curl_easy_setopt(m_curlHandle, CURLOPT_LOW_SPEED_TIME, 30L);    // for 30 seconds
    } else { // Shorter timeout for metadata operations or small data posts
        curl_easy_setopt(m_curlHandle, CURLOPT_TIMEOUT_MS, 60000L);    // 60 seconds
        curl_easy_setopt(m_curlHandle, CURLOPT_LOW_SPEED_LIMIT, 0L); // Disable low speed limit for non-uploads
        curl_easy_setopt(m_curlHandle, CURLOPT_LOW_SPEED_TIME, 0L);
    }

    // Perform the request
    res = curl_easy_perform(m_curlHandle);

    // Get response code
    if (res == CURLE_OK) {
        curl_easy_getinfo(m_curlHandle, CURLINFO_RESPONSE_CODE, &responseCode);
    } else {
        responseCode = 0; // Indicate curl error (e.g. timeout, connection refused)
        // Log detailed CURL error string here as it's a direct CURL failure
        std::cerr << "GcsTarget: CURL request failed: " << curl_easy_strerror(res) << " (" << res << ")" << std::endl;
    }

    // Cleanup
    if (header_slist) {
        curl_slist_free_all(header_slist);
    }
    // Reset options for next request (important for reusing handle)
    curl_easy_setopt(m_curlHandle, CURLOPT_HTTPGET, 1L); // Default to GET
    curl_easy_setopt(m_curlHandle, CURLOPT_CUSTOMREQUEST, NULL);
    curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDSIZE, (long)-1); // Reset for safety
    curl_easy_setopt(m_curlHandle, CURLOPT_UPLOAD, 0L); // Reset UPLOAD flag
    // READFUNCTION and READDATA are reset above if inFileStream is null, or set if it's provided.
    // No need to explicitly null them again here unless there's a path where they aren't reset.
    curl_easy_setopt(m_curlHandle, CURLOPT_HTTPHEADER, NULL); // Remove custom headers for next call

    return res;
}

// Static read_callback for GCS, similar to SftpTarget
static size_t read_callback_gcs(char *buffer, size_t size, size_t nitems, void *instream) {
    std::ifstream *fileStream = static_cast<std::ifstream*>(instream);
    fileStream->read(buffer, size * nitems);
    size_t bytesRead = fileStream->gcount();

    if (bytesRead == 0 && fileStream->eof()) {
        // EOF
    } else if (bytesRead < size * nitems && fileStream->fail() && !fileStream->eof()) {
        std::cerr << "GcsTarget::read_callback_gcs: Read error from input file." << std::endl;
        return CURL_READFUNC_ABORT; // Abort transfer on read error
    }
    return bytesRead;
}
