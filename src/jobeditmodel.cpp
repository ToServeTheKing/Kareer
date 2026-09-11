/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "jobeditmodel.h"

#include <KLocalizedString>

#include <QDate>

using namespace Qt::Literals::StringLiterals;
using JobFieldCatalog::ComboRow;
using JobFieldCatalog::DateRow;
using JobFieldCatalog::Field;
using JobFieldCatalog::SpinBoxRow;
using JobFieldCatalog::TextAreaRow;
using JobFieldCatalog::TextRow;

namespace
{
/// The three salary fields share the "0 shown in the UI means unset, -1 stored" convention.
bool isSalaryField(const QString &id)
{
    return id == QLatin1String("salaryMin") || id == QLatin1String("salaryMax") || id == QLatin1String("salaryExpectation");
}
}

JobEditModel::JobEditModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_history(new StageHistoryModel(this))
    , m_fields(JobFieldCatalog::fields())
{
    resetValues();
}

int JobEditModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_fields.size();
}

QVariant JobEditModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_fields.size()) {
        return {};
    }
    const Field &field = m_fields.at(index.row());
    switch (role) {
    case FieldIdRole:
        return field.id;
    case CategoryIdRole:
        return field.categoryId;
    case LabelRole:
        return field.label;
    case RowTypeRole:
        return static_cast<int>(field.rowType);
    case ValueRole:
        return m_values.value(field.id);
    case ComboOptionsRole:
        return field.comboOptions;
    case PlaceholderRole:
        return field.placeholder;
    case SpinMinRole:
        return field.spinMin;
    case SpinMaxRole:
        return field.spinMax;
    default:
        return {};
    }
}

QHash<int, QByteArray> JobEditModel::roleNames() const
{
    return {
        {FieldIdRole, "fieldId"},
        {CategoryIdRole, "categoryId"},
        {LabelRole, "label"},
        {RowTypeRole, "rowType"},
        {ValueRole, "value"},
        {ComboOptionsRole, "comboOptions"},
        {PlaceholderRole, "placeholder"},
        {SpinMinRole, "spinMin"},
        {SpinMaxRole, "spinMax"},
    };
}

JobsModel *JobEditModel::jobsModel() const
{
    return m_jobsModel;
}

void JobEditModel::setJobsModel(JobsModel *model)
{
    if (m_jobsModel == model) {
        return;
    }
    m_jobsModel = model;
    // QML does not guarantee editingJobId is assigned after jobsModel; if it
    // came first, the resetValues() it triggered had no model to load from
    // and only filled new-job defaults.
    resetValues();
    Q_EMIT jobsModelChanged();
}

int JobEditModel::editingJobId() const
{
    return m_editingJobId;
}

void JobEditModel::setEditingJobId(int id)
{
    if (m_editingJobId == id) {
        return;
    }
    m_editingJobId = id;
    resetValues();
    Q_EMIT editingJobIdChanged();
}

void JobEditModel::resetValues()
{
    m_values.clear();

    if (m_editingJobId < 0 || !m_jobsModel) {
        m_history->load({{u"Applied"_s, QDateTime(QDate::currentDate(), QTime(12, 0)).toUTC()}});
        // Every field needs a value of its own type: a missing one reaches
        // QML as undefined, which a text field shows as the word "undefined"
        // (typing "a" then gave "undefineda").
        for (const Field &field : m_fields) {
            switch (field.rowType) {
            case TextRow:
            case TextAreaRow:
                m_values[field.id] = QString();
                break;
            case ComboRow:
                m_values[field.id] = field.comboOptions.value(0);
                break;
            case SpinBoxRow:
                m_values[field.id] = 0;
                break;
            case DateRow:
                m_values[field.id] = QDate::currentDate();
                break;
            }
        }
        m_values[u"currency"_s] = u"USD"_s;
        m_values[u"remoteType"_s] = u"Unspecified"_s;
        m_values[u"salaryMin"_s] = 0;
        m_values[u"salaryMax"_s] = 0;
        m_values[u"salaryExpectation"_s] = 0;
    } else {
        const QVariantMap data = m_jobsModel->jobData(m_editingJobId);
        for (const Field &field : m_fields) {
            QVariant value = data.value(field.id);
            if (field.id == u"remoteType"_s && value.toString().isEmpty()) {
                value = u"Unspecified"_s;
            }
            if (isSalaryField(field.id) && value.toInt() < 0) {
                value = 0;
            }
            m_values[field.id] = value;
        }

        QList<StageStep> steps = m_jobsModel->stageHistory(m_editingJobId);
        if (steps.isEmpty()) {
            steps.append({data.value(u"stage"_s).toString(), data.value(u"createdAt"_s).toDateTime()});
        }
        // The first step's recorded time is when the job was logged; show the
        // date actually applied instead, which is what that step stands for.
        const QDate applied = data.value(u"dateApplied"_s).toDate();
        if (applied.isValid()) {
            steps.first().at = QDateTime(applied, QTime(12, 0)).toUTC();
        }
        m_history->load(steps);
    }

    if (rowCount() > 0) {
        Q_EMIT dataChanged(index(0), index(rowCount() - 1));
    }
}

