/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "jobsdatabase.h"
#include "jobstage.h"

#include <QAtomicInteger>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>

using namespace Qt::Literals::StringLiterals;

namespace
{
QAtomicInteger<int> s_connectionCounter{0};

QString s_pathOverride;
QString s_configuredPath;
bool s_selectionPending = false;

QVariant salaryToVariant(int value)
{
    if (value < 0) {
        return {};
    }
    return value;
}

int salaryFromVariant(const QVariant &value)
{
    return value.isNull() ? -1 : value.toInt();
}

/// Modified-at stamps decide which write wins a sync, so they keep
/// milliseconds; everything else in the file is to the second.
QString syncStamp()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}
}

JobsDatabase::JobsDatabase()
{
    if (s_selectionPending) {
        m_lastError = u"No database selected"_s;
        return;
    }
    init(defaultPath());
}

JobsDatabase::JobsDatabase(const QString &path)
{
    init(path);
}

JobsDatabase::~JobsDatabase()
{
    close();
}

void JobsDatabase::close()
{
    if (m_connectionName.isEmpty()) {
        return;
    }
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isValid()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
    m_connectionName.clear();
    m_path.clear();
}

QString JobsDatabase::defaultPath()
{
    if (!s_pathOverride.isEmpty()) {
        return s_pathOverride;
    }
    const QString envPath = qEnvironmentVariable("KAREER_DB_PATH");
    if (!envPath.isEmpty()) {
        return envPath;
    }
    if (!s_configuredPath.isEmpty()) {
        return s_configuredPath;
    }
    return standardPath();
}

QString JobsDatabase::standardPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/kareer/kareer.sqlite"_s;
}

void JobsDatabase::setPathOverride(const QString &path)
{
    s_pathOverride = path;
}

void JobsDatabase::setConfiguredPath(const QString &path)
{
    s_configuredPath = path;
}

QString JobsDatabase::configuredPath()
{
    return s_configuredPath;
}

bool JobsDatabase::hasForcedPath()
{
    return !s_pathOverride.isEmpty() || !qEnvironmentVariableIsEmpty("KAREER_DB_PATH");
}

void JobsDatabase::setSelectionPending(bool pending)
{
    s_selectionPending = pending;
}

bool JobsDatabase::selectionPending()
{
    return s_selectionPending;
}

QString JobsDatabase::path() const
{
    return m_path;
}

bool JobsDatabase::reopenIfPathChanged()
{
    if (s_selectionPending) {
        return false;
    }
    const QString wanted = defaultPath();
    if (!m_path.isEmpty() && wanted == m_path) {
        return false;
    }
    close();
    m_lastError.clear();
    init(wanted);
    return true;
}

bool JobsDatabase::checkWritable()
{
    if (!isOpen()) {
        return false;
    }
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (!query.exec(u"PRAGMA user_version"_s) || !query.next()) {
        m_lastError = query.lastError().text();
        return false;
    }
    const int version = query.value(0).toInt();
    query.finish();
    if (!query.exec(u"PRAGMA user_version = %1"_s.arg(version))) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

void JobsDatabase::init(const QString &path)
{
    m_connectionName = u"kareer_conn_%1"_s.arg(s_connectionCounter.fetchAndAddRelaxed(1));
    m_path = path;

    QDir().mkpath(QFileInfo(path).absolutePath());

    QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, m_connectionName);
    db.setDatabaseName(path);
    if (!db.open()) {
        m_lastError = db.lastError().text();
        return;
    }

    QSqlQuery pragma(db);
    pragma.exec(u"PRAGMA foreign_keys = ON"_s);

    if (!migrate()) {
        return;
    }
}

