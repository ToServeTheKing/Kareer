/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "clicommands.h"

#include "job.h"
#include "jobsdatabase.h"
#include "jobstage.h"
#include "statsmodel.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>

using namespace Qt::Literals::StringLiterals;

namespace
{

QJsonValue optionalInt(int value)
{
    return value < 0 ? QJsonValue() : QJsonValue(value);
}

QJsonObject jobToJson(const Job &job)
{
    QJsonObject o;
    o["id"_L1] = job.id;
    o["company"_L1] = job.company;
    o["title"_L1] = job.title;
    o["location"_L1] = job.location;
    o["remoteType"_L1] = job.remoteType;
    o["source"_L1] = job.source;
    o["url"_L1] = job.url;
    o["dateApplied"_L1] = job.dateApplied.isValid() ? QJsonValue(job.dateApplied.toString(Qt::ISODate)) : QJsonValue();
    o["salaryMin"_L1] = optionalInt(job.salaryMin);
    o["salaryMax"_L1] = optionalInt(job.salaryMax);
    o["salaryExpectation"_L1] = optionalInt(job.salaryExpectation);
    o["currency"_L1] = job.currency;
    o["notes"_L1] = job.notes;
    o["contact"_L1] = job.contact;
    o["stage"_L1] = job.stage;
    o["createdAt"_L1] = job.createdAt.toString(Qt::ISODate);
    o["updatedAt"_L1] = job.updatedAt.toString(Qt::ISODate);
    return o;
}

void printJson(const QJsonValue &value)
{
    const QJsonDocument doc = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    QTextStream(stdout) << QString::fromUtf8(doc.toJson(QJsonDocument::Compact)) << Qt::endl;
}

void printJobHuman(const Job &job, QTextStream &out)
{
    out << u"#"_s << job.id << u"  "_s << job.company << u" — "_s << job.title << u"  ["_s << job.stage << u"]"_s << Qt::endl;
    if (!job.location.isEmpty() || !job.remoteType.isEmpty()) {
        out << u"  Location: "_s << job.location;
        if (!job.remoteType.isEmpty()) {
            out << u" ("_s << job.remoteType << u")"_s;
        }
        out << Qt::endl;
    }
    if (job.dateApplied.isValid()) {
        out << u"  Applied: "_s << job.dateApplied.toString(Qt::ISODate) << Qt::endl;
    }
    if (job.salaryMin >= 0 || job.salaryMax >= 0) {
        out << u"  Salary: "_s;
        if (job.salaryMin >= 0) {
            out << job.salaryMin;
        }
        if (job.salaryMin >= 0 && job.salaryMax >= 0) {
            out << u"–"_s;
        }
        if (job.salaryMax >= 0) {
            out << job.salaryMax;
        }
        out << u" "_s << job.currency << Qt::endl;
    }
    if (job.salaryExpectation >= 0) {
        out << u"  Expectation: "_s << job.salaryExpectation << u" "_s << job.currency << Qt::endl;
    }
    if (!job.source.isEmpty()) {
        out << u"  Source: "_s << job.source << Qt::endl;
    }
    if (!job.url.isEmpty()) {
        out << u"  URL: "_s << job.url << Qt::endl;
    }
    if (!job.contact.isEmpty()) {
        out << u"  Contact: "_s << job.contact << Qt::endl;
    }
    if (!job.notes.isEmpty()) {
        out << u"  Notes: "_s << job.notes << Qt::endl;
    }
}

bool normalizeRemoteType(const QString &input, QString &out, QString &error)
{
    if (input.isEmpty()) {
        out.clear();
        return true;
    }
    static const QStringList canonical{u"Onsite"_s, u"Hybrid"_s, u"Remote"_s};
    for (const QString &candidate : canonical) {
        if (candidate.compare(input, Qt::CaseInsensitive) == 0) {
            out = candidate;
            return true;
        }
    }
    error = u"Invalid --remote value '%1' (expected onsite, hybrid, or remote)"_s.arg(input);
    return false;
}

void addCommonJobOptions(QCommandLineParser &parser)
{
    parser.addOption({u"company"_s, u"Company name"_s, u"company"_s});
    parser.addOption({u"title"_s, u"Job title"_s, u"title"_s});
    parser.addOption({u"location"_s, u"Location"_s, u"location"_s});
    parser.addOption({u"remote"_s, u"Remote type: onsite, hybrid, or remote"_s, u"type"_s});
    parser.addOption({u"source"_s, u"Where this lead came from (referral, LinkedIn, ...)"_s, u"source"_s});
    parser.addOption({u"url"_s, u"Job posting URL"_s, u"url"_s});
    parser.addOption({u"date-applied"_s, u"Date applied, YYYY-MM-DD (default: today)"_s, u"date"_s});
    parser.addOption({u"salary-min"_s, u"Posted salary range minimum"_s, u"amount"_s});
    parser.addOption({u"salary-max"_s, u"Posted salary range maximum"_s, u"amount"_s});
    parser.addOption({u"salary-expectation"_s, u"Your stated salary expectation"_s, u"amount"_s});
    parser.addOption({u"currency"_s, u"Currency code (default: USD)"_s, u"code"_s});
    parser.addOption({u"notes"_s, u"Free-text notes / expectations"_s, u"text"_s});
    parser.addOption({u"contact"_s, u"Recruiter or contact name"_s, u"name"_s});
    parser.addOption({u"stage"_s, u"Pipeline stage (default: Applied)"_s, u"stage"_s});
    parser.addOption({u"json"_s, u"Print machine-readable JSON"_s});
}

bool parseSalaryOption(QCommandLineParser &parser, const QString &option, int &target, QTextStream &err)
{
    if (!parser.isSet(option)) {
        return true;
    }
    bool ok = false;
    const int value = parser.value(option).toInt(&ok);
    if (!ok || value < 0) {
        err << u"Error: --%1 must be a non-negative integer"_s.arg(option) << Qt::endl;
        return false;
    }
    target = value;
    return true;
}

int runAdd(const QString &program, const QStringList &args)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Add a new job application"_s);
    parser.addHelpOption();
    addCommonJobOptions(parser);
    parser.process(QStringList{program} + args);

