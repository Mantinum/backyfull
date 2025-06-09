#pragma once

#include <QtCore/QObject>
#include <QtNetworkAuth/qoauth2authorizationcodeflow.h>

class GcsAuth : public QObject {
    Q_OBJECT                               // <-- indispensable
public:
    explicit GcsAuth(QObject* parent = nullptr);
    ~GcsAuth() override;                   // out-of-line to ensure vtable

    void startInteractiveAuth();

private:
    QOAuth2AuthorizationCodeFlow oauth_;
};
