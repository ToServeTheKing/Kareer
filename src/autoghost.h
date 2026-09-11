/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QTimer>

/**
 * Marks applications with no response as Ghosted: anything still at
 * Applied whose last activity is more than days() old (see
 * JobsDatabase::ghostStaleApplications()). The switch and the threshold live
 * in kareerrc ([AutoGhost] Enabled/Days) and are shown on the Preferences
 * page.
 *
 * runNow() is called at startup for both the GUI and the CLI; the GUI runs
 * it again after switching databases and shortly after the settings change,
 * and refreshes the models when ran() reports moved applications.
 */
class AutoGhost : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int days READ days WRITE setDays NOTIFY settingsChanged)
    /// How many applications the most recent run moved to Ghosted.
    Q_PROPERTY(int lastRunCount READ lastRunCount NOTIFY ran)

public:
    static constexpr int DefaultDays = 30;

    explicit AutoGhost(QObject *parent = nullptr);

    /// Ghosts stale applications in the current database if enabled, and
    /// returns how many were moved.
    static int runNow();

    bool enabled() const;
    void setEnabled(bool enabled);
    int days() const;
    void setDays(int days);
    int lastRunCount() const;

    /// runNow(), then emits ran() with the number of applications moved.
    Q_INVOKABLE int run();

Q_SIGNALS:
    void settingsChanged();
    void ran(int count);

private:
    /// Settings changes run shortly after they settle, so stepping the
    /// threshold down doesn't ghost applications at every value in between.
    QTimer m_settleTimer;
};