    QTextStream err(stderr);

    if (!parser.isSet(u"company"_s) || !parser.isSet(u"title"_s)) {
        err << u"Error: --company and --title are required"_s << Qt::endl;
        return 1;
    }

    Job job;
    job.company = parser.value(u"company"_s);
    job.title = parser.value(u"title"_s);
    job.location = parser.value(u"location"_s);
    job.source = parser.value(u"source"_s);
    job.url = parser.value(u"url"_s);
    job.contact = parser.value(u"contact"_s);
    job.notes = parser.value(u"notes"_s);
    job.currency = parser.isSet(u"currency"_s) ? parser.value(u"currency"_s) : u"USD"_s;
    job.stage = parser.isSet(u"stage"_s) ? parser.value(u"stage"_s) : u"Applied"_s;

    QString remoteError;
    if (!normalizeRemoteType(parser.value(u"remote"_s), job.remoteType, remoteError)) {
        err << remoteError << Qt::endl;
        return 1;
    }

    if (parser.isSet(u"date-applied"_s)) {
        job.dateApplied = QDate::fromString(parser.value(u"date-applied"_s), Qt::ISODate);
        if (!job.dateApplied.isValid()) {
            err << u"Error: --date-applied must be YYYY-MM-DD"_s << Qt::endl;
            return 1;
        }
    } else {
        job.dateApplied = QDate::currentDate();
    }

    if (!parseSalaryOption(parser, u"salary-min"_s, job.salaryMin, err) || !parseSalaryOption(parser, u"salary-max"_s, job.salaryMax, err)
        || !parseSalaryOption(parser, u"salary-expectation"_s, job.salaryExpectation, err)) {
        return 1;
    }

    if (!JobStage::isValid(job.stage)) {
        err << u"Error: unknown stage '%1'. Valid stages: %2"_s.arg(job.stage, JobStage::canonicalStages().join(u", "_s)) << Qt::endl;
        return 1;
    }

    JobsDatabase db;
    if (!db.addJob(job)) {
        err << u"Error: %1"_s.arg(db.lastError()) << Qt::endl;
        return 1;
    }

    if (parser.isSet(u"json"_s)) {
        printJson(jobToJson(job));
    } else {
        QTextStream(stdout) << u"Added application #%1: %2 — %3 (%4)"_s.arg(job.id).arg(job.company, job.title, job.stage) << Qt::endl;
    }
    return 0;
}

