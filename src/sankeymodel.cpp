/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "sankeymodel.h"
#include "jobstage.h"

#include <KLocalizedString>

#include <QColor>
#include <QHash>
#include <QPair>
#include <QPointF>
#include <QSet>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::Literals::StringLiterals;

namespace
{

QString num(qreal value)
{
    return QString::number(value, 'f', 2);
}

QString point(qreal x, qreal y)
{
    return num(x) + u","_s + num(y);
}

/// Cubic S-curve with horizontal tangents from the current point (xa, ya) to
/// (xb, yb). Every ribbon crossing the same gap between two columns uses the
/// same xa/xb, so two ribbons in the same vertical order at both ends of a
/// gap cannot cross inside it.
QString curveTo(qreal xa, qreal ya, qreal xb, qreal yb)
{
    const qreal mid = (xa + xb) / 2.0;
    return u" C"_s + point(mid, ya) + u" "_s + point(mid, yb) + u" "_s + point(xb, yb);
}

/// Outcomes other than Accepted: drop-offs end here. Accepted continues the
/// main line of the funnel instead.
bool isSink(const QString &stage)
{
    return JobStage::isTerminal(stage) && stage != QLatin1String("Accepted");
}

struct NodeInfo {
    QString stage;
    int column = 0;
    int rank = 0; ///< Index among the columns actually present.
    int order = 0;
    int value = 0;
    bool sink = false;

    qreal x = 0;
    qreal y = 0;
    qreal width = 0;
    qreal height = 0;
};

struct LinkInfo {
    QString from;
    QString to;
    int value = 0;
    int fromIndex = 0;
    int toIndex = 0;
    int fromRank = 0;
    int toRank = 0;
    bool sink = false; ///< Ends in a drop-off outcome.

    qreal thickness = 0;
    qreal sourceY = 0;
    qreal targetY = 0;
    /// Top y of the ribbon in each column it passes through (fromRank+1 .. toRank-1).
    QHash<int, qreal> laneY;

    bool passesColumns() const
    {
        return toRank - fromRank > 1;
    }
};

} // namespace

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
    m_db.reopenIfPathChanged();
    m_counts.clear();

    // Each application is drawn once, as one left-to-right path: the funnel
    // stages it reached in order, then its current stage if that is an
    // outcome. Backward or sideways moves (Offer back to Interview, Ghosted
    // then Rejected) are history, not flow; drawing them as ribbons would
    // run right-to-left across everything else.
    QList<int> jobOrder;
    QHash<int, QStringList> stagesByJob;
    for (const StageTransition &transition : m_db.stageTransitions()) {
        const QString stage = JobStage::canonical(transition.toStage);
        if (!JobStage::isValid(stage)) {
            continue;
        }
        if (!stagesByJob.contains(transition.jobId)) {
            jobOrder.append(transition.jobId);
        }
        stagesByJob[transition.jobId].append(stage);
    }

    for (int jobId : std::as_const(jobOrder)) {
        const QStringList stages = stagesByJob.value(jobId);

        QStringList path{QString::fromLatin1(JobStage::Start)};
        int furthestColumn = JobStage::column(path.first());
        for (const QString &stage : stages) {
            if (JobStage::isTerminal(stage)) {
                continue;
            }
            const int column = JobStage::column(stage);
            if (column > furthestColumn) {
                path.append(stage);
                furthestColumn = column;
            }
        }
        if (!stages.isEmpty() && JobStage::isTerminal(stages.last())) {
            path.append(stages.last());
        }

        for (int i = 0; i + 1 < path.size(); ++i) {
            ++m_counts[{path.at(i), path.at(i + 1)}];
        }
    }

    relayout(width, height, nodeWidth, padding);
}