bool JobsDatabase::migrate()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);

    if (!query.exec(uR"(
        CREATE TABLE IF NOT EXISTS jobs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            company TEXT NOT NULL,
            title TEXT NOT NULL,
            location TEXT,
            remote_type TEXT,
            source TEXT,
            url TEXT,
            date_applied TEXT,
            salary_min INTEGER,
            salary_max INTEGER,
            salary_expectation INTEGER,
            currency TEXT,
            notes TEXT,
            contact TEXT,
            stage TEXT NOT NULL,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
    )"_s)) {
        m_lastError = query.lastError().text();
        return false;
    }

    if (!query.exec(uR"(
        CREATE TABLE IF NOT EXISTS stage_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            job_id INTEGER NOT NULL REFERENCES jobs(id) ON DELETE CASCADE,
            from_stage TEXT,
            to_stage TEXT NOT NULL,
            changed_at TEXT NOT NULL
        )
    )"_s)) {
        m_lastError = query.lastError().text();
        return false;
    }

    // Sync bookkeeping, added in 0.2. Existing rows get an id and count as
    // modified when they were last updated, and start dirty so the first
    // sync uploads them.
    QSet<QString> jobColumns;
    if (!query.exec(u"PRAGMA table_info(jobs)"_s)) {
        m_lastError = query.lastError().text();
        return false;
    }
    while (query.next()) {
        jobColumns.insert(query.value(1).toString());
    }
    for (const auto &[column, type] : {
             std::pair{u"uuid"_s, u"TEXT"_s},
             std::pair{u"fields_modified_at"_s, u"TEXT"_s},
             std::pair{u"history_modified_at"_s, u"TEXT"_s},
             std::pair{u"sync_dirty"_s, u"INTEGER NOT NULL DEFAULT 1"_s},
         }) {
        if (!jobColumns.contains(column) && !query.exec(u"ALTER TABLE jobs ADD COLUMN %1 %2"_s.arg(column, type))) {
            m_lastError = query.lastError().text();
            return false;
        }
    }
    for (const QString &statement : {
             u"UPDATE jobs SET uuid = lower(hex(randomblob(16))) WHERE uuid IS NULL"_s,
             u"UPDATE jobs SET fields_modified_at = updated_at WHERE fields_modified_at IS NULL"_s,
             u"UPDATE jobs SET history_modified_at = updated_at WHERE history_modified_at IS NULL"_s,
             u"CREATE UNIQUE INDEX IF NOT EXISTS jobs_uuid ON jobs(uuid)"_s,
             u"CREATE TABLE IF NOT EXISTS sync_tombstones (uuid TEXT PRIMARY KEY)"_s,
             u"CREATE TABLE IF NOT EXISTS sync_state (key TEXT PRIMARY KEY, value TEXT)"_s,
         }) {
        if (!query.exec(statement)) {
            m_lastError = query.lastError().text();
            return false;
        }
    }

    // Rows written before stages were canonicalized on write (e.g.
    // "rejected" via the CLI) would otherwise show up as separate Sankey
    // nodes with default column/color. lower() is ASCII-only, which is fine:
    // the stage vocabulary is ASCII.
    for (const QString &stage : JobStage::canonicalStages()) {
        for (const QString &statement : {
                 u"UPDATE jobs SET stage = :s WHERE stage != :s AND lower(stage) = lower(:s)"_s,
                 u"UPDATE stage_history SET to_stage = :s WHERE to_stage != :s AND lower(to_stage) = lower(:s)"_s,
                 u"UPDATE stage_history SET from_stage = :s WHERE from_stage != :s AND lower(from_stage) = lower(:s)"_s,
             }) {
            QSqlQuery normalize(db);
            normalize.prepare(statement);
            normalize.bindValue(u":s"_s, stage);
            if (!normalize.exec()) {
                m_lastError = normalize.lastError().text();
                return false;
            }
        }
    }

    return true;
}

bool JobsDatabase::isOpen() const
{
    return QSqlDatabase::database(m_connectionName, false).isOpen();
}

QString JobsDatabase::lastError() const
{
    return m_lastError;
}