int runList(const QString &program, const QStringList &args)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(u"List job applications"_s);
    parser.addHelpOption();
    parser.addOption({u"stage"_s, u"Filter by stage"_s, u"stage"_s});
    parser.addOption({u"company"_s, u"Filter by company (substring match)"_s, u"text"_s});
    parser.addOption({u"json"_s, u"Print machine-readable JSON"_s});
    parser.process(QStringList{program} + args);

    JobsDatabase db;
    QList<Job> jobs = db.allJobs();

    if (parser.isSet(u"stage"_s)) {
        const QString stage = parser.value(u"stage"_s);
        jobs.removeIf([&](const Job &j) {
            return j.stage.compare(stage, Qt::CaseInsensitive) != 0;
        });
    }
    if (parser.isSet(u"company"_s)) {
        const QString needle = parser.value(u"company"_s);
        jobs.removeIf([&](const Job &j) {
            return !j.company.contains(needle, Qt::CaseInsensitive);
        });
    }

    if (parser.isSet(u"json"_s)) {
        QJsonArray arr;
        for (const Job &j : std::as_const(jobs)) {
            arr.append(jobToJson(j));
        }
        printJson(arr);
        return 0;
    }

    QTextStream out(stdout);
    if (jobs.isEmpty()) {
        out << u"No applications found."_s << Qt::endl;
        return 0;
    }
    for (const Job &j : std::as_const(jobs)) {
        out << u"#"_s << j.id << u"  "_s << j.company << u" — "_s << j.title << u"  ["_s << j.stage << u"]  "_s
            << (j.dateApplied.isValid() ? j.dateApplied.toString(Qt::ISODate) : u"?"_s) << Qt::endl;
    }
    return 0;
}

int runShow(const QString &program, const QStringList &args)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Show one job application"_s);
    parser.addHelpOption();
    parser.addOption({u"json"_s, u"Print machine-readable JSON"_s});
    parser.addPositionalArgument(u"id"_s, u"Application id"_s);
    parser.process(QStringList{program} + args);

    QTextStream err(stderr);
    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        err << u"Error: missing application id"_s << Qt::endl;
        return 1;
    }
    bool ok = false;
    const int id = positional.first().toInt(&ok);
    if (!ok) {
        err << u"Error: id must be an integer"_s << Qt::endl;
        return 1;
    }

    JobsDatabase db;
    const auto job = db.jobById(id);
    if (!job) {
        err << u"Error: no application #%1"_s.arg(id) << Qt::endl;
        return 1;
    }

    if (parser.isSet(u"json"_s)) {
        printJson(jobToJson(*job));
    } else {
        QTextStream out(stdout);
        printJobHuman(*job, out);
    }
    return 0;
}

int runUpdate(const QString &program, const QStringList &args)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Update fields on an existing application"_s);
    parser.addHelpOption();
    addCommonJobOptions(parser);
    parser.addPositionalArgument(u"id"_s, u"Application id"_s);
    parser.process(QStringList{program} + args);

    QTextStream err(stderr);
    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        err << u"Error: missing application id"_s << Qt::endl;
        return 1;
    }
    bool ok = false;
    const int id = positional.first().toInt(&ok);
    if (!ok) {
        err << u"Error: id must be an integer"_s << Qt::endl;
        return 1;
    }

    JobsDatabase db;
    const auto existing = db.jobById(id);
    if (!existing) {
        err << u"Error: no application #%1"_s.arg(id) << Qt::endl;
        return 1;
    }

    Job job = *existing;
    if (parser.isSet(u"company"_s)) {
        job.company = parser.value(u"company"_s);
    }
    if (parser.isSet(u"title"_s)) {
        job.title = parser.value(u"title"_s);
    }
    if (parser.isSet(u"location"_s)) {
        job.location = parser.value(u"location"_s);
    }
    if (parser.isSet(u"source"_s)) {
        job.source = parser.value(u"source"_s);
    }
    if (parser.isSet(u"url"_s)) {
        job.url = parser.value(u"url"_s);
    }
    if (parser.isSet(u"contact"_s)) {
        job.contact = parser.value(u"contact"_s);
    }
    if (parser.isSet(u"notes"_s)) {
        job.notes = parser.value(u"notes"_s);
    }
    if (parser.isSet(u"currency"_s)) {
        job.currency = parser.value(u"currency"_s);
    }
    if (parser.isSet(u"remote"_s)) {
        QString remoteError;
        if (!normalizeRemoteType(parser.value(u"remote"_s), job.remoteType, remoteError)) {
            err << remoteError << Qt::endl;
            return 1;
        }
    }
    if (parser.isSet(u"date-applied"_s)) {
        const QDate d = QDate::fromString(parser.value(u"date-applied"_s), Qt::ISODate);
        if (!d.isValid()) {
            err << u"Error: --date-applied must be YYYY-MM-DD"_s << Qt::endl;
            return 1;
        }
        job.dateApplied = d;
    }
    if (!parseSalaryOption(parser, u"salary-min"_s, job.salaryMin, err) || !parseSalaryOption(parser, u"salary-max"_s, job.salaryMax, err)
        || !parseSalaryOption(parser, u"salary-expectation"_s, job.salaryExpectation, err)) {
        return 1;
    }

    if (!db.updateJob(job)) {
        err << u"Error: %1"_s.arg(db.lastError()) << Qt::endl;
        return 1;
    }

    if (parser.isSet(u"stage"_s)) {
        const QString stage = parser.value(u"stage"_s);
        if (!JobStage::isValid(stage)) {
            err << u"Error: unknown stage '%1'. Valid stages: %2"_s.arg(stage, JobStage::canonicalStages().join(u", "_s)) << Qt::endl;
            return 1;
        }
        if (!db.setStage(id, stage)) {
            err << u"Error: %1"_s.arg(db.lastError()) << Qt::endl;
            return 1;
        }
    }

    const auto updated = db.jobById(id);
    if (parser.isSet(u"json"_s)) {
        printJson(jobToJson(*updated));
    } else {
        QTextStream(stdout) << u"Updated application #%1"_s.arg(id) << Qt::endl;
    }
    return 0;
}

