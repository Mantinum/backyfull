#ifndef GCSAUTH_H
#define GCSAUTH_H

#include <QObject>
#include <QtNetworkAuth/qoauth2authorizationcodeflow.h>
#include <QtNetworkAuth/qoauthhttpserverreplyhandler.h>

class GcsAuth : public QObject {
    Q_OBJECT
public:
    explicit GcsAuth(QObject* parent = nullptr);
    void startInteractiveAuth();
private:
    QOAuth2AuthorizationCodeFlow oauth_;
};

#endif // GCSAUTH_H