Job JobsDatabase::jobFromQuery(QSqlQuery &query) const
{
    Job job;
    job.id = query.value(u"id"_s).toInt();
    job.company = query.value(u"company"_s).toString();
    job.title = query.value(u"title"_s).toString();
    job.location = query.value(u"location"_s).toString();
    job.remoteType = query.value(u"remote_type"_s).toString();
    job.source = query.value(u"source"_s).toString();
    job.url = query.value(u"url"_s).toString();
    job.dateApplied = QDate::fromString(query.value(u"date_applied"_s).toString(), Qt::ISODate);
    job.salaryMin = salaryFromVariant(query.value(u"salary_min"_s));
    job.salaryMax = salaryFromVariant(query.value(u"salary_max"_s));
    job.salaryExpectation = salaryFromVariant(query.value(u"salary_expectation"_s));
    job.currency = query.value(u"currency"_s).toString();
    job.notes = query.value(u"notes"_s).toString();
    job.contact = query.value(u"contact"_s).toString();
    job.stage = query.value(u"stage"_s).toString();
    job.createdAt = QDateTime::fromString(query.value(u"created_at"_s).toString(), Qt::ISODate);
    job.updatedAt = QDateTime::fromString(query.value(u"updated_at"_s).toString(), Qt::ISODate);
    return job;
}

QList<Job> JobsDatabase::allJobs() const
{
    QList<Job> jobs;
    if (!isOpen()) {
        return jobs;
    }
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(u"SELECT * FROM jobs ORDER BY date_applied DESC, id DESC"_s);
    if (!query.exec()) {
        return jobs;
    }
    while (query.next()) {
        jobs.append(jobFromQuery(query));
    }
    return jobs;
}

