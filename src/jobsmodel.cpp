// SPDX-License-Identifier: GPL-3.0-or-later
#include "jobsmodel.h"
#include "jobstage.h"

using namespace Qt::Literals::StringLiterals;

JobsModel::JobsModel(QObject *parent)
    : QAbstractListModel(parent)
{
    refresh();
}

void JobsModel::refresh()
{
    beginResetModel();
    m_jobs = m_db.allJobs();
    endResetModel();
    Q_EMIT countChanged();
}

int JobsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_jobs.size();
}

QVariant JobsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_jobs.size()) {
        return {};
    }
    const Job &job = m_jobs.at(index.row());
    switch (role) {
    case IdRole:
        return job.id;
    case CompanyRole:
        return job.company;
    case TitleRole:
        return job.title;
    case LocationRole:
        return job.location;
    case RemoteTypeRole:
        return job.remoteType;
    case SourceRole:
        return job.source;
    case UrlRole:
        return job.url;
    case DateAppliedRole:
        return job.dateApplied;
    case SalaryMinRole:
        return job.salaryMin;
    case SalaryMaxRole:
        return job.salaryMax;
    case SalaryExpectationRole:
        return job.salaryExpectation;
    case CurrencyRole:
        return job.currency;
    case NotesRole:
        return job.notes;
    case ContactRole:
        return job.contact;
    case StageRole:
        return job.stage;
    case StageColorRole:
        return JobStage::color(job.stage);
    case CreatedAtRole:
        return job.createdAt;
    case UpdatedAtRole:
        return job.updatedAt;
    default:
        return {};
    }
}

QHash<int, QByteArray> JobsModel::roleNames() const
{
    return {
        {IdRole, "jobId"},
        {CompanyRole, "company"},
        {TitleRole, "title"},
        {LocationRole, "location"},
        {RemoteTypeRole, "remoteType"},
        {SourceRole, "source"},
        {UrlRole, "url"},
        {DateAppliedRole, "dateApplied"},
        {SalaryMinRole, "salaryMin"},
        {SalaryMaxRole, "salaryMax"},
        {SalaryExpectationRole, "salaryExpectation"},
        {CurrencyRole, "currency"},
        {NotesRole, "notes"},
        {ContactRole, "contact"},
        {StageRole, "stage"},
        {StageColorRole, "stageColor"},
        {CreatedAtRole, "createdAt"},
        {UpdatedAtRole, "updatedAt"},
    };
}

QStringList JobsModel::stages() const
{
    return JobStage::canonicalStages();
}

Job JobsModel::jobFromMap(const QVariantMap &fields)
{
    Job job;
    job.company = fields.value(u"company"_s).toString();
    job.title = fields.value(u"title"_s).toString();
    job.location = fields.value(u"location"_s).toString();
    job.remoteType = fields.value(u"remoteType"_s).toString();
    job.source = fields.value(u"source"_s).toString();
    job.url = fields.value(u"url"_s).toString();

    const QVariant dateValue = fields.value(u"dateApplied"_s);
    job.dateApplied = dateValue.canConvert<QDate>() ? dateValue.toDate() : QDate::fromString(dateValue.toString(), Qt::ISODate);

    job.salaryMin = fields.value(u"salaryMin"_s, -1).toInt();
    job.salaryMax = fields.value(u"salaryMax"_s, -1).toInt();
    job.salaryExpectation = fields.value(u"salaryExpectation"_s, -1).toInt();
    job.currency = fields.value(u"currency"_s, u"USD"_s).toString();
    if (job.currency.isEmpty()) {
        job.currency = u"USD"_s;
    }
    job.notes = fields.value(u"notes"_s).toString();
    job.contact = fields.value(u"contact"_s).toString();
    job.stage = fields.value(u"stage"_s).toString();
    return job;
}

QVariantMap JobsModel::mapFromJob(const Job &job)
{
    return {
        {u"id"_s, job.id},
        {u"company"_s, job.company},
        {u"title"_s, job.title},
        {u"location"_s, job.location},
        {u"remoteType"_s, job.remoteType},
        {u"source"_s, job.source},
        {u"url"_s, job.url},
        {u"dateApplied"_s, job.dateApplied},
        {u"salaryMin"_s, job.salaryMin},
        {u"salaryMax"_s, job.salaryMax},
        {u"salaryExpectation"_s, job.salaryExpectation},
        {u"currency"_s, job.currency},
        {u"notes"_s, job.notes},
        {u"contact"_s, job.contact},
        {u"stage"_s, job.stage},
        {u"createdAt"_s, job.createdAt},
        {u"updatedAt"_s, job.updatedAt},
    };
}

QVariantMap JobsModel::jobData(int id) const
{
    const auto job = m_db.jobById(id);
    if (!job) {
        return {};
    }
    return mapFromJob(*job);
}

bool JobsModel::addJob(const QVariantMap &fields)
{
    Job job = jobFromMap(fields);
    const bool ok = m_db.addJob(job);
    if (ok) {
        refresh();
    }
    return ok;
}

bool JobsModel::updateJob(int id, const QVariantMap &fields)
{
    Job job = jobFromMap(fields);
    job.id = id;
    const bool ok = m_db.updateJob(job);
    if (ok) {
        refresh();
    }
    return ok;
}

bool JobsModel::setStage(int id, const QString &stage)
{
    const bool ok = m_db.setStage(id, stage);
    if (ok) {
        refresh();
    }
    return ok;
}

bool JobsModel::removeJob(int id)
{
    const bool ok = m_db.deleteJob(id);
    if (ok) {
        refresh();
    }
    return ok;
}

QString JobsModel::lastError() const
{
    return m_db.lastError();
}
