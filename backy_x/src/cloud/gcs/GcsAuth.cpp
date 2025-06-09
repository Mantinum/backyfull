#include "GcsAuth.h"
#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QStandardPaths>
#include <QDebug>
#include <QSettings>
#include <optional>
#include <QMultiMap>
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

    credentialManager_ = std::unique_ptr<CredentialManager>(createPlatformCredentialManager());

    QSettings settings;
    settings.beginGroup("GCS");
    accountIdentifier_ = settings.value("gcs_account_identifier", "default").toString();
    settings.endGroup();

    if (credentialManager_) {
        auto tokenOpt = credentialManager_->retrieveGcsRefreshToken(accountIdentifier_);
        if (tokenOpt && !tokenOpt->isEmpty()) {
            oauth_.setRefreshToken(*tokenOpt);
        }
    }

    auto *handler = new QOAuthHttpServerReplyHandler(0, this);
    oauth_.setReplyHandler(handler);

    // Extra params for Google
    oauth_.setModifyParametersFunction(
        [](QAbstractOAuth::Stage stage,
           QMultiMap<QString, QVariant>* params) {
            if (stage == QAbstractOAuth::Stage::RequestingAuthorization) {
                params->insert("access_type", "offline");
                params->insert("prompt", "consent");
            }
        });

    connect(&oauth_, &QOAuth2AuthorizationCodeFlow::authorizeWithBrowser,
            [](const QUrl &url) { QDesktopServices::openUrl(url); });

    connect(&oauth_, &QOAuth2AuthorizationCodeFlow::granted, this, [this]() {
        qInfo() << "GCS auth success, token:" << oauth_.token();
        QString rt = oauth_.refreshToken();
        if (credentialManager_ && !rt.isEmpty()) {
            credentialManager_->storeGcsRefreshToken(accountIdentifier_, rt);
        }

        QSettings s;
        s.beginGroup("GCS");
        s.setValue("gcs_last_authenticated_account", accountIdentifier_);
        s.endGroup();
    });
}

GcsAuth::~GcsAuth() = default;

void GcsAuth::startInteractiveAuth()
{
    oauth_.grant();
}