std::optional<Job> JobsDatabase::jobById(int id) const
{
    if (!isOpen()) {
        return std::nullopt;
    }
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(u"SELECT * FROM jobs WHERE id = :id"_s);
    query.bindValue(u":id"_s, id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return jobFromQuery(query);
}

bool JobsDatabase::addJob(Job &job)
{
    if (job.stage.isEmpty()) {
        job.stage = QStringLiteral("Applied");
    }
    if (!JobStage::isValid(job.stage)) {
        m_lastError = u"Unknown stage '%1'"_s.arg(job.stage);
        return false;
    }
    job.stage = JobStage::canonical(job.stage);

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    job.createdAt = now;
    job.updatedAt = now;

    QSqlQuery query(db);
    query.prepare(
        uR"(
        INSERT INTO jobs (company, title, location, remote_type, source, url, date_applied,
                           salary_min, salary_max, salary_expectation, currency, notes, contact,
                           stage, created_at, updated_at, uuid, fields_modified_at, history_modified_at)
        VALUES (:company, :title, :location, :remote_type, :source, :url, :date_applied,
                :salary_min, :salary_max, :salary_expectation, :currency, :notes, :contact,
                :stage, :created_at, :updated_at, :uuid, :modified_at, :modified_at)
    )"_s);
    query.bindValue(u":company"_s, job.company);
    query.bindValue(u":title"_s, job.title);
    query.bindValue(u":location"_s, job.location);
    query.bindValue(u":remote_type"_s, job.remoteType);
    query.bindValue(u":source"_s, job.source);
    query.bindValue(u":url"_s, job.url);
    query.bindValue(u":date_applied"_s, job.dateApplied.isValid() ? job.dateApplied.toString(Qt::ISODate) : QVariant());
    query.bindValue(u":salary_min"_s, salaryToVariant(job.salaryMin));
    query.bindValue(u":salary_max"_s, salaryToVariant(job.salaryMax));
    query.bindValue(u":salary_expectation"_s, salaryToVariant(job.salaryExpectation));
    query.bindValue(u":currency"_s, job.currency);
    query.bindValue(u":notes"_s, job.notes);
    query.bindValue(u":contact"_s, job.contact);
    query.bindValue(u":stage"_s, job.stage);
    query.bindValue(u":created_at"_s, job.createdAt.toString(Qt::ISODate));
    query.bindValue(u":updated_at"_s, job.updatedAt.toString(Qt::ISODate));
    query.bindValue(u":uuid"_s, QUuid::createUuid().toString(QUuid::WithoutBraces));
    query.bindValue(u":modified_at"_s, syncStamp());

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    job.id = query.lastInsertId().toInt();

    QSqlQuery history(db);
    history.prepare(u"INSERT INTO stage_history (job_id, from_stage, to_stage, changed_at) VALUES (:job_id, NULL, :to_stage, :changed_at)"_s);
    history.bindValue(u":job_id"_s, job.id);
    history.bindValue(u":to_stage"_s, job.stage);
    history.bindValue(u":changed_at"_s, job.createdAt.toString(Qt::ISODate));
    if (!history.exec()) {
        m_lastError = history.lastError().text();
        return false;
    }

    return true;
}

bool JobsDatabase::updateJob(const Job &job)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(
        uR"(
        UPDATE jobs SET company = :company, title = :title, location = :location,
                        remote_type = :remote_type, source = :source, url = :url,
                        date_applied = :date_applied, salary_min = :salary_min,
                        salary_max = :salary_max, salary_expectation = :salary_expectation,
                        currency = :currency, notes = :notes, contact = :contact,
                        updated_at = :updated_at, fields_modified_at = :modified_at, sync_dirty = 1
        WHERE id = :id
    )"_s);
    query.bindValue(u":company"_s, job.company);
    query.bindValue(u":title"_s, job.title);
    query.bindValue(u":location"_s, job.location);
    query.bindValue(u":remote_type"_s, job.remoteType);
    query.bindValue(u":source"_s, job.source);
    query.bindValue(u":url"_s, job.url);
    query.bindValue(u":date_applied"_s, job.dateApplied.isValid() ? job.dateApplied.toString(Qt::ISODate) : QVariant());
    query.bindValue(u":salary_min"_s, salaryToVariant(job.salaryMin));
    query.bindValue(u":salary_max"_s, salaryToVariant(job.salaryMax));
    query.bindValue(u":salary_expectation"_s, salaryToVariant(job.salaryExpectation));
    query.bindValue(u":currency"_s, job.currency);
    query.bindValue(u":notes"_s, job.notes);
    query.bindValue(u":contact"_s, job.contact);
    query.bindValue(u":updated_at"_s, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.bindValue(u":modified_at"_s, syncStamp());
    query.bindValue(u":id"_s, job.id);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    if (query.numRowsAffected() < 1) {
        m_lastError = u"No job with id %1"_s.arg(job.id);
        return false;
    }
    return true;
}

bool JobsDatabase::setStage(int id, const QString &newStage)
{
    if (!JobStage::isValid(newStage)) {
        m_lastError = u"Unknown stage '%1'"_s.arg(newStage);
        return false;
    }
    const QString stage = JobStage::canonical(newStage);

    const auto current = jobById(id);
    if (!current) {
        m_lastError = u"No job with id %1"_s.arg(id);
        return false;
    }
    if (current->stage.compare(stage, Qt::CaseInsensitive) == 0) {
        return true;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    const QDateTime now = QDateTime::currentDateTimeUtc();

    QSqlQuery query(db);
    query.prepare(u"UPDATE jobs SET stage = :stage, updated_at = :updated_at, history_modified_at = :modified_at, sync_dirty = 1 WHERE id = :id"_s);
    query.bindValue(u":stage"_s, stage);
    query.bindValue(u":updated_at"_s, now.toString(Qt::ISODate));
    query.bindValue(u":modified_at"_s, syncStamp());
    query.bindValue(u":id"_s, id);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }

    QSqlQuery history(db);
    history.prepare(u"INSERT INTO stage_history (job_id, from_stage, to_stage, changed_at) VALUES (:job_id, :from_stage, :to_stage, :changed_at)"_s);
    history.bindValue(u":job_id"_s, id);
    history.bindValue(u":from_stage"_s, current->stage);
    history.bindValue(u":to_stage"_s, stage);
    history.bindValue(u":changed_at"_s, now.toString(Qt::ISODate));
    if (!history.exec()) {
        m_lastError = history.lastError().text();
        return false;
    }

    return true;
}

bool JobsDatabase::deleteJob(int id)
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(u"INSERT OR IGNORE INTO sync_tombstones (uuid) SELECT uuid FROM jobs WHERE id = :id"_s);
    query.bindValue(u":id"_s, id);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    query.prepare(u"DELETE FROM jobs WHERE id = :id"_s);
    query.bindValue(u":id"_s, id);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    if (query.numRowsAffected() < 1) {
        m_lastError = u"No job with id %1"_s.arg(id);
        return false;
    }
    return true;
}

