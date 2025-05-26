#include "util/CredentialManager.h"
#include <QDebug>
#include <QString>
#include <QByteArray> // For UTF-8 conversions

#if defined(__APPLE__)

#import <Security/Security.h>

// Helper to convert QString to a C-string for Keychain functions
static const char* qsToCString(const QString& str, QByteArray& byteArray) {
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
        QByteArray serviceBytes, usernameBytes, secretBytes;
        OSStatus status = SecKeychainAddGenericPassword(
            nullptr, // Default keychain
            static_cast<UInt32>(service.length()), // serviceNameLength
            qsToCString(service, serviceBytes),    // serviceName
            static_cast<UInt32>(username.length()),// accountNameLength
            qsToCString(username, usernameBytes),  // accountName
            static_cast<UInt32>(secret.toUtf8().length()), // passwordLength (use toUtf8 for byte length)
            secret.toUtf8().constData(),           // passwordData
            nullptr                                // itemRef (optional)
        );

        if (status == errSecSuccess) {
            qDebug() << "MacOSCredentialManager: Secret stored successfully for service" << service << "user" << username;
            return true;
        } else if (status == errSecDuplicateItem) {
            // Item already exists, try to update it by deleting and re-adding (simplest update)
            // A more robust update would use SecKeychainItemModifyAttributesAndData
            qDebug() << "MacOSCredentialManager: Secret already exists for service" << service << "user" << username << ". Attempting to update.";
            if (deleteSecret(service, username)) { // Delete existing
                // Re-populate QByteArrays as they might have gone out of scope or their data moved
                // if deleteSecret was called in a way that created new ones.
                // For safety, always re-convert before a new API call.
                QByteArray serviceBytesUpdate, usernameBytesUpdate, secretBytesUpdate;
                status = SecKeychainAddGenericPassword(
                    nullptr,
                    static_cast<UInt32>(service.length()), qsToCString(service, serviceBytesUpdate),
                    static_cast<UInt32>(username.length()), qsToCString(username, usernameBytesUpdate),
                    static_cast<UInt32>(secret.toUtf8().length()), secret.toUtf8().constData(), // secret.toUtf8() is safe here
                    nullptr);
                if (status == errSecSuccess) {
                    qDebug() << "MacOSCredentialManager: Secret updated successfully.";
                    return true;
                }
            }
            qWarning() << "MacOSCredentialManager: Failed to update secret. Status:" << status << GetMacOSStatusErrorString(status);
            return false;
        } else {
            qWarning() << "MacOSCredentialManager: Failed to store secret. Status:" << status << GetMacOSStatusErrorString(status);
            return false;
        }
    }

    std::optional<QString> retrieveSecret(const QString& service, const QString& username) override {
        QByteArray serviceBytes, usernameBytes;
        void *passwordData = nullptr;
        UInt32 passwordLength = 0;

        OSStatus status = SecKeychainFindGenericPassword(
            nullptr,                               // Default keychain
            static_cast<UInt32>(service.length()), // serviceNameLength
            qsToCString(service, serviceBytes),    // serviceName
            static_cast<UInt32>(username.length()),// accountNameLength
            qsToCString(username, usernameBytes),  // accountName
            &passwordLength,                       // passwordLength (out)
            &passwordData,                         // passwordData (out)
            nullptr                                // itemRef (out, optional)
        );

        if (status == errSecSuccess) {
            qDebug() << "MacOSCredentialManager: Secret retrieved successfully for service" << service << "user" << username;
            QString secret = QString::fromUtf8(static_cast<const char*>(passwordData), static_cast<int>(passwordLength));
            SecKeychainItemFreeContent(nullptr, passwordData); // Free data buffer
            return secret;
        } else if (status == errSecItemNotFound) {
            qDebug() << "MacOSCredentialManager: Secret not found for service" << service << "user" << username;
            return std::nullopt;
        } else {
            qWarning() << "MacOSCredentialManager: Failed to retrieve secret. Status:" << status << GetMacOSStatusErrorString(status);
            return std::nullopt;
        }
    }

    bool deleteSecret(const QString& service, const QString& username) override {
        QByteArray serviceBytes, usernameBytes;
        SecKeychainItemRef itemRef = nullptr;
        OSStatus status;

        // First, find the item to get its reference (needed for delete)
        status = SecKeychainFindGenericPassword(
            nullptr,
            static_cast<UInt32>(service.length()), qsToCString(service, serviceBytes),
            static_cast<UInt32>(username.length()), qsToCString(username, usernameBytes),
            nullptr, nullptr, // We don't need password data here
            &itemRef);

        if (status == errSecSuccess && itemRef != nullptr) {
            status = SecKeychainItemDelete(itemRef);
            CFRelease(itemRef); // Release the itemRef whether delete succeeded or not, as we obtained it.
            if (status == errSecSuccess) {
                qDebug() << "MacOSCredentialManager: Secret deleted successfully for service" << service << "user" << username;
                return true;
            } else {
                qWarning() << "MacOSCredentialManager: Failed to delete secret. Status:" << status << GetMacOSStatusErrorString(status);
                return false;
            }
        } else if (status == errSecItemNotFound) {
            qDebug() << "MacOSCredentialManager: Secret to delete not found for service" << service << "user" << username << "(considered success).";
            if (itemRef) CFRelease(itemRef); // Still release if somehow itemRef was set but status was errSecItemNotFound
            return true; // Standard behavior: deleting a non-existent item is often a success.
        } else {
            qWarning() << "MacOSCredentialManager: Failed to find secret for deletion. Status:" << status << GetMacOSStatusErrorString(status);
            if (itemRef) CFRelease(itemRef); // Ensure release on other errors too
            return false;
        }
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
            // QString::fromCFString is deprecated. Use QString::CFStringRefToString.
            QString errorString = QString::CFStringRefToString(errorStringRef);
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
