/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "job.h"
#include "jobsdatabase.h"

#include <QAbstractListModel>
#include <QList>
#include <QQmlEngine>

/**
 * List of all job applications, newest first, backed by JobsDatabase.
 * QML reads jobs through model roles and writes through the invokable
 * methods, which go straight to the database and then refresh in place.
 */
class JobsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QStringList stages READ stages CONSTANT)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        CompanyRole,
        TitleRole,
        LocationRole,
        RemoteTypeRole,
        SourceRole,
        UrlRole,
        DateAppliedRole,
        SalaryMinRole,
        SalaryMaxRole,
        SalaryExpectationRole,
        CurrencyRole,
        NotesRole,
        ContactRole,
        StageRole,
        StageColorRole,
        CreatedAtRole,
        UpdatedAtRole,
    };
    Q_ENUM(Roles)

    explicit JobsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QStringList stages() const;

    /// Full record for one job, for prefilling the edit dialog.
    Q_INVOKABLE QVariantMap jobData(int id) const;

    Q_INVOKABLE bool addJob(const QVariantMap &fields);
    Q_INVOKABLE bool updateJob(int id, const QVariantMap &fields);
    Q_INVOKABLE bool setStage(int id, const QString &stage);
    Q_INVOKABLE bool removeJob(int id);

    Q_INVOKABLE QString lastError() const;

public Q_SLOTS:
    void refresh();

Q_SIGNALS:
    void countChanged();

private:
    static Job jobFromMap(const QVariantMap &fields);
    static QVariantMap mapFromJob(const Job &job);

    JobsDatabase m_db;
    QList<Job> m_jobs;
};
