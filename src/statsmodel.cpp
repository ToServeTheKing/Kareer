// SPDX-License-Identifier: GPL-3.0-or-later
#include "statsmodel.h"
#include "jobstage.h"

using namespace Qt::Literals::StringLiterals;

StatsModel::StatsModel(QObject *parent)
    : QObject(parent)
{
    refresh();
}

void StatsModel::refresh()
{
    m_jobs = m_db.allJobs();
    Q_EMIT changed();
}

int StatsModel::totalApplications() const
{
    return m_jobs.size();
}

int StatsModel::activeApplications() const
{
    int count = 0;
    for (const Job &job : m_jobs) {
        if (!JobStage::isTerminal(job.stage)) {
            ++count;
        }
    }
    return count;
}

int StatsModel::offerCount() const
{
    int count = 0;
    for (const Job &job : m_jobs) {
        if (job.stage == QLatin1String("Offer") || job.stage == QLatin1String("Accepted")) {
            ++count;
        }
    }
    return count;
}

int StatsModel::acceptedCount() const
{
    int count = 0;
    for (const Job &job : m_jobs) {
        if (job.stage == QLatin1String("Accepted")) {
            ++count;
        }
    }
    return count;
}

int StatsModel::rejectedCount() const
{
    int count = 0;
    for (const Job &job : m_jobs) {
        if (job.stage == QLatin1String("Rejected")) {
            ++count;
        }
    }
    return count;
}

double StatsModel::responseRate() const
{
    if (m_jobs.isEmpty()) {
        return 0.0;
    }
    int responded = 0;
    for (const Job &job : m_jobs) {
        if (job.stage != QLatin1String("Applied")) {
            ++responded;
        }
    }
    return 100.0 * responded / m_jobs.size();
}

double StatsModel::offerRate() const
{
    if (m_jobs.isEmpty()) {
        return 0.0;
    }
    return 100.0 * offerCount() / m_jobs.size();
}

QVariantMap StatsModel::stageCounts() const
{
    QVariantMap counts;
    for (const QString &stage : JobStage::canonicalStages()) {
        counts.insert(stage, 0);
    }
    for (const Job &job : m_jobs) {
        counts[job.stage] = counts.value(job.stage, 0).toInt() + 1;
    }
    return counts;
}
