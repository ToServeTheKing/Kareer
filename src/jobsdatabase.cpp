/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "jobsdatabase.h"
#include "jobstage.h"

#include <QAtomicInteger>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

using namespace Qt::Literals::StringLiterals;

namespace
{
QAtomicInteger<int> s_connectionCounter{0};

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
}

JobsDatabase::JobsDatabase()
{
    init(defaultPath());
}

JobsDatabase::JobsDatabase(const QString &path)
{
    init(path);
}

JobsDatabase::~JobsDatabase()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isValid()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

QString JobsDatabase::defaultPath()
{
    const QString overridePath = qEnvironmentVariable("KAREER_DB_PATH");
    if (!overridePath.isEmpty()) {
        return overridePath;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/kareer"_s;
    QDir().mkpath(dir);
    return dir + u"/kareer.sqlite"_s;
}

void JobsDatabase::init(const QString &path)
{
    m_connectionName = u"kareer_conn_%1"_s.arg(s_connectionCounter.fetchAndAddRelaxed(1));

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
    query.prepare(uR"(
        INSERT INTO jobs (company, title, location, remote_type, source, url, date_applied,
                           salary_min, salary_max, salary_expectation, currency, notes, contact,
                           stage, created_at, updated_at)
        VALUES (:company, :title, :location, :remote_type, :source, :url, :date_applied,
                :salary_min, :salary_max, :salary_expectation, :currency, :notes, :contact,
                :stage, :created_at, :updated_at)
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
    query.prepare(uR"(
        UPDATE jobs SET company = :company, title = :title, location = :location,
                        remote_type = :remote_type, source = :source, url = :url,
                        date_applied = :date_applied, salary_min = :salary_min,
                        salary_max = :salary_max, salary_expectation = :salary_expectation,
                        currency = :currency, notes = :notes, contact = :contact,
                        updated_at = :updated_at
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
    query.prepare(u"UPDATE jobs SET stage = :stage, updated_at = :updated_at WHERE id = :id"_s);
    query.bindValue(u":stage"_s, stage);
    query.bindValue(u":updated_at"_s, now.toString(Qt::ISODate));
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

QList<StageTransition> JobsDatabase::stageTransitions() const
{
    QList<StageTransition> transitions;
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
