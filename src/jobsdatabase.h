/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "job.h"

#include <QList>
#include <QString>
#include <optional>

class QSqlDatabase;

/**
 * SQLite-backed storage for job applications and their stage history.
 *
 * Every JobsDatabase instance owns its own named QSqlDatabase connection
 * (Qt's SQL connections are identified by name, not by object identity),
 * so multiple instances - e.g. in tests - never collide.
 */
class JobsDatabase
{
public:
    JobsDatabase();
    explicit JobsDatabase(const QString &path);
    ~JobsDatabase();

    JobsDatabase(const JobsDatabase &) = delete;
    JobsDatabase &operator=(const JobsDatabase &) = delete;

    /// The database file a default-constructed instance opens. Resolved in
    /// order: the --db override, the KAREER_DB_PATH environment variable
    /// (used by autotests), the path configured in kareerrc, and finally
    /// $XDG_DATA_HOME/kareer/kareer.sqlite.
    static QString defaultPath();

    /// $XDG_DATA_HOME/kareer/kareer.sqlite, ignoring every override.
    static QString standardPath();

    /// Set from the --db command-line option; wins over everything else.
    static void setPathOverride(const QString &path);

    /// The user's chosen location (kareerrc); empty means standardPath().
    static void setConfiguredPath(const QString &path);
    static QString configuredPath();

    /// True when --db or KAREER_DB_PATH decides the path, so the configured
    /// location has no effect.
    static bool hasForcedPath();

    /// While true (GUI first run, before the user has picked a location), a
    /// default-constructed instance opens nothing and reports an error.
    static void setSelectionPending(bool pending);
    static bool selectionPending();

    /// The file this instance opened, or empty if it opened nothing.
    QString path() const;

    /// Reopens against defaultPath() if that no longer matches path() (the
    /// user picked another location). Returns true if it reopened.
    bool reopenIfPathChanged();

    /// Performs a harmless write (rewrites the header's user_version), so
    /// callers can tell a read-only file apart from a usable one.
    bool checkWritable();

    bool isOpen() const;
    QString lastError() const;

    QList<Job> allJobs() const;
    std::optional<Job> jobById(int id) const;

    /// Inserts a new job. On success, job.id/createdAt/updatedAt are filled
    /// in and an initial Start -> job.stage transition is recorded.
    bool addJob(Job &job);

    /// Updates every field except stage (use setStage for that, so every
    /// stage change is captured in the history).
    bool updateJob(const Job &job);

    /// Moves a job to newStage, recording the transition. A no-op (but still
    /// successful) if the job is already in newStage.
    bool setStage(int id, const QString &newStage);

    bool deleteJob(int id);

    /// The steps one application went through, oldest first.
    QList<StageStep> stageHistory(int jobId) const;

    /// Rewrites an application's whole history in one transaction (used by
    /// the history editor, e.g. to log Applied -> Interview -> Rejected after
    /// the fact). Steps are sorted by time (stably), consecutive repeats of a
    /// stage collapse into the first, the job's stage becomes the last step's
    /// and its date applied the first step's date. Fails, changing nothing,
    /// if there are no steps, a stage is unknown, or a time is invalid.
    bool replaceStageHistory(int jobId, QList<StageStep> steps);

    /// Moves every application still at Applied whose last activity (the
    /// later of its date applied and its last stage change) is more than
    /// `days` days before `now` to Ghosted, recorded through setStage() like
    /// any other move. Returns how many were moved; does nothing if days <= 0.
    int ghostStaleApplications(int days, const QDateTime &now = QDateTime::currentDateTimeUtc());

    QList<StageTransition> stageTransitions() const;

private:
    void init(const QString &path);
    void close();
    bool migrate();
    Job jobFromQuery(class QSqlQuery &query) const;

    QString m_connectionName;
    QString m_path;
    QString m_lastError;
};
