/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QUrl>

/**
 * Where the database lives, as chosen by the user: the first-run dialog and
 * the Preferences page drive this. The choice is stored in kareerrc
 * ([Database] Path) and pushed into JobsDatabase::setConfiguredPath(); the
 * --db option and KAREER_DB_PATH still win over it (see JobsDatabase::defaultPath()).
 *
 * Emits changed() after every switch; Main.qml reacts by refreshing the
 * models, which reopen themselves via JobsDatabase::reopenIfPathChanged().
 */
class DatabaseLocation : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The effective database file, or empty while the first-run choice is pending.
    Q_PROPERTY(QString path READ path NOTIFY changed)
    /// $XDG_DATA_HOME/kareer/kareer.sqlite.
    Q_PROPERTY(QString standardPath READ standardPath CONSTANT)
    Q_PROPERTY(bool setupPending READ setupPending NOTIFY changed)
    /// The configured file that could not be found (so setup is being asked again), if any.
    Q_PROPERTY(QString missingPath READ missingPath NOTIFY changed)
    /// True when --db or KAREER_DB_PATH is in effect; the stored location is then ignored.
    Q_PROPERTY(bool overridden READ overridden CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY messageChanged)
    Q_PROPERTY(QString lastNotice READ lastNotice NOTIFY messageChanged)

public:
    explicit DatabaseLocation(QObject *parent = nullptr);

    /// Reads kareerrc into JobsDatabase::setConfiguredPath(). Call once at
    /// startup, for both the GUI and the CLI, so they share one database.
    static void loadConfiguredPath();

    /// True when the GUI should ask where the database lives: nothing forces
    /// a path and the file it would open does not exist yet.
    static bool needsSetup();

    QString path() const;
    QString standardPath() const;
    bool setupPending() const;
    QString missingPath() const;
    bool overridden() const;
    QString lastError() const;
    QString lastNotice() const;

    /// Create (or reuse) the database at the standard location.
    Q_INVOKABLE bool useDefault();
    /// Create (or reuse) kareer.sqlite inside the chosen folder.
    Q_INVOKABLE bool createIn(const QUrl &folder);
    /// Use an existing database file where it is.
    Q_INVOKABLE bool useFile(const QUrl &file);
    /// Copy the current database into the chosen folder and switch to the
    /// copy. The original file is left in place.
    Q_INVOKABLE bool moveTo(const QUrl &folder);

    Q_INVOKABLE void clearMessages();

Q_SIGNALS:
    void changed();
    void messageChanged();

private:
    /// Validates path, stores it (empty = standard location), and switches to it.
    bool switchTo(const QString &path, bool mustExist);
    void setMessages(const QString &error, const QString &notice);

    QString m_missingPath;
    QString m_lastError;
    QString m_lastNotice;
};
