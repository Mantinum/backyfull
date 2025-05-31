#ifndef CREDENTIALMANAGER_H
#define CREDENTIALMANAGER_H

#include <QString> // For service, username, secret
#include <optional>  // For retrieveSecret return type

// Forward declaration of the factory function implemented in platform-specific files
class CredentialManager; // Forward declare the class itself for the function signature
CredentialManager* createPlatformCredentialManager();

class CredentialManager {
public:
    // Virtual destructor is important for base classes
    virtual ~CredentialManager() = default;

    // Stores a secret associated with a service and username.
    // Returns true on success, false on failure.
    virtual bool storeSecret(const QString& service, const QString& username, const QString& secret) = 0;

    // Retrieves a secret for a given service and username.
    // Returns the secret as a QString if found, or std::nullopt if not found or on error.
    virtual std::optional<QString> retrieveSecret(const QString& service, const QString& username) = 0;

    // Deletes a secret for a given service and username.
    // Returns true on success or if the secret didn't exist; false on failure to delete an existing secret.
    virtual bool deleteSecret(const QString& service, const QString& username) = 0;

    // GCS Refresh Token Management
    virtual bool storeGcsRefreshToken(const QString& accountIdentifier, const QString& refreshToken) = 0;
    virtual std::optional<QString> retrieveGcsRefreshToken(const QString& accountIdentifier) = 0;
    virtual bool deleteGcsRefreshToken(const QString& accountIdentifier) = 0;

    // Factory method to get the platform-specific instance
    // This is a common pattern, but implementation details might vary.
    // For now, this declaration serves as a placeholder for the concept.
    // The actual instantiation might happen elsewhere or via more specific factory functions.
    // static CredentialManager* getInstance(); // This might be tricky with different implementations.
                                            // Let's defer the static getInstance() for now.
                                            // The GUI or app logic will decide which concrete manager to new up.
};

#endif // CREDENTIALMANAGER_H
