#include "GcsAuth.h"
#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QStandardPaths>
#include <QDebug>
#include <optional>
#include <QtNetworkAuth/qoauthhttpserverreplyhandler.h>

namespace {
struct OAuthCredentials {
    QString clientId;
    QString clientSecret;
};

static std::optional<OAuthCredentials> loadCredentials() {
    QFile credFile("./oauth_credentials.json");
    if (credFile.exists() && credFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonDocument doc = QJsonDocument::fromJson(credFile.readAll());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj.contains("installed") && obj["installed"].isObject()) {
                QJsonObject inst = obj["installed"].toObject();
                OAuthCredentials c;
                c.clientId = inst.value("client_id").toString();
                c.clientSecret = inst.value("client_secret").toString();
                if (!c.clientId.isEmpty() && !c.clientSecret.isEmpty()) {
                    return c;
                }
            }
        }
    }
    qWarning() << "Failed to load oauth_credentials.json";
    return std::nullopt;
}
}

GcsAuth::GcsAuth(QObject* parent)
    : QObject(parent)
{
    oauth_.setAuthorizationUrl(QUrl("https://accounts.google.com/o/oauth2/v2/auth"));
    oauth_.setTokenUrl(QUrl("https://oauth2.googleapis.com/token"));

    if (auto creds = loadCredentials()) {
        oauth_.setClientIdentifier(creds->clientId);
        oauth_.setClientIdentifierSharedKey(creds->clientSecret);
    }

    oauth_.setRequestedScopeTokens({
        "https://www.googleapis.com/auth/devstorage.read_write"});

    auto *handler = new QOAuthHttpServerReplyHandler(0, this);
    oauth_.setReplyHandler(handler);

    connect(&oauth_, &QOAuth2AuthorizationCodeFlow::authorizeWithBrowser,
            [](const QUrl &url) { QDesktopServices::openUrl(url); });

    connect(&oauth_, &QOAuth2AuthorizationCodeFlow::granted, this, [this]() {
        qInfo() << "GCS auth success, token:" << oauth_.token();
    });
}

GcsAuth::~GcsAuth() = default;

void GcsAuth::startInteractiveAuth()
{
    oauth_.grant();
}


