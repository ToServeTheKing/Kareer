/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "stagehistorymodel.h"

#include "jobstage.h"

#include <algorithm>

using namespace Qt::Literals::StringLiterals;

namespace
{
/// Midday local time, so the calendar date survives time-zone conversion.
QDateTime atMidday(const QDate &date)
{
    return QDateTime(date, QTime(12, 0)).toUTC();
}

QString nextStage(const QString &stage)
{
    static const QStringList funnel{u"Applied"_s, u"Screening"_s, u"Interview"_s, u"Onsite"_s, u"Offer"_s, u"Accepted"_s};
    if (JobStage::isTerminal(stage)) {
        return u"Screening"_s; // a closed application reopening
    }
    const int index = funnel.indexOf(stage);
    return funnel.value(index + 1, u"Rejected"_s);
}
}

StageHistoryModel::StageHistoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int StageHistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_steps.size();
}

QVariant StageHistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_steps.size()) {
        return {};
    }
    const StageStep &step = m_steps.at(index.row());
    switch (role) {
    case StageRole:
        return step.stage;
    case DateRole:
        return step.at.toLocalTime().date();
    default:
        return {};
    }
}

QHash<int, QByteArray> StageHistoryModel::roleNames() const
{
    return {{StageRole, "stage"}, {DateRole, "date"}};
}

QStringList StageHistoryModel::stages() const
{
    return JobStage::canonicalStages();
}

void StageHistoryModel::load(const QList<StageStep> &steps)
{
    beginResetModel();
    m_steps = steps;
    std::stable_sort(m_steps.begin(), m_steps.end(), [](const StageStep &a, const StageStep &b) {
        return a.at < b.at;
    });
    endResetModel();
    m_edited = false;
    Q_EMIT countChanged();
}

QList<StageStep> StageHistoryModel::steps() const
{
    return m_steps;
}

bool StageHistoryModel::isEdited() const
{
    return m_edited;
}

void StageHistoryModel::setStage(int row, const QString &stage)
{
    if (row < 0 || row >= m_steps.size() || !JobStage::isValid(stage) || m_steps.at(row).stage == stage) {
        return;
    }
    m_steps[row].stage = JobStage::canonical(stage);
    Q_EMIT dataChanged(index(row), index(row), {StageRole});
    markEdited();
}

void StageHistoryModel::setDate(int row, const QDateTime &when)
{
    if (row < 0 || row >= m_steps.size() || !when.isValid()) {
        return;
    }
    const QDate date = when.toLocalTime().date();
    if (m_steps.at(row).at.toLocalTime().date() == date) {
        return;
    }
    m_steps[row].at = atMidday(date);
    sortByDate();
    markEdited();
}

void StageHistoryModel::appendStep()
{
    const QString stage = m_steps.isEmpty() ? u"Applied"_s : nextStage(m_steps.last().stage);
    // Never earlier than the current last step, so the new step stays last.
    QDateTime at = atMidday(QDate::currentDate());
    if (!m_steps.isEmpty() && m_steps.last().at > at) {
        at = m_steps.last().at;
    }
    beginInsertRows({}, m_steps.size(), m_steps.size());
    m_steps.append({stage, at});
    endInsertRows();
    Q_EMIT countChanged();
    markEdited();
}

void StageHistoryModel::removeStep(int row)
{
    if (row < 0 || row >= m_steps.size() || m_steps.size() <= 1) {
        return;
    }
    beginRemoveRows({}, row, row);
    m_steps.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
    markEdited();
}

void StageHistoryModel::sortByDate()
{
    beginResetModel();
    std::stable_sort(m_steps.begin(), m_steps.end(), [](const StageStep &a, const StageStep &b) {
        return a.at < b.at;
    });
    endResetModel();
}

void StageHistoryModel::markEdited()
{
    m_edited = true;
    Q_EMIT edited();
}
