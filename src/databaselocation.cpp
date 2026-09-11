/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "databaselocation.h"

#include "jobsdatabase.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>

using namespace Qt::Literals::StringLiterals;

namespace
{
constexpr auto ConfigFile = "kareerrc";
constexpr auto ConfigGroup = "Database";
constexpr auto ConfigKey = "Path";
constexpr auto DatabaseFileName = "kareer.sqlite";

KConfigGroup databaseGroup()
{
    return KConfigGroup(KSharedConfig::openConfig(QString::fromLatin1(ConfigFile)), QString::fromLatin1(ConfigGroup));
}

QString folderFile(const QUrl &folder)
{
    return QDir(folder.toLocalFile()).filePath(QString::fromLatin1(DatabaseFileName));
}
}

DatabaseLocation::DatabaseLocation(QObject *parent)
    : QObject(parent)
{
    if (JobsDatabase::selectionPending() && !JobsDatabase::configuredPath().isEmpty()) {
        m_missingPath = JobsDatabase::configuredPath();
    }
}

void DatabaseLocation::loadConfiguredPath()
{
    JobsDatabase::setConfiguredPath(databaseGroup().readEntry(ConfigKey, QString()));
}

bool DatabaseLocation::needsSetup()
{
    return !JobsDatabase::hasForcedPath() && !QFileInfo::exists(JobsDatabase::defaultPath());
}

QString DatabaseLocation::path() const
{
    return JobsDatabase::selectionPending() ? QString() : JobsDatabase::defaultPath();
}

QString DatabaseLocation::standardPath() const
{
    return JobsDatabase::standardPath();
}

bool DatabaseLocation::setupPending() const
{
    return JobsDatabase::selectionPending();
}

QString DatabaseLocation::missingPath() const
{
    return m_missingPath;
}

bool DatabaseLocation::overridden() const
{
    return JobsDatabase::hasForcedPath();
}

QString DatabaseLocation::lastError() const
{
    return m_lastError;
}

QString DatabaseLocation::lastNotice() const
{
    return m_lastNotice;
}

bool DatabaseLocation::useDefault()
{
    return switchTo(QString(), false);
}

bool DatabaseLocation::createIn(const QUrl &folder)
{
    if (!folder.isLocalFile()) {
        setMessages(i18n("Please choose a local folder."), QString());
        return false;
    }
    return switchTo(folderFile(folder), false);
}

bool DatabaseLocation::useFile(const QUrl &file)
{
    if (!file.isLocalFile()) {
        setMessages(i18n("Please choose a local file."), QString());
        return false;
    }
    return switchTo(file.toLocalFile(), true);
}

bool DatabaseLocation::moveTo(const QUrl &folder)
{
    if (!folder.isLocalFile()) {
        setMessages(i18n("Please choose a local folder."), QString());
        return false;
    }
    const QString source = JobsDatabase::defaultPath();
    const QString target = folderFile(folder);
    if (QFileInfo(source).canonicalFilePath() == QFileInfo(target).canonicalFilePath()) {
        setMessages(i18n("The database is already in that folder."), QString());
        return false;
    }
    if (QFileInfo::exists(target)) {
        setMessages(i18n("%1 already exists. Choose another folder, or open that file instead.", target), QString());
        return false;
    }
    if (!QFile::copy(source, target)) {
        setMessages(i18n("Could not copy the database to %1.", target), QString());
        return false;
    }
    if (!switchTo(target, true)) {
        QFile::remove(target);
        return false;
    }
    setMessages(QString(), i18n("Now using %1. The previous file at %2 was left in place.", target, source));
    return true;
}

void DatabaseLocation::clearMessages()
{
    setMessages(QString(), QString());
}

bool DatabaseLocation::switchTo(const QString &path, bool mustExist)
{
    const QString effective = path.isEmpty() ? JobsDatabase::standardPath() : path;

    if (mustExist && !QFileInfo::exists(effective)) {
        setMessages(i18n("%1 does not exist.", effective), QString());
        return false;
    }

    // Refuse to add Kareer's tables to some unrelated SQLite file.
    if (QFileInfo::exists(effective)) {
        const QString connection = u"kareer_probe"_s;
        bool foreign = false;
        {
            QSqlDatabase probe = QSqlDatabase::addDatabase(u"QSQLITE"_s, connection);
            probe.setDatabaseName(effective);
            probe.setConnectOptions(u"QSQLITE_OPEN_READONLY"_s);
            if (probe.open()) {
                const QStringList tables = probe.tables();
                foreign = !tables.isEmpty() && !tables.contains(u"jobs"_s);
            }
            probe.close();
        }
        QSqlDatabase::removeDatabase(connection);
        if (foreign) {
            setMessages(i18n("%1 is not a Kareer database.", effective), QString());
            return false;
        }
    }

    {
        // Opening creates the file and schema if needed; the write check
        // catches files that can be read but not written (for example a
        // single-file grant inside the Flatpak sandbox, where SQLite cannot
        // create its journal next to the database).
        JobsDatabase db(effective);
        if (!db.isOpen() || !db.lastError().isEmpty() || !db.checkWritable()) {
            setMessages(i18n("Could not use %1: %2", effective, db.lastError()), QString());
            return false;
        }
    }

    KConfigGroup group = databaseGroup();
    if (path.isEmpty()) {
        group.deleteEntry(ConfigKey);
    } else {
        group.writeEntry(ConfigKey, path);
    }
    group.sync();

    JobsDatabase::setConfiguredPath(path);
    JobsDatabase::setSelectionPending(false);
    m_missingPath.clear();
    setMessages(QString(), QString());
    Q_EMIT changed();
    return true;
}

void DatabaseLocation::setMessages(const QString &error, const QString &notice)
{
    if (m_lastError == error && m_lastNotice == notice) {
        return;
    }
    m_lastError = error;
    m_lastNotice = notice;
    Q_EMIT messageChanged();
}