int runStage(const QString &program, const QStringList &args)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Move an application to a new stage"_s);
    parser.addHelpOption();
    parser.addOption({u"json"_s, u"Print machine-readable JSON"_s});
    parser.addPositionalArgument(u"id"_s, u"Application id"_s);
    parser.addPositionalArgument(u"stage"_s, u"New stage: %1"_s.arg(JobStage::canonicalStages().join(u", "_s)));
    parser.process(QStringList{program} + args);

    QTextStream err(stderr);
    const QStringList positional = parser.positionalArguments();
    if (positional.size() < 2) {
        err << u"Error: usage: kareer stage <id> <stage>"_s << Qt::endl;
        return 1;
    }
    bool ok = false;
    const int id = positional.at(0).toInt(&ok);
    if (!ok) {
        err << u"Error: id must be an integer"_s << Qt::endl;
        return 1;
    }
    const QString stage = positional.at(1);
    if (!JobStage::isValid(stage)) {
        err << u"Error: unknown stage '%1'. Valid stages: %2"_s.arg(stage, JobStage::canonicalStages().join(u", "_s)) << Qt::endl;
        return 1;
    }

    JobsDatabase db;
    if (!db.setStage(id, stage)) {
        err << u"Error: %1"_s.arg(db.lastError()) << Qt::endl;
        return 1;
    }

    const auto job = db.jobById(id);
    if (parser.isSet(u"json"_s)) {
        printJson(jobToJson(*job));
    } else {
        QTextStream(stdout) << u"Application #%1 moved to %2"_s.arg(id).arg(stage) << Qt::endl;
    }
    return 0;
}

int runDelete(const QString &program, const QStringList &args)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Delete an application"_s);
    parser.addHelpOption();
    parser.addOption({u"yes"_s, u"Confirm deletion"_s});
    parser.addPositionalArgument(u"id"_s, u"Application id"_s);
    parser.process(QStringList{program} + args);

    QTextStream err(stderr);
    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        err << u"Error: missing application id"_s << Qt::endl;
        return 1;
    }
    bool ok = false;
    const int id = positional.first().toInt(&ok);
    if (!ok) {
        err << u"Error: id must be an integer"_s << Qt::endl;
        return 1;
    }

    if (!parser.isSet(u"yes"_s)) {
        err << u"Refusing to delete application #%1 without --yes"_s.arg(id) << Qt::endl;
        return 1;
    }

    JobsDatabase db;
    if (!db.deleteJob(id)) {
        err << u"Error: %1"_s.arg(db.lastError()) << Qt::endl;
        return 1;
    }

    QTextStream(stdout) << u"Deleted application #%1"_s.arg(id) << Qt::endl;
    return 0;
}