QList<StageStep> JobsDatabase::stageHistory(int jobId) const
{
    QList<StageStep> steps;
    if (!isOpen()) {
        return steps;
    }
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(u"SELECT to_stage, changed_at FROM stage_history WHERE job_id = :id ORDER BY changed_at ASC, id ASC"_s);
    query.bindValue(u":id"_s, jobId);
    if (!query.exec()) {
        return steps;
    }
    while (query.next()) {
        steps.append({query.value(0).toString(), QDateTime::fromString(query.value(1).toString(), Qt::ISODate)});
    }
    return steps;
}

bool JobsDatabase::replaceStageHistory(int jobId, QList<StageStep> steps)
{
    if (!jobById(jobId)) {
        m_lastError = u"No job with id %1"_s.arg(jobId);
        return false;
    }
    if (steps.isEmpty()) {
        m_lastError = u"An application needs at least one stage"_s;
        return false;
    }
    for (StageStep &step : steps) {
        if (!JobStage::isValid(step.stage)) {
            m_lastError = u"Unknown stage '%1'"_s.arg(step.stage);
            return false;
        }
        if (!step.at.isValid()) {
            m_lastError = u"Missing date for stage '%1'"_s.arg(step.stage);
            return false;
        }
        step.stage = JobStage::canonical(step.stage);
    }
    std::stable_sort(steps.begin(), steps.end(), [](const StageStep &a, const StageStep &b) {
        return a.at < b.at;
    });
    QList<StageStep> collapsed;
    for (const StageStep &step : std::as_const(steps)) {
        if (collapsed.isEmpty() || collapsed.last().stage != step.stage) {
            collapsed.append(step);
        }
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) {
        m_lastError = db.lastError().text();
        return false;
    }
    const auto fail = [&](const QSqlQuery &query) {
        m_lastError = query.lastError().text();
        db.rollback();
        return false;
    };

    if (!writeHistory(jobId, collapsed)) {
        db.rollback();
        return false;
    }
    // date_applied is one of the fields, so both halves count as modified.
    QSqlQuery query(db);
    query.prepare(
        u"UPDATE jobs SET stage = :stage, date_applied = :date_applied, updated_at = :updated_at, "
        u"fields_modified_at = :modified_at, history_modified_at = :modified_at, sync_dirty = 1 WHERE id = :id"_s);
    query.bindValue(u":modified_at"_s, syncStamp());
    query.bindValue(u":stage"_s, collapsed.last().stage);
    query.bindValue(u":date_applied"_s, collapsed.first().at.toLocalTime().date().toString(Qt::ISODate));
    query.bindValue(u":updated_at"_s, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.bindValue(u":id"_s, jobId);
    if (!query.exec()) {
        return fail(query);
    }
    if (!db.commit()) {
        m_lastError = db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

bool JobsDatabase::writeHistory(int jobId, const QList<StageStep> &steps)
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(u"DELETE FROM stage_history WHERE job_id = :id"_s);
    query.bindValue(u":id"_s, jobId);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    QString previous;
    for (const StageStep &step : steps) {
        query.prepare(u"INSERT INTO stage_history (job_id, from_stage, to_stage, changed_at) VALUES (:job_id, :from_stage, :to_stage, :changed_at)"_s);
        query.bindValue(u":job_id"_s, jobId);
        query.bindValue(u":from_stage"_s, previous.isEmpty() ? QVariant() : QVariant(previous));
        query.bindValue(u":to_stage"_s, step.stage);
        query.bindValue(u":changed_at"_s, step.at.toUTC().toString(Qt::ISODate));
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            return false;
        }
        previous = step.stage;
    }
    return true;
}

int JobsDatabase::ghostStaleApplications(int days, const QDateTime &now)
{
    if (days <= 0 || !isOpen()) {
        return 0;
    }

    QList<int> stale;
    {
        QSqlQuery query(QSqlDatabase::database(m_connectionName));
        query.prepare(
            uR"(
            SELECT j.id, j.date_applied, j.created_at, MAX(h.changed_at)
            FROM jobs j LEFT JOIN stage_history h ON h.job_id = j.id
            WHERE j.stage = :stage
            GROUP BY j.id
        )"_s);
        query.bindValue(u":stage"_s, u"Applied"_s);
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            return 0;
        }
        while (query.next()) {
            // A job logged today for an application sent weeks ago only
            // starts its month today, and one moved back to Applied gets a
            // fresh month: whichever happened last counts.
            QDateTime lastActivity = QDateTime::fromString(query.value(3).toString(), Qt::ISODate);
            if (!lastActivity.isValid()) {
                lastActivity = QDateTime::fromString(query.value(2).toString(), Qt::ISODate);
            }
            const QDate applied = QDate::fromString(query.value(1).toString(), Qt::ISODate);
            if (applied.isValid() && (!lastActivity.isValid() || applied.startOfDay() > lastActivity)) {
                lastActivity = applied.startOfDay();
            }
            if (lastActivity.isValid() && lastActivity.addDays(days) < now) {
                stale.append(query.value(0).toInt());
            }
        }
    }

    int moved = 0;
    for (int id : std::as_const(stale)) {
        if (setStage(id, u"Ghosted"_s)) {
            ++moved;
        }
    }
    return moved;
}

