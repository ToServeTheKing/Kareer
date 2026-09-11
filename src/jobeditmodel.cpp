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
    m_loadedStage.clear();

    if (m_editingJobId < 0 || !m_jobsModel) {
        m_values[u"currency"_s] = u"USD"_s;
        m_values[u"stage"_s] = u"Applied"_s;
        m_values[u"dateApplied"_s] = QDate::currentDate();
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
        m_loadedStage = m_values.value(u"stage"_s).toString();
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

    if (m_editingJobId < 0) {
        if (!m_jobsModel->addJob(fields)) {
            setLastError(m_jobsModel->lastError());
            return false;
        }
        return true;
    }

    if (!m_jobsModel->updateJob(m_editingJobId, fields)) {
        setLastError(m_jobsModel->lastError());
        return false;
    }

    // updateJob() deliberately never writes the stage (so every stage change
    // lands in stage_history); route a changed stage through setStage().
    const QString stage = fields.value(u"stage"_s).toString();
    if (!stage.isEmpty() && stage != m_loadedStage) {
        if (!m_jobsModel->setStage(m_editingJobId, stage)) {
            setLastError(m_jobsModel->lastError());
            return false;
        }
        m_loadedStage = stage;
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
