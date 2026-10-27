/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;

/**
 * Keeps the local database in step with a Kareer server (server/ in this
 * repository, self-hosted), which is also where an assistant's MCP connector
 * makes its changes. There is no default server: sync stays off until the
 * user enters one.
 *
 * Signing in is OAuth with PKCE: the client registers itself with the
 * server, opens the browser at its authorize page (which hands off to
 * Authentik) and catches the redirect on a loopback port. The tokens are kept
 * in sync-credentials.json beside the default database, readable only by the
 * user. A sync pushes JobsDatabase::pendingSyncRecords() and merges what
 * comes back; see JobsDatabase for the rules.
 *
 * The GUI syncs at startup, every few minutes, and shortly after a local
 * change; the CLI syncs around each subcommand (see Cli::run).
 */
class SyncClient : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Sync)
    QML_SINGLETON

    Q_PROPERTY(QString serverUrl READ serverUrl WRITE setServerUrl NOTIFY stateChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QDateTime lastSynced READ lastSynced NOTIFY stateChanged)

public:
    explicit SyncClient(QObject *parent = nullptr);
    ~SyncClient() override;

    QString serverUrl() const;
    void setServerUrl(const QString &url);
    bool signedIn() const;
    bool busy() const;
    QString lastError() const;
    QDateTime lastSynced() const;

    /// Opens the browser to sign in; finished() reports the outcome.
    /// `openUrl` defaults to QDesktopServices; the CLI passes its own.
    void signIn(const std::function<void(const QUrl &)> &openUrl);
    Q_INVOKABLE void signIn();
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void syncNow();
    /// Syncs a few seconds from now if anything local is waiting to go up.
    Q_INVOKABLE void scheduleSync();

    /// Runs `start` (one of the actions above) and blocks in a local event
    /// loop until it finishes. For the CLI. Returns false on failure.
    bool runBlocking(const std::function<void()> &start, int timeoutMs);

Q_SIGNALS:
    void stateChanged();
    /// A sync finished; `changed` is how many local jobs it changed.
    void synced(int changed);
    /// Any action (sign-in or sync) ended; `ok` is false if it failed.
    void finished(bool ok);

private:
    struct Credentials {
        QString clientId;
        QString accessToken;
        QString refreshToken;
        QDateTime expiresAt;
    };

    void loadCredentials();
    void saveCredentials() const;
    QString credentialsPath() const;

    void fail(const QString &message);
    void succeed();
    void withFreshToken(const std::function<void()> &next);
    void postSync(bool retried);
    void finishSignIn(const QString &code, const QString &redirectUri, const QString &verifier);
    QNetworkReply *postJson(const QString &path, const QJsonObject &body, bool authorized);
    QNetworkReply *postForm(const QString &path, const QList<std::pair<QString, QString>> &form);
    bool storeTokens(const QJsonObject &reply);

    QNetworkAccessManager *m_network;
    QTcpServer *m_loopback = nullptr;
    QTimer m_debounce;
    Credentials m_credentials;
    bool m_busy = false;
    QString m_lastError;
    QDateTime m_lastSynced;
};