int runStats(const QString &program, const QStringList &args)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Summary statistics across all applications"_s);
    parser.addHelpOption();
    parser.addOption({u"json"_s, u"Print machine-readable JSON"_s});
    parser.process(QStringList{program} + args);

    StatsModel stats;
    const QVariantMap counts = stats.stageCounts();

    if (parser.isSet(u"json"_s)) {
        QJsonObject o;
        o["totalApplications"_L1] = stats.totalApplications();
        o["activeApplications"_L1] = stats.activeApplications();
        o["offerCount"_L1] = stats.offerCount();
        o["acceptedCount"_L1] = stats.acceptedCount();
        o["rejectedCount"_L1] = stats.rejectedCount();
        o["responseRate"_L1] = stats.responseRate();
        o["offerRate"_L1] = stats.offerRate();
        QJsonObject stageCounts;
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
            stageCounts[it.key()] = it.value().toInt();
        }
        o["stageCounts"_L1] = stageCounts;
        printJson(o);
        return 0;
    }

    QTextStream out(stdout);
    out << u"Total applications: %1"_s.arg(stats.totalApplications()) << Qt::endl;
    out << u"Active: %1"_s.arg(stats.activeApplications()) << Qt::endl;
    out << u"Offers: %1   Accepted: %2   Rejected: %3"_s.arg(stats.offerCount()).arg(stats.acceptedCount()).arg(stats.rejectedCount()) << Qt::endl;
    out << u"Response rate: %1%   Offer rate: %2%"_s.arg(stats.responseRate(), 0, 'f', 1).arg(stats.offerRate(), 0, 'f', 1) << Qt::endl;
    out << u"By stage:"_s << Qt::endl;
    for (const QString &stage : JobStage::canonicalStages()) {
        out << u"  %1: %2"_s.arg(stage, -12).arg(counts.value(stage).toInt()) << Qt::endl;
    }
    return 0;
}

int runStages(const QString &program, const QStringList &args)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(u"List the canonical pipeline stages"_s);
    parser.addHelpOption();
    parser.addOption({u"json"_s, u"Print machine-readable JSON"_s});
    parser.process(QStringList{program} + args);

    if (parser.isSet(u"json"_s)) {
        QJsonArray arr;
        for (const QString &s : JobStage::canonicalStages()) {
            arr.append(s);
        }
        printJson(arr);
        return 0;
    }

    QTextStream out(stdout);
    for (const QString &s : JobStage::canonicalStages()) {
        out << s << Qt::endl;
    }
    return 0;
}

int runHelp()
{
    QTextStream out(stdout);
    out << u"Usage: kareer <command> [options]\n\n"_s
        << u"Commands:\n"_s
        << u"  add      Add a new job application\n"_s
        << u"  list     List job applications\n"_s
        << u"  show     Show one job application\n"_s
        << u"  update   Update fields on an existing application\n"_s
        << u"  stage    Move an application to a new stage\n"_s
        << u"  delete   Delete an application\n"_s
        << u"  stats    Summary statistics\n"_s
        << u"  stages   List the canonical pipeline stages\n\n"_s
        << u"Run 'kareer <command> --help' for the options of a specific command.\n"_s
        << u"Running kareer with no command (or an unrecognized one) starts the GUI.\n"_s;
    return 0;
}

}

bool Cli::isSubcommand(const QString &arg)
{
    static const QSet<QString> subcommands{
        u"add"_s, u"list"_s, u"show"_s, u"update"_s, u"stage"_s, u"delete"_s, u"stats"_s, u"stages"_s, u"help"_s,
    };
    return subcommands.contains(arg);
}

int Cli::run(QCoreApplication &app)
{
    const QStringList allArgs = app.arguments();
    const QString program = allArgs.value(0);
    const QString subcommand = allArgs.value(1);
    const QStringList rest = allArgs.mid(2);

    if (subcommand == u"add"_s) {
        return runAdd(program, rest);
    }
    if (subcommand == u"list"_s) {
        return runList(program, rest);
    }
    if (subcommand == u"show"_s) {
        return runShow(program, rest);
    }
    if (subcommand == u"update"_s) {
        return runUpdate(program, rest);
    }
    if (subcommand == u"stage"_s) {
        return runStage(program, rest);
    }
    if (subcommand == u"delete"_s) {
        return runDelete(program, rest);
    }
    if (subcommand == u"stats"_s) {
        return runStats(program, rest);
    }
    if (subcommand == u"stages"_s) {
        return runStages(program, rest);
    }
    return runHelp();
}
