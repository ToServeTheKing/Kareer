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

    /// Default location: $XDG_DATA_HOME/kareer/kareer.sqlite, overridable
    /// with the KAREER_DB_PATH environment variable (used by autotests).
    static QString defaultPath();

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

    QList<StageTransition> stageTransitions() const;

private:
    void init(const QString &path);
    bool migrate();
    Job jobFromQuery(class QSqlQuery &query) const;

    QString m_connectionName;
    QString m_lastError;
};
