// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

/**
 * The fixed set of stages an application can be in. Kept as a small closed
 * vocabulary (rather than free text) so the Sankey diagram's columns and
 * stacking order stay stable no matter what data is loaded.
 */
namespace JobStage
{
/// Stages in pipeline order. "Start" is a synthetic node (not a real stage,
/// never stored on a Job) representing "before the first recorded stage".
constexpr auto Start = "Start";

QStringList canonicalStages();

bool isValid(const QString &stage);

/// Sankey column index. Start = 0; Applied..Offer walk the funnel; the three
/// terminal outcomes (Accepted/Rejected/Withdrawn/Ghosted) share the last
/// column so a rejection right after Applied is still a valid (longer) link.
int column(const QString &stage);

/// Stacking order of nodes within a column (top to bottom).
int orderInColumn(const QString &stage);

/// Stable color used for both the node box and its outgoing links.
QColor color(const QString &stage);

bool isTerminal(const QString &stage);
}
