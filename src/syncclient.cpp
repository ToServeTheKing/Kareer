/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "syncclient.h"

#include "jobsdatabase.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>

using namespace Qt::Literals::StringLiterals;

namespace
{
KConfigGroup settingsGroup()
{
    return KConfigGroup(KSharedConfig::openConfig(u"kareerrc"_s), u"Sync"_s);
}

QByteArray randomToken(int bytes)
{
    QByteArray data(bytes, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(data.data()), bytes / 4);
    return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QJsonObject replyJson(QNetworkReply *reply)
{
    return QJsonDocument::fromJson(reply->readAll()).object();
}

QString replyError(QNetworkReply *reply, const QJsonObject &body)
{
    if (const QString description = body[u"error_description"_s].toString(); !description.isEmpty()) {
        return description;
    }
    if (const QString error = body[u"error"_s].toString(); !error.isEmpty()) {
        return error;
    }
    return reply->errorString();
}
}

SyncClient::SyncClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(3000);
    connect(&m_debounce, &QTimer::timeout, this, &SyncClient::syncNow);
    loadCredentials();
    m_lastSynced = settingsGroup().readEntry("LastSynced", QDateTime());
}

SyncClient::~SyncClient() = default;

QString SyncClient::serverUrl() const
{
    QString url = settingsGroup().readEntry("ServerUrl", QString()).trimmed();
    while (url.endsWith(u'/')) {
        url.chop(1);
    }
    return url;
}

void SyncClient::setServerUrl(const QString &url)
{
    if (url == serverUrl()) {
        return;
    }
    // Tokens and the change cursor belong to the old server.
    signOut();
    KConfigGroup group = settingsGroup();
    group.writeEntry("ServerUrl", url.trimmed());
    group.sync();
    JobsDatabase db;
    db.applySyncResult({}, {}, 0);
    Q_EMIT stateChanged();
}

bool SyncClient::signedIn() const
{
    return !m_credentials.refreshToken.isEmpty();
}

bool SyncClient::busy() const
{
    return m_busy;
}

QString SyncClient::lastError() const
{
    return m_lastError;
}

QDateTime SyncClient::lastSynced() const
{
    return m_lastSynced;
}

QString SyncClient::credentialsPath() const
{
    return QFileInfo(JobsDatabase::standardPath()).absolutePath() + u"/sync-credentials.json"_s;
}

void SyncClient::loadCredentials()
{
    QFile file(credentialsPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    if (o[u"server"_s].toString() != serverUrl()) {
        return;
    }
    m_credentials.clientId = o[u"client_id"_s].toString();
    m_credentials.accessToken = o[u"access_token"_s].toString();
    m_credentials.refreshToken = o[u"refresh_token"_s].toString();
    m_credentials.expiresAt = QDateTime::fromString(o[u"expires_at"_s].toString(), Qt::ISODate);
}

void SyncClient::saveCredentials() const
{
    const QString path = credentialsPath();
    if (m_credentials.refreshToken.isEmpty()) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(QJsonObject{
                                 {u"server"_s, serverUrl()},
                                 {u"client_id"_s, m_credentials.clientId},
                                 {u"access_token"_s, m_credentials.accessToken},
                                 {u"refresh_token"_s, m_credentials.refreshToken},
                                 {u"expires_at"_s, m_credentials.expiresAt.toString(Qt::ISODate)},
                             })
                   .toJson());
    file.commit();
}

bool SyncClient::storeTokens(const QJsonObject &reply)
{
    const QString access = reply[u"access_token"_s].toString();
    if (access.isEmpty()) {
        return false;
    }
    m_credentials.accessToken = access;
    m_credentials.refreshToken = reply[u"refresh_token"_s].toString(m_credentials.refreshToken);
    m_credentials.expiresAt = QDateTime::currentDateTimeUtc().addSecs(reply[u"expires_in"_s].toInt(3600));
    saveCredentials();
    return true;
}

void SyncClient::fail(const QString &message)
{
    m_busy = false;
    m_lastError = message;
    Q_EMIT stateChanged();
    Q_EMIT finished(false);
}

void SyncClient::succeed()
{
    m_busy = false;
    m_lastError.clear();
    Q_EMIT stateChanged();
    Q_EMIT finished(true);
}