QVariantList JobEditModel::categories() const
{
    QVariantList result;
    for (const JobFieldCatalog::Category &category : JobFieldCatalog::categories()) {
        result.append(QVariantMap{{u"id"_s, category.id}, {u"title"_s, category.title}});
    }
    return result;
}

QString JobEditModel::lastError() const
{
    return m_lastError;
}

StageHistoryModel *JobEditModel::history() const
{
    return m_history;
}

void JobEditModel::setValue(int row, const QVariant &value)
{
    if (row < 0 || row >= m_fields.size()) {
        return;
    }
    m_values[m_fields.at(row).id] = value;
    Q_EMIT dataChanged(index(row), index(row), {ValueRole});
}

bool JobEditModel::save()
{
    setLastError(QString());

    if (!m_jobsModel) {
        setLastError(i18n("Cannot save: no applications list is attached to this form."));
        return false;
    }

    if (m_values.value(u"company"_s).toString().trimmed().isEmpty()) {
        setLastError(i18n("Company is required."));
        return false;
    }
    if (m_values.value(u"title"_s).toString().trimmed().isEmpty()) {
        setLastError(i18n("Job title is required."));
        return false;
    }

    QVariantMap fields;
    for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it) {
        fields.insert(it.key(), it.value());
    }
    if (fields.value(u"remoteType"_s).toString() == u"Unspecified"_s) {
        fields[u"remoteType"_s] = QString();
    }
    for (const QString &id : {u"salaryMin"_s, u"salaryMax"_s, u"salaryExpectation"_s}) {
        if (fields.value(id).toInt() <= 0) {
            fields[id] = -1;
        }
    }

    // The history decides the current stage and the date applied.
    const QList<StageStep> steps = m_history->steps();
    fields[u"stage"_s] = steps.last().stage;
    fields[u"dateApplied"_s] = steps.first().at.toLocalTime().date();

    if (m_editingJobId < 0) {
        const int id = m_jobsModel->addJob(fields);
        // A new application's history is always written in full, so one
        // logged after the fact (Applied, Interview, Rejected) keeps its path.
        if (id < 0 || !m_jobsModel->replaceStageHistory(id, steps)) {
            setLastError(m_jobsModel->lastError());
            return false;
        }
        return true;
    }

    // updateJob() deliberately never writes the stage; stage changes only
    // ever come from the history.
    if (!m_jobsModel->updateJob(m_editingJobId, fields)) {
        setLastError(m_jobsModel->lastError());
        return false;
    }
    if (m_history->isEdited()) {
        if (!m_jobsModel->replaceStageHistory(m_editingJobId, steps)) {
            setLastError(m_jobsModel->lastError());
            return false;
        }
        m_history->load(m_jobsModel->stageHistory(m_editingJobId));
    }
    return true;
}

void JobEditModel::setLastError(const QString &error)
{
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    Q_EMIT lastErrorChanged();
}

bool JobEditModel::deleteJob()
{
    if (!m_jobsModel || m_editingJobId < 0) {
        return false;
    }
    return m_jobsModel->removeJob(m_editingJobId);
}
