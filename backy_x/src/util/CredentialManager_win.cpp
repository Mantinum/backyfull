#include "util/CredentialManager.h"
#include <QDebug>
#include <QString>
#include <vector> // For potential buffer management, though direct conversion is often fine

#if defined(_WIN32) || defined(_WIN64) // Or Q_OS_WIN

#include <windows.h>
#include <wincred.h> // For Credential Manager functions

// Helper to get error string from GetLastError()
static QString getWindowsErrorString(DWORD errorCode) {
    LPWSTR messageBuffer = nullptr;
    size_t size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&messageBuffer, 0, nullptr);
    
    QString message = QString("Windows Error Code %1").arg(errorCode);
    if (size > 0 && messageBuffer) {
        message = QString::fromWCharArray(messageBuffer, static_cast<int>(size)); // Ensure size is int for QString
        LocalFree(messageBuffer);
    }
    return message.trimmed(); // Trim trailing newlines often included
}


class WindowsCredentialManager : public CredentialManager {
public:
    WindowsCredentialManager() {
        qDebug() << "WindowsCredentialManager instantiated";
    }

    ~WindowsCredentialManager() override {
        qDebug() << "WindowsCredentialManager destroyed";
    }

    bool storeSecret(const QString& service, const QString& username, const QString& secret) override {
        QString targetName = service + "/" + username; 
        
        // Correctly convert QString to null-terminated wchar_t array
        std::vector<wchar_t> targetNameW(targetName.size() + 1);
        targetName.toWCharArray(targetNameW.data());
        targetNameW[targetName.size()] = L'\0'; // Ensure null termination

        // Correctly prepare the secret blob
        // QString::utf16() returns const ushort* which is compatible with const wchar_t* on Windows
        // The size must be in bytes, including the null terminator if the API expects it
        // For CredWrite, the blob does not require null termination internally, but size must be accurate.
        std::vector<BYTE> secretBlob(secret.length() * sizeof(wchar_t));
        if (secret.length() > 0) { // Avoid memcpy with zero size if secret is empty
             memcpy(secretBlob.data(), secret.utf16(), secretBlob.size());
        }


        CREDENTIALW cred = {0};
        cred.Type = CRED_TYPE_GENERIC;
        cred.TargetName = targetNameW.data();
        cred.CredentialBlobSize = static_cast<DWORD>(secretBlob.size());
        cred.CredentialBlob = secretBlob.empty() ? nullptr : secretBlob.data(); // Handle empty secret
        cred.Persist = CRED_PERSIST_LOCAL_MACHINE; 
        
        if (CredWriteW(&cred, 0)) {
            qDebug() << "WindowsCredentialManager: Secret stored successfully for target" << targetName;
            return true;
        } else {
            DWORD errorCode = GetLastError();
            if (errorCode == ERROR_ALREADY_EXISTS) {
                qDebug() << "WindowsCredentialManager: Secret already exists for target" << targetName << ". Attempting to update.";
                // For update, we must delete then add. CredWrite does not update if attributes are different.
                // We will use the simpler delete + add strategy.
                if (deleteSecret(service, username)) { 
                    // Re-populate secretBlob in case its lifetime was tied to a temporary (though not here)
                    // and ensure targetNameW is still valid.
                    if (CredWriteW(&cred, 0)) {
                         qDebug() << "WindowsCredentialManager: Secret updated successfully for target" << targetName;
                         return true;
                    }
                    errorCode = GetLastError(); 
                } else {
                    // If delete failed, we report the original "already exists" or a new delete error.
                    // For simplicity, let's report the error from the failed delete (which deleteSecret would log)
                    // or if deleteSecret returned true (not found), then this path shouldn't be hit.
                    // The deleteSecret logs errors, so we can just indicate update failed.
                     qWarning() << "WindowsCredentialManager: Failed to update secret (delete step failed) for target" << targetName;
                     return false; // Or report specific error from delete
                }
            }
            qWarning() << "WindowsCredentialManager: Failed to store/update secret for target" << targetName << "." << getWindowsErrorString(errorCode);
            return false;
        }
    }

    std::optional<QString> retrieveSecret(const QString& service, const QString& username) override {
        QString targetName = service + "/" + username;
        std::vector<wchar_t> targetNameW(targetName.size() + 1);
        targetName.toWCharArray(targetNameW.data());
        targetNameW[targetName.size()] = L'\0';

        PCREDENTIALW pcred = nullptr;
        if (CredReadW(targetNameW.data(), CRED_TYPE_GENERIC, 0, &pcred)) {
            qDebug() << "WindowsCredentialManager: Secret retrieved successfully for target" << targetName;
            QString secretValue;
            if (pcred->CredentialBlobSize > 0 && pcred->CredentialBlob != nullptr) {
                 secretValue = QString::fromWCharArray(reinterpret_cast<const wchar_t*>(pcred->CredentialBlob), pcred->CredentialBlobSize / sizeof(wchar_t));
            } else {
                // Handle empty secret if stored that way (CredentialBlobSize == 0)
                secretValue = QString(""); 
            }
            CredFree(pcred);
            return secretValue;
        } else {
            DWORD errorCode = GetLastError();
            if (errorCode == ERROR_NOT_FOUND) {
                qDebug() << "WindowsCredentialManager: Secret not found for target" << targetName;
            } else {
                qWarning() << "WindowsCredentialManager: Failed to retrieve secret for target" << targetName << "." << getWindowsErrorString(errorCode);
            }
            return std::nullopt;
        }
    }

    bool deleteSecret(const QString& service, const QString& username) override {
        QString targetName = service + "/" + username;
        std::vector<wchar_t> targetNameW(targetName.size() + 1);
        targetName.toWCharArray(targetNameW.data());
        targetNameW[targetName.size()] = L'\0';

        if (CredDeleteW(targetNameW.data(), CRED_TYPE_GENERIC, 0)) {
            qDebug() << "WindowsCredentialManager: Secret deleted successfully for target" << targetName;
            return true;
        } else {
            DWORD errorCode = GetLastError();
            if (errorCode == ERROR_NOT_FOUND) {
                qDebug() << "WindowsCredentialManager: Secret to delete not found for target" << targetName << "(considered success).";
                return true; 
            }
            qWarning() << "WindowsCredentialManager: Failed to delete secret for target" << targetName << "." << getWindowsErrorString(errorCode);
            return false;
        }
    }

    // GCS Refresh Token Management
    bool storeGcsRefreshToken(const QString& accountIdentifier, const QString& refreshToken) override {
        // Use a fixed service name for all GCS refresh tokens
        return storeSecret("BackyFull_GCS_RefreshToken", accountIdentifier, refreshToken);
    }

    std::optional<QString> retrieveGcsRefreshToken(const QString& accountIdentifier) override {
        return retrieveSecret("BackyFull_GCS_RefreshToken", accountIdentifier);
    }

    bool deleteGcsRefreshToken(const QString& accountIdentifier) override {
        return deleteSecret("BackyFull_GCS_RefreshToken", accountIdentifier);
    }
};

#if defined(Q_OS_WIN) 
CredentialManager* createPlatformCredentialManager() {
    return new WindowsCredentialManager();
}
#endif

#else 
// Stub for non-Windows
#endif // _WIN32 || _WIN64
