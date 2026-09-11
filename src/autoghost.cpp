/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "autoghost.h"

#include "jobsdatabase.h"

#include <KConfigGroup>
#include <KSharedConfig>

using namespace Qt::Literals::StringLiterals;

namespace
{
int s_lastRunCount = 0;

KConfigGroup settingsGroup()
{
    return KConfigGroup(KSharedConfig::openConfig(u"kareerrc"_s), u"AutoGhost"_s);
}
}

AutoGhost::AutoGhost(QObject *parent)
    : QObject(parent)
{
    m_settleTimer.setSingleShot(true);
    m_settleTimer.setInterval(1500);
    connect(&m_settleTimer, &QTimer::timeout, this, &AutoGhost::run);
}

int AutoGhost::runNow()
{
    const KConfigGroup group = settingsGroup();
    if (!group.readEntry("Enabled", true)) {
        s_lastRunCount = 0;
        return 0;
    }
    JobsDatabase db;
    s_lastRunCount = db.ghostStaleApplications(group.readEntry("Days", DefaultDays));
    return s_lastRunCount;
}

bool AutoGhost::enabled() const
{
    return settingsGroup().readEntry("Enabled", true);
}

void AutoGhost::setEnabled(bool enabled)
{
    if (enabled == this->enabled()) {
        return;
    }
    KConfigGroup group = settingsGroup();
    group.writeEntry("Enabled", enabled);
    group.sync();
    Q_EMIT settingsChanged();
    m_settleTimer.start();
}

int AutoGhost::days() const
{
    return settingsGroup().readEntry("Days", DefaultDays);
}

void AutoGhost::setDays(int days)
{
    days = qMax(1, days);
    if (days == this->days()) {
        return;
    }
    KConfigGroup group = settingsGroup();
    group.writeEntry("Days", days);
    group.sync();
    Q_EMIT settingsChanged();
    m_settleTimer.start();
}

int AutoGhost::lastRunCount() const
{
    return s_lastRunCount;
}

int AutoGhost::run()
{
    m_settleTimer.stop();
    const int count = runNow();
    Q_EMIT ran(count);
    return count;
}
