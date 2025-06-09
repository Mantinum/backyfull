#pragma once

#include <QtCore/QObject>
#include <QtNetworkAuth/qoauth2authorizationcodeflow.h>

class GcsAuth : public QObject {
    Q_OBJECT
public:
    explicit GcsAuth(QObject* parent = nullptr);
    ~GcsAuth() override = default;

    void startInteractiveAuth();

private:
    QOAuth2AuthorizationCodeFlow oauth_;
};
