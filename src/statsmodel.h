// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "jobsdatabase.h"

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

/**
 * Summary counters for the dashboard, computed on demand from JobsDatabase.
 * QML calls refresh() whenever the underlying jobs may have changed (the
 * dashboard becoming visible is enough - this is cheap for the data sizes a
 * personal tracker deals with).
 */
class StatsModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int totalApplications READ totalApplications NOTIFY changed)
    Q_PROPERTY(int activeApplications READ activeApplications NOTIFY changed)
    Q_PROPERTY(int offerCount READ offerCount NOTIFY changed)
    Q_PROPERTY(int acceptedCount READ acceptedCount NOTIFY changed)
    Q_PROPERTY(int rejectedCount READ rejectedCount NOTIFY changed)
    Q_PROPERTY(double responseRate READ responseRate NOTIFY changed)
    Q_PROPERTY(double offerRate READ offerRate NOTIFY changed)
    Q_PROPERTY(QVariantMap stageCounts READ stageCounts NOTIFY changed)

public:
    explicit StatsModel(QObject *parent = nullptr);

    int totalApplications() const;
    int activeApplications() const;
    int offerCount() const;
    int acceptedCount() const;
    int rejectedCount() const;
    double responseRate() const;
    double offerRate() const;
    QVariantMap stageCounts() const;

public Q_SLOTS:
    void refresh();

Q_SIGNALS:
    void changed();

private:
    JobsDatabase m_db;
    QList<Job> m_jobs;
};
