/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "jobfieldcatalog.h"
#include "jobsmodel.h"
#include "stagehistorymodel.h"

#include <QAbstractListModel>
#include <QHash>
#include <QQmlEngine>
#include <QVariant>

/**
 * Drives the add/edit form the way FlatKontrol's PermissionsController drives
 * PermissionsPage: a generic row model (one row per JobFieldCatalog::Field)
 * that QML renders via Repeater + DelegateChooser on rowType, instead of
 * each field being hand-written in QML.
 */
class JobEditModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(JobsModel *jobsModel READ jobsModel WRITE setJobsModel NOTIFY jobsModelChanged)
    Q_PROPERTY(int editingJobId READ editingJobId WRITE setEditingJobId NOTIFY editingJobIdChanged)
    Q_PROPERTY(QVariantList categories READ categories CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    /// The application's stage history (the Pipeline section): the last step
    /// is its current stage, the first step's date its date applied.
    Q_PROPERTY(StageHistoryModel *history READ history CONSTANT)

public:
    enum Roles {
        FieldIdRole = Qt::UserRole + 1,
        CategoryIdRole,
        LabelRole,
        RowTypeRole,
        ValueRole,
        ComboOptionsRole,
        PlaceholderRole,
        SpinMinRole,
        SpinMaxRole,
    };
    Q_ENUM(Roles)

    explicit JobEditModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    JobsModel *jobsModel() const;
    void setJobsModel(JobsModel *model);

    int editingJobId() const;
    void setEditingJobId(int id);

    QVariantList categories() const;
    QString lastError() const;
    StageHistoryModel *history() const;

    /// Updates the value for the field at this row (called from the QML delegate).
    Q_INVOKABLE void setValue(int row, const QVariant &value);

    /// Persists the current values via jobsModel; true on success.
    Q_INVOKABLE bool save();

    Q_INVOKABLE bool deleteJob();

Q_SIGNALS:
    void jobsModelChanged();
    void editingJobIdChanged();
    void lastErrorChanged();

private:
    void resetValues();
    void setLastError(const QString &error);

    JobsModel *m_jobsModel = nullptr;
    int m_editingJobId = -1;
    QHash<QString, QVariant> m_values;
    QString m_lastError;
    StageHistoryModel *m_history = nullptr;
    QList<JobFieldCatalog::Field> m_fields;
};