QList<StageTransition> JobsDatabase::stageTransitions() const
{
    QList<StageTransition> transitions;
    if (!isOpen()) {
        return transitions;
    }
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(u"SELECT job_id, from_stage, to_stage, changed_at FROM stage_history ORDER BY changed_at ASC, id ASC"_s);
    if (!query.exec()) {
        return transitions;
    }
    while (query.next()) {
        StageTransition t;
        t.jobId = query.value(0).toInt();
        t.fromStage = query.value(1).toString();
        t.toStage = query.value(2).toString();
        t.changedAt = QDateTime::fromString(query.value(3).toString(), Qt::ISODate);
        transitions.append(t);
    }
    return transitions;
}

namespace
{
/// The job columns that travel as a sync record's "fields", by wire name.
/// Stage is not one of them: it is derived from the history.
const QStringList &syncFieldColumns()
{
    static const QStringList columns{
        u"company"_s,
        u"title"_s,
        u"location"_s,
        u"remote_type"_s,
        u"source"_s,
        u"url"_s,
        u"date_applied"_s,
        u"salary_min"_s,
        u"salary_max"_s,
        u"salary_expectation"_s,
        u"currency"_s,
        u"notes"_s,
        u"contact"_s,
        u"created_at"_s,
    };
    return columns;
}

bool isIntegerColumn(const QString &column)
{
    return column.startsWith(u"salary_"_s);
}

QDateTime parseStamp(const QJsonValue &value)
{
    return QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
}
}

