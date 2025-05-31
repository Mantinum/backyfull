#include "util/CredentialManager.h"
#include <QDebug>
#include <QString>
#include <QByteArray> // For UTF-8 conversions

#if defined(__APPLE__)

#import <Security/Security.h>

// Helper to convert QString to a C-string for Keychain functions
[[maybe_unused]] static const char* qsToCString(const QString& str, QByteArray& byteArray) {
    byteArray = str.toUtf8(); // Or toLocal8Bit() depending on expected encoding by Keychain
    return byteArray.constData();
}

class MacOSCredentialManager : public CredentialManager {
public:
    MacOSCredentialManager() {
        qDebug() << "MacOSCredentialManager instantiated";
    }

    ~MacOSCredentialManager() override {
        qDebug() << "MacOSCredentialManager destroyed";
    }

    bool storeSecret(const QString& service, const QString& username, const QString& secret) override {
        CFStringRef cfService = service.toCFString();
        CFStringRef cfUsername = username.toCFString();
        QByteArray secretUtf8 = secret.toUtf8();
        CFDataRef cfSecret = CFDataCreate(kCFAllocatorDefault, (const UInt8*)secretUtf8.constData(), secretUtf8.length());

        if (!cfService || !cfUsername || !cfSecret) {
            qWarning() << "MacOSCredentialManager: Failed to create CoreFoundation objects.";
            if (cfService) CFRelease(cfService);
            if (cfUsername) CFRelease(cfUsername);
            if (cfSecret) CFRelease(cfSecret);
            return false;
        }

        CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        if (!query) {
            qWarning() << "MacOSCredentialManager: Failed to create query dictionary.";
            CFRelease(cfService);
            CFRelease(cfUsername);
            CFRelease(cfSecret);
            return false;
        }

        CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(query, kSecAttrService, cfService);
        CFDictionarySetValue(query, kSecAttrAccount, cfUsername);
        CFDictionarySetValue(query, kSecValueData, cfSecret);

        OSStatus status = SecItemAdd(query, nullptr);

        if (status == errSecSuccess) {
            qDebug() << "MacOSCredentialManager: Secret stored successfully for service" << service << "user" << username;
            CFRelease(query);
            CFRelease(cfService);
            CFRelease(cfUsername);
            CFRelease(cfSecret);
            return true;
        } else if (status == errSecDuplicateItem) {
            qDebug() << "MacOSCredentialManager: Secret already exists for service" << service << "user" << username << ". Attempting to update.";
            
            // Create a query for deletion (without kSecValueData)
            CFMutableDictionaryRef deleteQuery = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            if (!deleteQuery) {
                qWarning() << "MacOSCredentialManager: Failed to create delete query dictionary.";
                CFRelease(query); // Original query
                CFRelease(cfService);
                CFRelease(cfUsername);
                CFRelease(cfSecret);
                return false;
            }
            CFDictionarySetValue(deleteQuery, kSecClass, kSecClassGenericPassword);
            CFDictionarySetValue(deleteQuery, kSecAttrService, cfService);
            CFDictionarySetValue(deleteQuery, kSecAttrAccount, cfUsername);

            OSStatus deleteStatus = SecItemDelete(deleteQuery);
            CFRelease(deleteQuery);

            if (deleteStatus == errSecSuccess || deleteStatus == errSecItemNotFound) {
                qDebug() << "MacOSCredentialManager: Existing item deleted or not found. Attempting to re-add.";
                // Attempt to add the item again
                status = SecItemAdd(query, nullptr); // query still holds all necessary data including kSecValueData
                if (status == errSecSuccess) {
                    qDebug() << "MacOSCredentialManager: Secret updated successfully.";
                    CFRelease(query);
                    CFRelease(cfService);
                    CFRelease(cfUsername);
                    CFRelease(cfSecret);
                    return true;
                }
            }
            qWarning() << "MacOSCredentialManager: Failed to update secret. Delete status:" << GetMacOSStatusErrorString(deleteStatus) << "Add status:" << GetMacOSStatusErrorString(status);
            CFRelease(query);
            CFRelease(cfService);
            CFRelease(cfUsername);
            CFRelease(cfSecret);
            return false;
        } else {
            qWarning() << "MacOSCredentialManager: Failed to store secret. Status:" << status << GetMacOSStatusErrorString(status);
            CFRelease(query);
            CFRelease(cfService);
            CFRelease(cfUsername);
            CFRelease(cfSecret);
            return false;
        }
    }

