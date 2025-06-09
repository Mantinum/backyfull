#pragma once

#include <QtCore/QObject>
#include <QtNetworkAuth/qoauth2authorizationcodeflow.h>
#include "util/CredentialManager.h"

class GcsAuth : public QObject {
    Q_OBJECT                               // <-- indispensable
public:
    explicit GcsAuth(QObject* parent = nullptr);
    ~GcsAuth() override;                   // out-of-line to ensure vtable

    void startInteractiveAuth();

private:
    QOAuth2AuthorizationCodeFlow oauth_;
    std::unique_ptr<CredentialManager> credentialManager_;
    QString accountIdentifier_;
};