QJsonArray JobsDatabase::pendingSyncRecords() const
{
    QJsonArray records;
    if (!isOpen()) {
        return records;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    query.prepare(u"SELECT * FROM jobs WHERE sync_dirty = 1"_s);
    if (!query.exec()) {
        return records;
    }
    while (query.next()) {
        QJsonObject fields;
        for (const QString &column : syncFieldColumns()) {
            const QVariant value = query.value(column);
            fields[column] = value.isNull() ? QJsonValue() : isIntegerColumn(column) ? QJsonValue(value.toInt()) : QJsonValue(value.toString());
        }
        QJsonArray history;
        for (const StageStep &step : stageHistory(query.value(u"id"_s).toInt())) {
            history.append(QJsonObject{{u"stage"_s, step.stage}, {u"at"_s, step.at.toUTC().toString(Qt::ISODate)}});
        }
        records.append(QJsonObject{
            {u"uuid"_s, query.value(u"uuid"_s).toString()},
            {u"fields_modified_at"_s, query.value(u"fields_modified_at"_s).toString()},
            {u"history_modified_at"_s, query.value(u"history_modified_at"_s).toString()},
            {u"fields"_s, fields},
            {u"history"_s, history},
        });
    }
    query.prepare(u"SELECT uuid FROM sync_tombstones"_s);
    if (query.exec()) {
        while (query.next()) {
            records.append(QJsonObject{{u"uuid"_s, query.value(0).toString()}, {u"deleted"_s, true}});
        }
    }
    return records;
}

bool JobsDatabase::hasPendingSyncRecords() const
{
    if (!isOpen()) {
        return false;
    }
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    return query.exec(u"SELECT EXISTS(SELECT 1 FROM jobs WHERE sync_dirty = 1) OR EXISTS(SELECT 1 FROM sync_tombstones)"_s) && query.next()
        && query.value(0).toBool();
}

qint64 JobsDatabase::syncCursor() const
{
    if (!isOpen()) {
        return 0;
    }
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (!query.exec(u"SELECT value FROM sync_state WHERE key = 'cursor'"_s) || !query.next()) {
        return 0;
    }
    return query.value(0).toLongLong();
}

int JobsDatabase::applySyncResult(const QJsonArray &pushed, const QJsonArray &pulled, qint64 cursor)
{
    if (!isOpen()) {
        return -1;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) {
        m_lastError = db.lastError().text();
        return -1;
    }
    const auto fail = [&](const QSqlQuery &query) {
        m_lastError = query.lastError().text();
        db.rollback();
        return -1;
    };

    QSqlQuery query(db);
    for (const QJsonValue &value : pushed) {
        const QJsonObject record = value.toObject();
        if (record[u"deleted"_s].toBool()) {
            query.prepare(u"DELETE FROM sync_tombstones WHERE uuid = :uuid"_s);
        } else {
            // A job edited again while the request was in flight stays dirty.
            query.prepare(
                u"UPDATE jobs SET sync_dirty = 0 WHERE uuid = :uuid "
                u"AND fields_modified_at = :fields_at AND history_modified_at = :history_at"_s);
            query.bindValue(u":fields_at"_s, record[u"fields_modified_at"_s].toString());
            query.bindValue(u":history_at"_s, record[u"history_modified_at"_s].toString());
        }
        query.bindValue(u":uuid"_s, record[u"uuid"_s].toString());
        if (!query.exec()) {
            return fail(query);
        }
    }

    int changedJobs = 0;
    for (const QJsonValue &value : pulled) {
        bool changed = false;
        if (!mergeSyncRecord(value.toObject(), changed)) {
            db.rollback();
            return -1;
        }
        changedJobs += changed ? 1 : 0;
    }

    query.prepare(u"INSERT OR REPLACE INTO sync_state (key, value) VALUES ('cursor', :cursor)"_s);
    query.bindValue(u":cursor"_s, QString::number(cursor));
    if (!query.exec()) {
        return fail(query);
    }
    if (!db.commit()) {
        m_lastError = db.lastError().text();
        db.rollback();
        return -1;
    }
    return changedJobs;
}

bool JobsDatabase::mergeSyncRecord(const QJsonObject &record, bool &changed)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    const QString uuid = record[u"uuid"_s].toString();
    if (uuid.isEmpty()) {
        return true;
    }

    if (record[u"deleted"_s].toBool()) {
        query.prepare(u"DELETE FROM jobs WHERE uuid = :uuid"_s);
        query.bindValue(u":uuid"_s, uuid);
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            return false;
        }
        changed = query.numRowsAffected() > 0;
        query.prepare(u"DELETE FROM sync_tombstones WHERE uuid = :uuid"_s);
        query.bindValue(u":uuid"_s, uuid);
        return query.exec();
    }

    QList<StageStep> history;
    for (const QJsonValue &step : record[u"history"_s].toArray()) {
        const QString stage = step[u"stage"_s].toString();
        const QDateTime at = parseStamp(step[u"at"_s]);
        if (JobStage::isValid(stage) && at.isValid()) {
            history.append({JobStage::canonical(stage), at});
        }
    }
    const QJsonObject fields = record[u"fields"_s].toObject();
    const QString fieldsAt = record[u"fields_modified_at"_s].toString();
    const QString historyAt = record[u"history_modified_at"_s].toString();

    int id = -1;
    QDateTime localFieldsAt;
    QDateTime localHistoryAt;
    query.prepare(u"SELECT id, fields_modified_at, history_modified_at FROM jobs WHERE uuid = :uuid"_s);
    query.bindValue(u":uuid"_s, uuid);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    if (query.next()) {
        id = query.value(0).toInt();
        localFieldsAt = parseStamp(query.value(1).toString());
        localHistoryAt = parseStamp(query.value(2).toString());
    } else {
        // Deleted here but not yet pushed: the delete goes up next and wins.
        query.prepare(u"SELECT 1 FROM sync_tombstones WHERE uuid = :uuid"_s);
        query.bindValue(u":uuid"_s, uuid);
        if (query.exec() && query.next()) {
            return true;
        }
        const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        query.prepare(
            u"INSERT INTO jobs (uuid, company, title, stage, created_at, updated_at, sync_dirty) "
            u"VALUES (:uuid, '', '', :stage, :now, :now, 0)"_s);
        query.bindValue(u":uuid"_s, uuid);
        query.bindValue(u":stage"_s, history.isEmpty() ? u"Applied"_s : history.last().stage);
        query.bindValue(u":now"_s, now);
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            return false;
        }
        id = query.lastInsertId().toInt();
    }

    const QDateTime remoteFieldsAt = parseStamp(fieldsAt);
    if (remoteFieldsAt.isValid() && (!localFieldsAt.isValid() || remoteFieldsAt > localFieldsAt)) {
        QStringList assignments;
        for (const QString &column : syncFieldColumns()) {
            assignments.append(u"%1 = :%1"_s.arg(column));
        }
        query.prepare(u"UPDATE jobs SET %1, fields_modified_at = :modified_at, updated_at = :now WHERE id = :id"_s.arg(assignments.join(u", "_s)));
        for (const QString &column : syncFieldColumns()) {
            const QJsonValue value = fields[column];
            QVariant bound;
            if (value.isDouble()) {
                bound = value.toInt();
            } else if (value.isString()) {
                bound = value.toString();
            } else if (column == u"company"_s || column == u"title"_s || column == u"created_at"_s) {
                bound = QString(); // NOT NULL columns
            }
            query.bindValue(u":"_s + column, bound);
        }
        query.bindValue(u":modified_at"_s, fieldsAt);
        query.bindValue(u":now"_s, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        query.bindValue(u":id"_s, id);
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            return false;
        }
        changed = true;
    }

    const QDateTime remoteHistoryAt = parseStamp(historyAt);
    if (!history.isEmpty() && remoteHistoryAt.isValid() && (!localHistoryAt.isValid() || remoteHistoryAt > localHistoryAt)) {
        if (!writeHistory(id, history)) {
            return false;
        }
        query.prepare(u"UPDATE jobs SET stage = :stage, history_modified_at = :modified_at WHERE id = :id"_s);
        query.bindValue(u":stage"_s, history.last().stage);
        query.bindValue(u":modified_at"_s, historyAt);
        query.bindValue(u":id"_s, id);
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            return false;
        }
        changed = true;
    }
    return true;
}

bool JobsDatabase::resetForSync()
{
    if (!isOpen()) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(db);
    for (const QString &statement : {
             u"DELETE FROM stage_history"_s,
             u"DELETE FROM jobs"_s,
             u"DELETE FROM sync_tombstones"_s,
             u"DELETE FROM sync_state"_s,
         }) {
        if (!query.exec(statement)) {
            m_lastError = query.lastError().text();
            return false;
        }
    }
    return true;
}