void SankeyModel::relayout(qreal width, qreal height, qreal nodeWidth, qreal padding)
{
    m_nodes.clear();
    m_links.clear();

    if (width <= 0 || height <= 0 || nodeWidth <= 0 || m_counts.isEmpty()) {
        Q_EMIT changed();
        return;
    }

    padding = qMax<qreal>(0.0, padding);

    //
    // Graph. Only left-to-right links are laid out; reload() never produces
    // anything else, but stay safe against arbitrary counts.
    //
    QHash<QString, int> inbound;
    QHash<QString, int> outbound;
    QSet<QString> stageNames;
    QList<QPair<QString, QString>> linkKeys;

    for (auto it = m_counts.constBegin(); it != m_counts.constEnd(); ++it) {
        const QString &from = it.key().first;
        const QString &to = it.key().second;
        if (it.value() <= 0 || from.isEmpty() || to.isEmpty() || JobStage::column(to) <= JobStage::column(from)) {
            continue;
        }
        outbound[from] += it.value();
        inbound[to] += it.value();
        stageNames.insert(from);
        stageNames.insert(to);
        linkKeys.append(it.key());
    }

    if (stageNames.isEmpty()) {
        Q_EMIT changed();
        return;
    }

    //
    // Nodes and the columns actually present.
    //
    QList<NodeInfo> nodeList;
    nodeList.reserve(stageNames.size());
    for (const QString &stage : std::as_const(stageNames)) {
        NodeInfo node;
        node.stage = stage;
        node.column = JobStage::column(stage);
        node.order = JobStage::orderInColumn(stage);
        node.sink = isSink(stage);
        // Applications still sitting in a stage make its inflow exceed its
        // outflow; the node must be tall enough for the larger side.
        node.value = qMax(inbound.value(stage, 0), outbound.value(stage, 0));
        nodeList.append(node);
    }

    std::sort(nodeList.begin(), nodeList.end(), [](const NodeInfo &a, const NodeInfo &b) {
        if (a.column != b.column) {
            return a.column < b.column;
        }
        if (a.order != b.order) {
            return a.order < b.order;
        }
        return a.stage < b.stage;
    });

    QList<int> usedColumns;
    for (const NodeInfo &node : std::as_const(nodeList)) {
        if (!usedColumns.contains(node.column)) {
            usedColumns.append(node.column);
        }
    }
    std::sort(usedColumns.begin(), usedColumns.end());
    const int rankCount = usedColumns.size();

    QHash<QString, int> nodeIndexByStage;
    for (int i = 0; i < nodeList.size(); ++i) {
        nodeList[i].rank = usedColumns.indexOf(nodeList.at(i).column);
        nodeIndexByStage.insert(nodeList.at(i).stage, i);
    }

    //
    // Links.
    //
    std::sort(linkKeys.begin(), linkKeys.end(), [&](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
        const int aFrom = nodeIndexByStage.value(a.first);
        const int bFrom = nodeIndexByStage.value(b.first);
        if (aFrom != bFrom) {
            return aFrom < bFrom;
        }
        return nodeIndexByStage.value(a.second) < nodeIndexByStage.value(b.second);
    });

    QList<LinkInfo> links;
    links.reserve(linkKeys.size());
    for (const auto &key : std::as_const(linkKeys)) {
        LinkInfo link;
        link.from = key.first;
        link.to = key.second;
        link.value = m_counts.value(key);
        link.fromIndex = nodeIndexByStage.value(key.first);
        link.toIndex = nodeIndexByStage.value(key.second);
        link.fromRank = nodeList.at(link.fromIndex).rank;
        link.toRank = nodeList.at(link.toIndex).rank;
        link.sink = nodeList.at(link.toIndex).sink;
        links.append(link);
    }

    //
    // Order the drop-off outcomes top to bottom by where their applications
    // dropped off: outcomes fed by later stages sit higher. Drop-off ribbons
    // travel in lanes ordered the same way (latest stage innermost), so this
    // keeps the ribbons' order when they rejoin as close as possible to the
    // order they travelled in.
    //
    QHash<int, qreal> meanSourceRank;
    {
        QHash<int, qreal> weighted;
        QHash<int, int> total;
        for (const LinkInfo &link : std::as_const(links)) {
            if (link.sink) {
                weighted[link.toIndex] += static_cast<qreal>(link.value) * link.fromRank;
                total[link.toIndex] += link.value;
            }
        }
        for (auto it = total.constBegin(); it != total.constEnd(); ++it) {
            meanSourceRank.insert(it.key(), weighted.value(it.key()) / it.value());
        }
    }
    QList<int> sinkOrder;
    for (int i = 0; i < nodeList.size(); ++i) {
        if (nodeList.at(i).sink) {
            sinkOrder.append(i);
        }
    }
    std::sort(sinkOrder.begin(), sinkOrder.end(), [&](int a, int b) {
        const qreal ma = meanSourceRank.value(a);
        const qreal mb = meanSourceRank.value(b);
        if (!qFuzzyCompare(ma + 1.0, mb + 1.0)) {
            return ma > mb;
        }
        return a < b; // nodeList is already sorted by JobStage::orderInColumn().
    });
    QHash<int, int> sinkPosition;
    for (int i = 0; i < sinkOrder.size(); ++i) {
        sinkPosition.insert(sinkOrder.at(i), i);
    }

    //
    // What each column holds, top to bottom:
    //
    //   over-lanes   main-line links skipping this column (e.g. Applied -> Interview)
    //   spine node   the funnel stage (or Accepted)
    //   under-lanes  drop-off links on their way to an outcome
    //   sinks        the drop-off outcomes themselves (last column only)
    //
    // Lanes keep one order along their whole run, and a stage's drop-offs join
    // the under-lanes on top (innermost), so drop-offs peel off each stage and
    // nest around each other instead of braiding.
    //
    QList<QList<int>> overLanes(rankCount);
    QList<QList<int>> underLanes(rankCount);
    QList<QList<int>> spineNodes(rankCount);
    QList<QList<int>> sinkNodes(rankCount);

    for (int i = 0; i < links.size(); ++i) {
        const LinkInfo &link = links.at(i);
        for (int rank = link.fromRank + 1; rank < link.toRank; ++rank) {
            (link.sink ? underLanes : overLanes)[rank].append(i);
        }
    }
    for (int rank = 0; rank < rankCount; ++rank) {
        std::sort(overLanes[rank].begin(), overLanes[rank].end(), [&](int a, int b) {
            const LinkInfo &la = links.at(a);
            const LinkInfo &lb = links.at(b);
            if (la.fromRank != lb.fromRank) {
                return la.fromRank < lb.fromRank;
            }
            if (la.toRank != lb.toRank) {
                return la.toRank > lb.toRank;
            }
            return a < b;
        });
        std::sort(underLanes[rank].begin(), underLanes[rank].end(), [&](int a, int b) {
            const LinkInfo &la = links.at(a);
            const LinkInfo &lb = links.at(b);
            if (la.fromRank != lb.fromRank) {
                return la.fromRank > lb.fromRank;
            }
            if (sinkPosition.value(la.toIndex) != sinkPosition.value(lb.toIndex)) {
                return sinkPosition.value(la.toIndex) < sinkPosition.value(lb.toIndex);
            }
            return a < b;
        });
    }
    for (int i = 0; i < nodeList.size(); ++i) {
        if (!nodeList.at(i).sink) {
            spineNodes[nodeList.at(i).rank].append(i);
        }
    }
    for (int index : std::as_const(sinkOrder)) {
        sinkNodes[nodeList.at(index).rank].append(index);
    }

    //
    // Shared scale: the largest for which every column's stack fits.
    //
    // Link thickness is always exactly value * scale. Nodes get a small
    // visual minimum height, which never changes link widths.
    //
    constexpr qreal minimumNodeHeight = 2.0;

    const auto laneThickness = [&](const QList<int> &lanes, qreal scale) {
        qreal sum = 0;
        for (int index : lanes) {
            sum += links.at(index).value * scale;
        }
        return sum;
    };
    const auto blockCount = [&](int rank) {
        return static_cast<int>(!overLanes.at(rank).isEmpty()) + static_cast<int>(!underLanes.at(rank).isEmpty()) + spineNodes.at(rank).size()
            + sinkNodes.at(rank).size();
    };
    QList<qreal> minimumHeight(rankCount, 0.0);
    for (int rank = 0; rank < rankCount; ++rank) {
        const int nodeCount = spineNodes.at(rank).size() + sinkNodes.at(rank).size();
        if (nodeCount > 0) {
            const qreal available = qMax<qreal>(0.0, height - padding * qMax(0, blockCount(rank) - 1));
            minimumHeight[rank] = qMin(minimumNodeHeight, available / nodeCount);
        }
    }
    const auto stackHeight = [&](int rank, qreal scale) {
        qreal used = padding * qMax(0, blockCount(rank) - 1);
        used += laneThickness(overLanes.at(rank), scale) + laneThickness(underLanes.at(rank), scale);
        for (const QList<int> *group : {&spineNodes.at(rank), &sinkNodes.at(rank)}) {
            for (int index : *group) {
                used += qMax(minimumHeight.at(rank), nodeList.at(index).value * scale);
            }
        }
        return used;
    };
    const auto fits = [&](qreal scale) {
        for (int rank = 0; rank < rankCount; ++rank) {
            if (stackHeight(rank, scale) > height + 0.0001) {
                return false;
            }
        }
        return true;
    };

    qreal scale = 0.0;
    if (fits(0.0)) {
        qreal low = 0.0;
        qreal high = 0.0;
        for (const NodeInfo &node : std::as_const(nodeList)) {
            if (node.value > 0) {
                high = qMax(high, height / static_cast<qreal>(node.value));
            }
        }
        for (int iteration = 0; iteration < 60; ++iteration) {
            const qreal middle = (low + high) / 2.0;
            if (fits(middle)) {
                low = middle;
            } else {
                high = middle;
            }
        }
        scale = low;
    }

    for (LinkInfo &link : links) {
        link.thickness = link.value * scale;
    }
    for (int i = 0; i < nodeList.size(); ++i) {
        NodeInfo &node = nodeList[i];
        node.width = nodeWidth;
        node.height = qMax(minimumHeight.at(node.rank), node.value * scale);
    }

    //
    // Positions. Columns spread over the width by rank; every stack starts at
    // the same top, and the tallest one is centered vertically.
    //
    qreal contentHeight = 0;
    for (int rank = 0; rank < rankCount; ++rank) {
        contentHeight = qMax(contentHeight, stackHeight(rank, scale));
    }
    const qreal top = qMax<qreal>(0.0, (height - contentHeight) / 2.0);
    const qreal bottom = top + contentHeight;

    QList<qreal> columnX(rankCount);
    for (int rank = 0; rank < rankCount; ++rank) {
        columnX[rank] = rankCount > 1 ? rank * (width - nodeWidth) / static_cast<qreal>(rankCount - 1) : (width - nodeWidth) / 2.0;
    }
    for (NodeInfo &node : nodeList) {
        node.x = columnX.at(node.rank);
    }

    //
    // Where each ribbon leaves its source, top to bottom: links skipping
    // ahead over later stages, the link to the next stage, then drop-offs.
    // Applications still sitting in the stage leave the bottom of the node
    // empty. A column has at most one main-line node, so the next stage's
    // position never needs to break a tie here.
    //
    QList<QList<int>> outgoing(nodeList.size());
    QList<QList<int>> incoming(nodeList.size());
    for (int i = 0; i < links.size(); ++i) {
        outgoing[links.at(i).fromIndex].append(i);
        incoming[links.at(i).toIndex].append(i);
    }

    const auto outCategory = [&](const LinkInfo &link) {
        if (link.sink) {
            return 2;
        }
        return link.passesColumns() ? 0 : 1;
    };
    const auto assignOutSlots = [&](int n) {
        QList<int> &slotOrder = outgoing[n];
        std::sort(slotOrder.begin(), slotOrder.end(), [&](int a, int b) {
            const LinkInfo &la = links.at(a);
            const LinkInfo &lb = links.at(b);
            const int ca = outCategory(la);
            const int cb = outCategory(lb);
            if (ca != cb) {
                return ca < cb;
            }
            if (ca == 0 && la.toRank != lb.toRank) {
                return la.toRank > lb.toRank;
            }
            if (ca == 2 && sinkPosition.value(la.toIndex) != sinkPosition.value(lb.toIndex)) {
                return sinkPosition.value(la.toIndex) < sinkPosition.value(lb.toIndex);
            }
            return a < b;
        });
        qreal cursor = nodeList.at(n).y;
        for (int index : std::as_const(slotOrder)) {
            links[index].sourceY = cursor;
            cursor += links.at(index).thickness;
        }
    };

    QList<qreal> sinkCursor(rankCount, top);
    for (int rank = 0; rank < rankCount; ++rank) {
        qreal cursor = top;
        bool first = true;
        const auto startBlock = [&]() {
            if (!first) {
                cursor += padding;
            }
            first = false;
        };

        if (!overLanes.at(rank).isEmpty()) {
            startBlock();
            for (int index : overLanes.at(rank)) {
                links[index].laneY.insert(rank, cursor);
                cursor += links.at(index).thickness;
            }
        }
        for (int index : spineNodes.at(rank)) {
            startBlock();
            nodeList[index].y = cursor;
            cursor += nodeList.at(index).height;
            assignOutSlots(index);
        }
        if (!underLanes.at(rank).isEmpty()) {
            // Drop-off lanes stay level instead of rising whenever the stage
            // above them gets shorter: each lane sits no higher than it was in
            // the previous column (or where it left its stage), and only moves
            // down to make room. They still keep their order, and are pulled
            // back up only as far as needed to stay inside the diagram.
            startBlock();
            const QList<int> &lanes = underLanes.at(rank);
            for (int index : lanes) {
                LinkInfo &link = links[index];
                const qreal previous = link.fromRank == rank - 1 ? link.sourceY : link.laneY.value(rank - 1);
                const qreal y = qMax(cursor, previous);
                link.laneY.insert(rank, y);
                cursor = y + link.thickness;
            }
            qreal limit = bottom;
            for (auto it = lanes.crbegin(); it != lanes.crend(); ++it) {
                LinkInfo &link = links[*it];
                const qreal y = qMin(link.laneY.value(rank), limit - link.thickness);
                link.laneY.insert(rank, y);
                limit = y;
            }
            cursor = links.at(lanes.last()).laneY.value(rank) + links.at(lanes.last()).thickness;
        }
        sinkCursor[rank] = first ? cursor : cursor + padding;
    }

    //
    // Drop-off outcomes: stacked in the last column, as close as possible to
    // the height their ribbons arrive at, so rejoining stays shallow.
    //
    for (int rank = 0; rank < rankCount; ++rank) {
        const QList<int> &sinks = sinkNodes.at(rank);
        if (sinks.isEmpty()) {
            continue;
        }
        qreal arrival = std::numeric_limits<qreal>::max();
        for (int n : sinks) {
            for (int index : std::as_const(incoming.at(n))) {
                const LinkInfo &link = links.at(index);
                arrival = qMin(arrival, link.passesColumns() ? link.laneY.value(link.toRank - 1) : link.sourceY);
            }
        }
        qreal groupHeight = padding * (sinks.size() - 1);
        for (int n : sinks) {
            groupHeight += nodeList.at(n).height;
        }
        const qreal lowest = sinkCursor.at(rank);
        const qreal highest = qMax(lowest, bottom - groupHeight);
        qreal cursor = qBound(lowest, arrival, highest);
        for (int n : sinks) {
            nodeList[n].y = cursor;
            cursor += nodeList.at(n).height + padding;
        }
    }

    //
    // Where each ribbon enters its target, top to bottom: for a stage, links
    // arriving over earlier stages first, then the link from the previous
    // stage; for a drop-off outcome, the latest stage first, matching the
    // lane order the ribbons arrive in.
    //
    for (int n = 0; n < nodeList.size(); ++n) {
        QList<int> &slotOrder = incoming[n];
        const bool sink = nodeList.at(n).sink;
        std::sort(slotOrder.begin(), slotOrder.end(), [&](int a, int b) {
            const LinkInfo &la = links.at(a);
            const LinkInfo &lb = links.at(b);
            if (sink) {
                if (la.fromRank != lb.fromRank) {
                    return la.fromRank > lb.fromRank;
                }
                return a < b;
            }
            if (la.passesColumns() != lb.passesColumns()) {
                return la.passesColumns();
            }
            if (la.fromRank != lb.fromRank) {
                return la.fromRank < lb.fromRank;
            }
            return a < b;
        });
        qreal cursor = nodeList.at(n).y;
        for (int index : std::as_const(slotOrder)) {
            links[index].targetY = cursor;
            cursor += links.at(index).thickness;
        }
    }

    //
    // Ribbon paths: an S-curve through each gap between columns and a straight
    // run across every column the ribbon passes.
    //
    for (const LinkInfo &link : std::as_const(links)) {
        if (link.thickness <= 0.0) {
            continue;
        }
        const NodeInfo &fromNode = nodeList.at(link.fromIndex);
        const NodeInfo &toNode = nodeList.at(link.toIndex);

        // Top edge, left to right: (x, y) at each column boundary.
        QList<QPointF> edge;
        edge.append({fromNode.x + fromNode.width, link.sourceY});
        for (int rank = link.fromRank + 1; rank < link.toRank; ++rank) {
            const qreal y = link.laneY.value(rank);
            edge.append({columnX.at(rank), y});
            edge.append({columnX.at(rank) + nodeWidth, y});
        }
        edge.append({toNode.x, link.targetY});

        QString path = u"M"_s + point(edge.first().x(), edge.first().y());
        for (int i = 1; i < edge.size(); ++i) {
            const QPointF &a = edge.at(i - 1);
            const QPointF &b = edge.at(i);
            // Odd steps cross a gap; even steps run straight across a column.
            path += i % 2 == 1 ? curveTo(a.x(), a.y(), b.x(), b.y()) : u" L"_s + point(b.x(), b.y());
        }
        path += u" L"_s + point(edge.last().x(), edge.last().y() + link.thickness);
        for (int i = edge.size() - 1; i > 0; --i) {
            const QPointF &a = edge.at(i);
            const QPointF &b = edge.at(i - 1);
            path += i % 2 == 1 ? curveTo(a.x(), a.y() + link.thickness, b.x(), b.y() + link.thickness) : u" L"_s + point(b.x(), b.y() + link.thickness);
        }
        path += u" Z"_s;

        QColor linkColor = JobStage::color(link.from);
        linkColor.setAlphaF(0.5);

        m_links.append(QVariantMap{
            {u"fromStage"_s, link.from},
            {u"toStage"_s, link.to},
            {u"value"_s, link.value},
            {u"pathData"_s, path},
            {u"color"_s, linkColor},
            {u"thickness"_s, link.thickness},
            {u"sourceY"_s, link.sourceY},
            {u"targetY"_s, link.targetY},
        });
    }

    //
    // Nodes and their labels. Labels go right of every column but the last,
    // whose labels go left.
    //
    constexpr qreal labelMargin = 8.0;
    constexpr qreal minLabelWidth = 24.0;
    const int lastRank = rankCount - 1;

    for (const NodeInfo &node : std::as_const(nodeList)) {
        const QString label = node.stage == QLatin1String(JobStage::Start) ? i18n("Applications") : i18n(node.stage.toUtf8().constData());

        const bool labelOnRight = node.rank < lastRank || lastRank == 0;
        qreal labelWidth = 0.0;
        if (lastRank == 0) {
            labelWidth = width - (node.x + node.width) - labelMargin;
        } else if (node.rank < lastRank) {
            const qreal gap = columnX.at(node.rank + 1) - (node.x + node.width);
            // The last gap is shared with the last column's labels, which
            // are drawn on its left.
            const qreal share = node.rank == lastRank - 1 ? 0.4 : 1.0;
            labelWidth = gap * share - 2.0 * labelMargin;
        } else {
            const qreal gap = node.x - (columnX.at(node.rank - 1) + node.width);
            labelWidth = gap * 0.6 - 2.0 * labelMargin;
        }
        labelWidth = qMax(labelWidth, minLabelWidth);

        m_nodes.append(QVariantMap{
            {u"stage"_s, node.stage},
            {u"label"_s, label},
            {u"x"_s, node.x},
            {u"y"_s, node.y},
            {u"width"_s, node.width},
            {u"height"_s, node.height},
            {u"value"_s, node.value},
            {u"color"_s, JobStage::color(node.stage)},
            {u"labelWidth"_s, labelWidth},
            {u"labelOnRight"_s, labelOnRight},
        });
    }

    Q_EMIT changed();
}