    std::optional<QString> retrieveSecret(const QString& service, const QString& username) override {
        CFStringRef cfService = service.toCFString();
        CFStringRef cfUsername = username.toCFString();

        if (!cfService || !cfUsername) {
            qWarning() << "MacOSCredentialManager: Failed to create CFStringRef for service or username.";
            if (cfService) CFRelease(cfService);
            if (cfUsername) CFRelease(cfUsername);
            return std::nullopt;
        }

        CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        if (!query) {
            qWarning() << "MacOSCredentialManager: Failed to create query dictionary for retrieveSecret.";
            CFRelease(cfService);
            CFRelease(cfUsername);
            return std::nullopt;
        }

        CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(query, kSecAttrService, cfService);
        CFDictionarySetValue(query, kSecAttrAccount, cfUsername);
        CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
        CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);

        CFTypeRef resultDataRef = nullptr;
        OSStatus status = SecItemCopyMatching(query, &resultDataRef);

        CFRelease(query);
        CFRelease(cfService);
        CFRelease(cfUsername);

        if (status == errSecSuccess) {
            if (resultDataRef != nullptr && CFGetTypeID(resultDataRef) == CFDataGetTypeID()) {
                CFDataRef cfData = (CFDataRef)resultDataRef;
                QString secret = QString::fromUtf8(
                    reinterpret_cast<const char*>(CFDataGetBytePtr(cfData)),
                    static_cast<int>(CFDataGetLength(cfData))
                );
                CFRelease(cfData); // resultDataRef is the same as cfData, so release it here
                qDebug() << "MacOSCredentialManager: Secret retrieved successfully for service" << service << "user" << username;
                return secret;
            } else {
                qWarning() << "MacOSCredentialManager: SecItemCopyMatching returned success but data is not CFDataRef or is null.";
                if (resultDataRef) CFRelease(resultDataRef); // Release if it's not null but not CFDataRef
                return std::nullopt;
            }
        } else if (status == errSecItemNotFound) {
            qDebug() << "MacOSCredentialManager: Secret not found for service" << service << "user" << username;
            // No data to release for errSecItemNotFound according to docs for SecItemCopyMatching
            return std::nullopt;
        } else {
            qWarning() << "MacOSCredentialManager: Failed to retrieve secret. Status:" << status << GetMacOSStatusErrorString(status);
            // No data to release on other errors according to docs for SecItemCopyMatching
            return std::nullopt;
        }
    }

    bool deleteSecret(const QString& service, const QString& username) override {
        CFStringRef cfService = service.toCFString();
        CFStringRef cfUsername = username.toCFString();

        if (!cfService || !cfUsername) {
            qWarning() << "MacOSCredentialManager: Failed to create CFStringRef for service or username in deleteSecret.";
            if (cfService) CFRelease(cfService);
            if (cfUsername) CFRelease(cfUsername);
            return false;
        }

        CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        if (!query) {
            qWarning() << "MacOSCredentialManager: Failed to create query dictionary for deleteSecret.";
            CFRelease(cfService);
            CFRelease(cfUsername);
            return false;
        }

        CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(query, kSecAttrService, cfService);
        CFDictionarySetValue(query, kSecAttrAccount, cfUsername);

        OSStatus status = SecItemDelete(query);

        CFRelease(query);
        CFRelease(cfService);
        CFRelease(cfUsername);

        if (status == errSecSuccess) {
            qDebug() << "MacOSCredentialManager: Secret deleted successfully for service" << service << "user" << username;
            return true;
        } else if (status == errSecItemNotFound) {
            qDebug() << "MacOSCredentialManager: Secret to delete not found for service" << service << "user" << username << "(considered success).";
            return true; // Deleting a non-existent item is often considered a success.
        } else {
            qWarning() << "MacOSCredentialManager: Failed to delete secret. Status:" << status << GetMacOSStatusErrorString(status);
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
    
private:
    // Helper to get error string from OSStatus (macOS specific)
    // This might be better in a more general utility file if used elsewhere.
    QString GetMacOSStatusErrorString(OSStatus macStatus) {
        // SecCopyErrorMessageString is available from macOS 10.10+
        // On older systems, this might not be available or might return nullptr.
        #if MAC_OS_X_VERSION_MIN_REQUIRED >= 101000
        CFStringRef errorStringRef = SecCopyErrorMessageString(macStatus, NULL);
        if (errorStringRef != NULL) {
            // Correct way to convert CFStringRef to QString
            QString errorString = QString::fromCFString(errorStringRef);
            CFRelease(errorStringRef);
            return errorString;
        }
        #endif
        return QString("OSStatus code %1").arg(macStatus);
    }

};


#ifdef Q_OS_MACOS
CredentialManager* createPlatformCredentialManager() {
    return new MacOSCredentialManager();
}
#endif

#else 
// Stub for non-macOS to ensure file is valid if accidentally included, though CMake should prevent this.
// This #else block is technically not needed as the top-level #if defined(__APPLE__) handles it.
// However, it doesn't hurt.
#endif // __APPLE__
