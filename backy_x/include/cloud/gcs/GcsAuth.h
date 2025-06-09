#pragma once

#include <QtCore/QObject>
#include <QtNetworkAuth/qoauth2authorizationcodeflow.h>

class GcsAuth : public QObject {
    Q_OBJECT                // <= imperative
public:
    explicit GcsAuth(QObject* parent = nullptr);
    ~GcsAuth() override = default;   // force vtable generation

    void startInteractiveAuth();

private:
    QOAuth2AuthorizationCodeFlow oauth_;
};