QNetworkReply *SyncClient::postJson(const QString &path, const QJsonObject &body, bool authorized)
{
    QNetworkRequest request(QUrl(serverUrl() + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
    request.setTransferTimeout(30000);
    if (authorized) {
        request.setRawHeader("Authorization", "Bearer " + m_credentials.accessToken.toUtf8());
    }
    return m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

QNetworkReply *SyncClient::postForm(const QString &path, const QList<std::pair<QString, QString>> &form)
{
    QNetworkRequest request(QUrl(serverUrl() + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/x-www-form-urlencoded"_s);
    request.setTransferTimeout(30000);
    QUrlQuery query;
    for (const auto &[key, value] : form) {
        query.addQueryItem(key, QString::fromLatin1(QUrl::toPercentEncoding(value)));
    }
    return m_network->post(request, query.toString(QUrl::FullyEncoded).toUtf8());
}

// --- signing in --------------------------------------------------------------

void SyncClient::signIn()
{
    signIn([](const QUrl &url) {
        QDesktopServices::openUrl(url);
    });
}

void SyncClient::signIn(const std::function<void(const QUrl &)> &openUrl)
{
    if (m_busy) {
        return;
    }
    m_busy = true;
    m_lastError.clear();
    Q_EMIT stateChanged();
    if (serverUrl().isEmpty()) {
        fail(i18n("Enter the address of your Kareer server first"));
        return;
    }

    delete m_loopback;
    m_loopback = new QTcpServer(this);
    if (!m_loopback->listen(QHostAddress::LocalHost)) {
        fail(m_loopback->errorString());
        return;
    }
    const QString redirectUri = u"http://127.0.0.1:%1/callback"_s.arg(m_loopback->serverPort());

    // A fresh registration per sign-in, because the loopback port is part of
    // the redirect URI and the server matches it exactly.
    QNetworkReply *reply = postJson(u"/register"_s,
                                    QJsonObject{
                                        {u"client_name"_s, u"Kareer desktop"_s},
                                        {u"redirect_uris"_s, QJsonArray{redirectUri}},
                                        {u"token_endpoint_auth_method"_s, u"none"_s},
                                        {u"grant_types"_s, QJsonArray{u"authorization_code"_s, u"refresh_token"_s}},
                                        {u"response_types"_s, QJsonArray{u"code"_s}},
                                    },
                                    false);
    connect(reply, &QNetworkReply::finished, this, [this, reply, redirectUri, openUrl] {
        reply->deleteLater();
        const QJsonObject body = replyJson(reply);
        if (reply->error() != QNetworkReply::NoError || body[u"client_id"_s].toString().isEmpty()) {
            fail(i18n("Could not reach %1: %2", serverUrl(), replyError(reply, body)));
            return;
        }
        m_credentials = {};
        m_credentials.clientId = body[u"client_id"_s].toString();

        const QByteArray verifier = randomToken(48);
        const QByteArray challenge =
            QCryptographicHash::hash(verifier, QCryptographicHash::Sha256).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        const QString state = QString::fromLatin1(randomToken(24));

        connect(m_loopback, &QTcpServer::newConnection, this, [this, state, redirectUri, verifier] {
            QTcpSocket *socket = m_loopback->nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, state, redirectUri, verifier] {
                const QByteArray requestLine = socket->readLine();
                // "GET /callback?code=...&state=... HTTP/1.1"
                const QList<QByteArray> parts = requestLine.split(' ');
                const QUrl url(QString::fromLatin1(parts.value(1)));
                if (url.path() != u"/callback"_s) {
                    socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                    return;
                }
                const QUrlQuery query(url);
                const bool ok = query.queryItemValue(u"state"_s) == state && query.hasQueryItem(u"code"_s);
                const QString problem = query.queryItemValue(u"error_description"_s, QUrl::FullyDecoded);
                const QByteArray page = ok ? QByteArray("<h1>Signed in to Kareer</h1><p>You can close this tab.</p>")
                                           : QByteArray("<h1>Kareer sign-in failed</h1><p>" + problem.toHtmlEscaped().toUtf8() + "</p>");
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: "
                              + QByteArray::number(page.size()) + "\r\n\r\n" + page);
                socket->disconnectFromHost();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                m_loopback->close();
                if (!ok) {
                    fail(problem.isEmpty() ? i18n("Sign-in was cancelled") : problem);
                    return;
                }
                finishSignIn(query.queryItemValue(u"code"_s, QUrl::FullyDecoded), redirectUri, QString::fromLatin1(verifier));
            });
        });

        QUrl authorize(serverUrl() + u"/authorize"_s);
        QUrlQuery query;
        query.addQueryItem(u"response_type"_s, u"code"_s);
        query.addQueryItem(u"client_id"_s, m_credentials.clientId);
        query.addQueryItem(u"redirect_uri"_s, redirectUri);
        query.addQueryItem(u"code_challenge"_s, QString::fromLatin1(challenge));
        query.addQueryItem(u"code_challenge_method"_s, u"S256"_s);
        query.addQueryItem(u"state"_s, state);
        authorize.setQuery(query);
        openUrl(authorize);
    });
}

void SyncClient::finishSignIn(const QString &code, const QString &redirectUri, const QString &verifier)
{
    QNetworkReply *reply = postForm(u"/token"_s,
                                    {
                                        {u"grant_type"_s, u"authorization_code"_s},
                                        {u"code"_s, code},
                                        {u"redirect_uri"_s, redirectUri},
                                        {u"client_id"_s, m_credentials.clientId},
                                        {u"code_verifier"_s, verifier},
                                    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QJsonObject body = replyJson(reply);
        if (reply->error() != QNetworkReply::NoError || !storeTokens(body)) {
            fail(replyError(reply, body));
            return;
        }
        succeed();
        // First sync right away, so the other machine's applications appear.
        syncNow();
    });
}

void SyncClient::signOut()
{
    m_credentials = {};
    saveCredentials();
    m_lastError.clear();
    Q_EMIT stateChanged();
}

// --- syncing -----------------------------------------------------------------

void SyncClient::withFreshToken(const std::function<void()> &next)
{
    if (QDateTime::currentDateTimeUtc().addSecs(60) < m_credentials.expiresAt) {
        next();
        return;
    }
    QNetworkReply *reply = postForm(u"/token"_s,
                                    {
                                        {u"grant_type"_s, u"refresh_token"_s},
                                        {u"refresh_token"_s, m_credentials.refreshToken},
                                        {u"client_id"_s, m_credentials.clientId},
                                    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, next] {
        reply->deleteLater();
        const QJsonObject body = replyJson(reply);
        if (reply->error() == QNetworkReply::NoError && storeTokens(body)) {
            next();
            return;
        }
        // The server answered and refused: the sign-in is gone for good.
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 400) {
            m_credentials = {};
            saveCredentials();
            fail(i18n("Signed out by the server. Sign in again to keep syncing."));
            return;
        }
        fail(replyError(reply, body));
    });
}

void SyncClient::syncNow()
{
    m_debounce.stop();
    if (!signedIn() || JobsDatabase::selectionPending()) {
        return;
    }
    if (m_busy) {
        m_debounce.start(); // try again once the current round trip is done
        return;
    }
    m_busy = true;
    Q_EMIT stateChanged();
    withFreshToken([this] {
        postSync(false);
    });
}

void SyncClient::postSync(bool retried)
{
    const QJsonArray pushed = [] {
        JobsDatabase db;
        return db.pendingSyncRecords();
    }();
    const qint64 since = [] {
        JobsDatabase db;
        return db.syncCursor();
    }();
    QNetworkReply *reply = postJson(u"/api/sync"_s, QJsonObject{{u"since"_s, since}, {u"jobs"_s, pushed}}, true);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pushed, retried] {
        reply->deleteLater();
        const QJsonObject body = replyJson(reply);
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401 && !retried) {
            m_credentials.expiresAt = {};
            withFreshToken([this] {
                postSync(true);
            });
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            fail(replyError(reply, body));
            return;
        }
        JobsDatabase db;
        const int changed = db.applySyncResult(pushed, body[u"jobs"_s].toArray(), body[u"cursor"_s].toInteger());
        if (changed < 0) {
            fail(db.lastError());
            return;
        }
        m_lastSynced = QDateTime::currentDateTimeUtc();
        KConfigGroup group = settingsGroup();
        group.writeEntry("LastSynced", m_lastSynced);
        group.sync();
        succeed();
        Q_EMIT synced(changed);
    });
}

void SyncClient::scheduleSync()
{
    if (!signedIn()) {
        return;
    }
    JobsDatabase db;
    if (db.hasPendingSyncRecords()) {
        m_debounce.start();
    }
}

bool SyncClient::runBlocking(const std::function<void()> &start, int timeoutMs)
{
    QEventLoop loop;
    bool ok = false;
    bool done = false;
    const auto connection = connect(this, &SyncClient::finished, &loop, [&](bool result) {
        ok = result;
        done = true;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, [&] {
        if (!done) {
            m_lastError = i18n("Timed out");
        }
        loop.quit();
    });
    start();
    if (!done && m_busy) {
        loop.exec();
    } else if (!done) {
        // start() had nothing to do (not signed in, no database).
        ok = m_lastError.isEmpty();
    }
    disconnect(connection);
    return ok;
}
