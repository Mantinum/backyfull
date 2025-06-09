#include "util/CredentialManager.h"
#include <QDebug>
#include <QString>
#include <QByteArray> // For string conversions

#if defined(__linux__) && !defined(ANDROID) // Ensure it's not Android Linux

// Qt's 'signals' keyword can conflict with identifiers in system headers like those from GLib (used by libsecret).
// Undefine 'signals' before including libsecret headers, as Qt headers (QString, QDebug, etc.) are already included.
#ifdef signals
#undef signals
#endif

#include <libsecret-1/libsecret/secret.h>
#include <vector> // For attribute list if needed for complex queries, not strictly for this schema

// Define the schema for storing secrets with libsecret
// This schema should be unique to your application to avoid conflicts.
static const SecretSchema BACKYFULL_SECRET_SCHEMA = {
    "org.backyfull.Password", // Unique name for your schema
    SECRET_SCHEMA_NONE,
    {
        // Attributes used to search for the secret
        { "service", SECRET_SCHEMA_ATTRIBUTE_STRING },
        { "username", SECRET_SCHEMA_ATTRIBUTE_STRING },
        // The schema must be NULL-terminated.
        { nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING }
    },
    0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr
};

// Helper to convert GError* to QString for logging
static QString gerrorToString(GError* error) {
    if (error && error->message) {
        return QString("GError domain %1, code %2: %3").arg(g_quark_to_string(error->domain)).arg(error->code).arg(error->message);
    }
    return QString("Unknown GError");
}


class LinuxCredentialManager : public CredentialManager {
public:
    LinuxCredentialManager() {
        qDebug() << "LinuxCredentialManager instantiated";
        // No specific initialization needed for libsecret usually, it's on-demand.
    }

    ~LinuxCredentialManager() override {
        qDebug() << "LinuxCredentialManager destroyed";
    }

    bool storeSecret(const QString& service, const QString& username, const QString& secret) override {
        QByteArray serviceUtf8 = service.toUtf8();
        QByteArray usernameUtf8 = username.toUtf8();
        QByteArray secretUtf8 = secret.toUtf8();
        GError *error = nullptr;

        // The "label" is a user-visible description of the secret.
        QString label = QString("BackyFull Secret for %1@%2").arg(username, service);
        QByteArray labelUtf8 = label.toUtf8();

        gboolean success = secret_password_store_sync(
            &BACKYFULL_SECRET_SCHEMA,
            nullptr, // Default keyring (or SECRET_COLLECTION_SESSION)
            labelUtf8.constData(),
            secretUtf8.constData(),
            nullptr, // GCancellable
            &error,
            // Variadic arguments for attributes, matching the schema
            "service", serviceUtf8.constData(),
            "username", usernameUtf8.constData(),
            nullptr // Terminator for attributes
        );

        if (success) {
            qDebug() << "LinuxCredentialManager: Secret stored successfully for service" << service << "user" << username;
            return true;
        } else {
            qWarning() << "LinuxCredentialManager: Failed to store secret for service" << service << "user" << username << ". Error:" << gerrorToString(error);
            if (error) g_error_free(error);
            return false;
        }
    }

    std::optional<QString> retrieveSecret(const QString& service, const QString& username) override {
        QByteArray serviceUtf8 = service.toUtf8();
        QByteArray usernameUtf8 = username.toUtf8();
        GError *error = nullptr;

        gchar *password_c = secret_password_lookup_sync(
            &BACKYFULL_SECRET_SCHEMA,
            nullptr, // Default keyring
            nullptr, // GCancellable
            &error,
            // Variadic arguments for attributes to match
            "service", serviceUtf8.constData(),
            "username", usernameUtf8.constData(),
            nullptr // Terminator for attributes
        );

        if (error) {
            qWarning() << "LinuxCredentialManager: Error retrieving secret for service" << service << "user" << username << ". Error:" << gerrorToString(error);
            g_error_free(error);
            return std::nullopt;
        }

        if (password_c) {
            qDebug() << "LinuxCredentialManager: Secret retrieved successfully for service" << service << "user" << username;
            QString secretValue = QString::fromUtf8(password_c); // Corrected variable name
            secret_password_free(password_c); // Free the C-string returned by libsecret
            return secretValue; // Corrected variable name
        } else {
            qDebug() << "LinuxCredentialManager: Secret not found for service" << service << "user" << username;
            return std::nullopt; // Not found, but not an error
        }
    }

    bool deleteSecret(const QString& service, const QString& username) override {
        QByteArray serviceUtf8 = service.toUtf8();
        QByteArray usernameUtf8 = username.toUtf8();
        GError *error = nullptr;

        gboolean success = secret_password_clear_sync(
            &BACKYFULL_SECRET_SCHEMA,
            nullptr, // Default keyring
            nullptr, // GCancellable
            &error,
            // Variadic arguments for attributes to match for deletion
            "service", serviceUtf8.constData(),
            "username", usernameUtf8.constData(),
            nullptr // Terminator for attributes
        );

        if (success) {
            qDebug() << "LinuxCredentialManager: Secret deleted (or did not exist) successfully for service" << service << "user" << username;
            return true;
        } else {
            // It's possible the error is because the item wasn't found, which some might consider success for delete.
            // However, libsecret's secret_password_clear_sync returns FALSE if no items matched.
            // So, we log it as a warning but could return true if "not found" is acceptable as "deleted".
            // For now, strict: only TRUE if libsecret says it succeeded (which means it found and cleared, or found nothing to clear but operation is ok).
            qWarning() << "LinuxCredentialManager: Failed to delete secret for service" << service << "user" << username << ". Error:" << gerrorToString(error);
            if (error) g_error_free(error);
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

#if defined(__linux__) && !defined(ANDROID) && defined(Q_OS_LINUX) // Double check with Q_OS_LINUX
CredentialManager* createPlatformCredentialManager() {
    return new LinuxCredentialManager();
}
#endif

#else 
// Stub for non-Linux to ensure file is valid if accidentally included
#endif // __linux__ && !ANDROID
