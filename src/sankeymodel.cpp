/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "sankeymodel.h"
#include "jobstage.h"

#include <KLocalizedString>

#include <QHash>
#include <QPair>
#include <QSet>
#include <QVariantMap>
#include <algorithm>
#include <iterator>
#include <numeric>

using namespace Qt::Literals::StringLiterals;

namespace
{
QString num(qreal v)
{
    return QString::number(v, 'f', 2);
}

QString point(qreal x, qreal y)
{
    return num(x) + u","_s + num(y);
}

struct NodeInfo {
    QString stage;
    int column = 0;
    int order = 0;
    int value = 0;
    qreal x = 0;
    qreal y = 0;
    qreal width = 0;
    qreal height = 0;
};
}

SankeyModel::SankeyModel(QObject *parent)
    : QObject(parent)
{
}

QVariantList SankeyModel::nodes() const
{
    return m_nodes;
}

QVariantList SankeyModel::links() const
{
    return m_links;
}

bool SankeyModel::isEmpty() const
{
    return m_nodes.isEmpty();
}

void SankeyModel::reload(qreal width, qreal height, qreal nodeWidth, qreal padding)
{
    m_counts.clear();
    const QList<StageTransition> transitions = m_db.stageTransitions();
    for (const StageTransition &t : transitions) {
        const QString from = t.fromStage.isEmpty() ? QString::fromLatin1(JobStage::Start) : t.fromStage;
        m_counts[{from, t.toStage}] += 1;
    }
    relayout(width, height, nodeWidth, padding);
}

