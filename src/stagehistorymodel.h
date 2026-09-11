/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "job.h"

#include <QAbstractListModel>
#include <QQmlEngine>

/**
 * The editable stage history of one application, as shown in the edit
 * form's Pipeline section: one row per step (stage + date), kept in date
 * order. The last step is the application's current stage, and the first
 * step's date is its date applied. JobEditModel owns one and saves it with
 * JobsDatabase::replaceStageHistory().
 */
class StageHistoryModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by JobEditModel")

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QStringList stages READ stages CONSTANT)

public:
    enum Roles {
        StageRole = Qt::UserRole + 1,
        DateRole,
    };
    Q_ENUM(Roles)

    explicit StageHistoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QStringList stages() const;

    /// Replaces every step without marking the history as edited.
    void load(const QList<StageStep> &steps);
    QList<StageStep> steps() const;
    bool isEdited() const;

    Q_INVOKABLE void setStage(int row, const QString &stage);
    /// Takes a JS Date from QML; only its (local) calendar date is kept.
    Q_INVOKABLE void setDate(int row, const QDateTime &when);
    /// Appends a step dated today, defaulting to the stage that usually
    /// comes next.
    Q_INVOKABLE void appendStep();
    /// Removes a step; the last remaining step can't be removed.
    Q_INVOKABLE void removeStep(int row);

Q_SIGNALS:
    void countChanged();
    /// Emitted after any change made through the invokables.
    void edited();

private:
    void sortByDate();
    void markEdited();

    QList<StageStep> m_steps;
    bool m_edited = false;
};