void SankeyModel::relayout(qreal width, qreal height, qreal nodeWidth, qreal padding)
{
    m_nodes.clear();
    m_links.clear();

    if (width <= 0 || height <= 0 || m_counts.isEmpty()) {
        Q_EMIT changed();
        return;
    }

    QHash<QString, int> inbound;
    QHash<QString, int> outbound;
    QSet<QString> stageNames;
    for (auto it = m_counts.constBegin(); it != m_counts.constEnd(); ++it) {
        const QString &from = it.key().first;
        const QString &to = it.key().second;
        outbound[from] += it.value();
        inbound[to] += it.value();
        stageNames.insert(from);
        stageNames.insert(to);
    }

    QList<NodeInfo> nodeList;
    nodeList.reserve(stageNames.size());
    for (const QString &stage : std::as_const(stageNames)) {
        NodeInfo n;
        n.stage = stage;
        n.column = JobStage::column(stage);
        n.order = JobStage::orderInColumn(stage);
        n.value = qMax(inbound.value(stage, 0), outbound.value(stage, 0));
        nodeList.append(n);
    }

    std::sort(nodeList.begin(), nodeList.end(), [](const NodeInfo &a, const NodeInfo &b) {
        if (a.column != b.column) {
            return a.column < b.column;
        }
        return a.order < b.order;
    });

    QHash<int, QList<int>> columnIndices;
    for (int i = 0; i < nodeList.size(); ++i) {
        columnIndices[nodeList.at(i).column].append(i);
    }

    // Columns are spread by their rank among the columns actually present,
    // not their canonical index, so a sparse pipeline (say Start, Applied,
    // Rejected) still fills the width instead of leaving dead stretches for
    // the unused stages in between.
    QList<int> usedColumns = columnIndices.keys();
    std::sort(usedColumns.begin(), usedColumns.end());
    QHash<int, int> rankOfColumn;
    for (int i = 0; i < usedColumns.size(); ++i) {
        rankOfColumn.insert(usedColumns.at(i), i);
    }
    const int rankCount = usedColumns.size();

    constexpr qreal minNodeHeight = 2.0;

    // A single vertical scale is shared by every column: the column with the
    // largest total flow determines it, so no column can overflow the
    // available height.
    qreal scale = 1.0;
    bool haveScale = false;
    for (auto it = columnIndices.constBegin(); it != columnIndices.constEnd(); ++it) {
        int total = 0;
        for (int idx : it.value()) {
            total += nodeList.at(idx).value;
        }
        if (total <= 0) {
            continue;
        }
        const qreal gaps = padding * qMax(0, it.value().size() - 1);
        const qreal available = qMax<qreal>(1.0, height - gaps);
        const qreal candidate = available / total;
        if (!haveScale || candidate < scale) {
            scale = candidate;
            haveScale = true;
        }
    }
    if (!haveScale) {
        scale = 1.0;
    }

    // Tiny nodes get clamped up to minNodeHeight, which consumes height the
    // raw totals above didn't account for; shrink the shared scale until
    // every column fits with its clamped nodes included. Terminates because
    // the scale only decreases and the clamped set only grows.
    bool again = haveScale;
    while (again) {
        again = false;
        for (auto it = columnIndices.constBegin(); it != columnIndices.constEnd(); ++it) {
            const qreal gaps = padding * qMax(0, it.value().size() - 1);
            qreal clampedHeight = 0;
            qreal freeTotal = 0;
            for (int idx : it.value()) {
                const qreal value = nodeList.at(idx).value;
                if (value * scale < minNodeHeight) {
                    clampedHeight += minNodeHeight;
                } else {
                    freeTotal += value;
                }
            }
            if (freeTotal <= 0) {
                continue;
            }
            const qreal available = height - gaps - clampedHeight;
            if (available <= 0) {
                continue;
            }
            const qreal candidate = available / freeTotal;
            if (candidate < scale) {
                scale = candidate;
                again = true;
            }
        }
    }

    // Per-column minimum node height: backs off from minNodeHeight when even
    // that many clamped nodes would overflow a very short viewport.
    QHash<int, qreal> minHeightByColumn;
    for (auto it = columnIndices.constBegin(); it != columnIndices.constEnd(); ++it) {
        const int count = it.value().size();
        const qreal gaps = padding * qMax(0, count - 1);
        minHeightByColumn.insert(it.key(), qMin(minNodeHeight, qMax<qreal>(0.5, (height - gaps) / count)));
    }

    for (auto it = columnIndices.begin(); it != columnIndices.end(); ++it) {
        const QList<int> &idxs = it.value();
        const qreal minHeight = minHeightByColumn.value(it.key());
        // Columns are top-aligned rather than centered: the funnel's success
        // path then runs level along the top while drop-off ribbons peel
        // downward into the open space beneath it, instead of every column
        // being centered and the ribbons weaving up and down to meet.
        const qreal startY = 0.0;
        const int rank = rankOfColumn.value(it.key());
        const qreal x = rankCount > 1 ? rank * (width - nodeWidth) / (rankCount - 1) : (width - nodeWidth) / 2.0;

        qreal cursorY = startY;
        for (int idx : idxs) {
            NodeInfo &n = nodeList[idx];
            n.x = x;
            n.y = cursorY;
            n.width = nodeWidth;
            n.height = qMax(n.value * scale, minHeight);
            cursorY += n.height + padding;
        }
    }

    QHash<QString, int> nodeIndexByStage;
    for (int i = 0; i < nodeList.size(); ++i) {
        nodeIndexByStage.insert(nodeList.at(i).stage, i);
    }

    QList<QPair<QString, QString>> linkKeys = m_counts.keys();
    std::sort(linkKeys.begin(), linkKeys.end(), [&](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
        const int aFrom = nodeIndexByStage.value(a.first);
        const int bFrom = nodeIndexByStage.value(b.first);
        if (aFrom != bFrom) {
            return aFrom < bFrom;
        }
        return nodeIndexByStage.value(a.second) < nodeIndexByStage.value(b.second);
    });

    // Ribbons share the node's vertical scale, but their minimum visible
    // thickness can add up past the node it stacks against; total the raw
    // thicknesses per node and side first, then squeeze each ribbon by its
    // endpoints' overflow so the stack always stays inside both nodes.
    QList<qreal> rawThickness;
    rawThickness.reserve(linkKeys.size());
    QHash<QString, qreal> outboundThickness;
    QHash<QString, qreal> inboundThickness;
    for (const auto &key : std::as_const(linkKeys)) {
        const NodeInfo &fromNode = nodeList.at(nodeIndexByStage.value(key.first));
        const NodeInfo &toNode = nodeList.at(nodeIndexByStage.value(key.second));
        const qreal minThickness = qMin<qreal>(1.5, qMin(minHeightByColumn.value(fromNode.column), minHeightByColumn.value(toNode.column)));
        const qreal thickness = qMax(m_counts.value(key) * scale, minThickness);
        rawThickness.append(thickness);
        outboundThickness[key.first] += thickness;
        inboundThickness[key.second] += thickness;
    }

    QList<qreal> finalThickness;
    finalThickness.reserve(linkKeys.size());
    for (int i = 0; i < linkKeys.size(); ++i) {
        const QPair<QString, QString> &key = linkKeys.at(i);
        const NodeInfo &fromNode = nodeList.at(nodeIndexByStage.value(key.first));
        const NodeInfo &toNode = nodeList.at(nodeIndexByStage.value(key.second));
        const qreal outFactor = qMin<qreal>(1.0, fromNode.height / outboundThickness.value(key.first));
        const qreal inFactor = qMin<qreal>(1.0, toNode.height / inboundThickness.value(key.second));
        finalThickness.append(rawThickness.at(i) * qMin(outFactor, inFactor));
    }

    // Each end of a ribbon gets its slot on the node independently: outgoing
    // ribbons stack in order of their target's height, incoming ones in
    // order of their source's height (d3-sankey style). One global order for
    // both ends would let a low slot head for a high target and twist over
    // its siblings.
    const auto nodeCenter = [&](const QString &stage) {
        const NodeInfo &n = nodeList.at(nodeIndexByStage.value(stage));
        return n.y + n.height / 2.0;
    };
    QList<int> order(linkKeys.size());
    std::iota(order.begin(), order.end(), 0);

    QList<qreal> sourceYTop(linkKeys.size());
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const QPair<QString, QString> &ka = linkKeys.at(a);
        const QPair<QString, QString> &kb = linkKeys.at(b);
        if (ka.first != kb.first) {
            return nodeIndexByStage.value(ka.first) < nodeIndexByStage.value(kb.first);
        }
        const qreal ya = nodeCenter(ka.second);
        const qreal yb = nodeCenter(kb.second);
        if (!qFuzzyCompare(ya, yb)) {
            return ya < yb;
        }
        return nodeIndexByStage.value(ka.second) < nodeIndexByStage.value(kb.second);
    });
    QHash<QString, qreal> sourceCursor;
    for (const NodeInfo &n : std::as_const(nodeList)) {
        sourceCursor.insert(n.stage, n.y);
    }
    for (int i : std::as_const(order)) {
        sourceYTop[i] = sourceCursor.value(linkKeys.at(i).first);
        sourceCursor[linkKeys.at(i).first] = sourceYTop.at(i) + finalThickness.at(i);
    }

    QList<qreal> targetYTop(linkKeys.size());
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const QPair<QString, QString> &ka = linkKeys.at(a);
        const QPair<QString, QString> &kb = linkKeys.at(b);
        if (ka.second != kb.second) {
            return nodeIndexByStage.value(ka.second) < nodeIndexByStage.value(kb.second);
        }
        const qreal ya = nodeCenter(ka.first);
        const qreal yb = nodeCenter(kb.first);
        if (!qFuzzyCompare(ya, yb)) {
            return ya < yb;
        }
        return nodeIndexByStage.value(ka.first) < nodeIndexByStage.value(kb.first);
    });
    QHash<QString, qreal> targetCursor;
    for (const NodeInfo &n : std::as_const(nodeList)) {
        targetCursor.insert(n.stage, n.y);
    }
    for (int i : std::as_const(order)) {
        targetYTop[i] = targetCursor.value(linkKeys.at(i).second);
        targetCursor[linkKeys.at(i).second] = targetYTop.at(i) + finalThickness.at(i);
    }

    for (int i = 0; i < linkKeys.size(); ++i) {
        const QPair<QString, QString> &key = linkKeys.at(i);
        const QString &from = key.first;
        const QString &to = key.second;
        const int value = m_counts.value(key);

        const NodeInfo &fromNode = nodeList.at(nodeIndexByStage.value(from));
        const NodeInfo &toNode = nodeList.at(nodeIndexByStage.value(to));

        const qreal thickness = finalThickness.at(i);
        const qreal y0Top = sourceYTop.at(i);
        const qreal y0Bottom = y0Top + thickness;
        const qreal y1Top = targetYTop.at(i);
        const qreal y1Bottom = y1Top + thickness;

        const qreal x0 = fromNode.x + fromNode.width;
        const qreal x1 = toNode.x;
        const qreal midX = (x0 + x1) / 2.0;

        const QString path = u"M"_s + point(x0, y0Top) + u" C"_s + point(midX, y0Top) + u" "_s + point(midX, y1Top) + u" "_s + point(x1, y1Top)
            + u" L"_s + point(x1, y1Bottom) + u" C"_s + point(midX, y1Bottom) + u" "_s + point(midX, y0Bottom) + u" "_s + point(x0, y0Bottom) + u" Z"_s;

        QColor linkColor = JobStage::color(from);
        linkColor.setAlphaF(0.5f);

        m_links.append(QVariantMap{
            {u"fromStage"_s, from},
            {u"toStage"_s, to},
            {u"value"_s, value},
            {u"pathData"_s, path},
            {u"color"_s, linkColor},
            {u"thickness"_s, thickness},
            {u"sourceY"_s, y0Top},
            {u"targetY"_s, y1Top},
        });
    }

    // Labels sit to the right of each node except in the last used column,
    // whose labels go to the left (d3-sankey style) so nothing ever renders
    // past the right edge. Each label is capped to the gap before the next
    // column in use, and the final gap is split between the right-side label
    // of the penultimate column and the left-side label of the last one, so
    // text elides instead of overlapping.
    QList<qreal> columnStarts;
    for (const NodeInfo &n : std::as_const(nodeList)) {
        if (!columnStarts.contains(n.x)) {
            columnStarts.append(n.x);
        }
    }
    std::sort(columnStarts.begin(), columnStarts.end());

    constexpr qreal labelMargin = 8.0;
    constexpr qreal minLabelWidth = 24.0;

    for (const NodeInfo &n : std::as_const(nodeList)) {
        const QString label = n.stage == QLatin1String(JobStage::Start) ? i18n("Applications") : i18n(n.stage.toUtf8().constData());

        const int columnPos = static_cast<int>(std::distance(columnStarts.begin(), std::find(columnStarts.begin(), columnStarts.end(), n.x)));
        const int lastPos = columnStarts.size() - 1;
        const bool labelOnRight = columnPos < lastPos || lastPos == 0;
        qreal labelWidth = 0;
        if (lastPos == 0) {
            labelWidth = width - (n.x + n.width) - labelMargin;
        } else if (columnPos < lastPos) {
            const qreal gap = columnStarts.at(columnPos + 1) - (n.x + n.width);
            // The outcome names in the last column run longer than the
            // penultimate stage's, so they get the bigger share of the gap.
            const qreal share = columnPos == lastPos - 1 ? 0.4 : 1.0;
            labelWidth = gap * share - 2 * labelMargin;
        } else {
            const qreal gap = n.x - (columnStarts.at(columnPos - 1) + n.width);
            labelWidth = gap * 0.6 - 2 * labelMargin;
        }
        labelWidth = qMax(labelWidth, minLabelWidth);

        m_nodes.append(QVariantMap{
            {u"stage"_s, n.stage},
            {u"label"_s, label},
            {u"x"_s, n.x},
            {u"y"_s, n.y},
            {u"width"_s, n.width},
            {u"height"_s, n.height},
            {u"value"_s, n.value},
            {u"color"_s, JobStage::color(n.stage)},
            {u"labelWidth"_s, labelWidth},
            {u"labelOnRight"_s, labelOnRight},
        });
    }

    Q_EMIT changed();
}
