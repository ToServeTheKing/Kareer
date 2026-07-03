// SPDX-License-Identifier: GPL-3.0-or-later
#include "sankeymodel.h"
#include "jobstage.h"

#include <KLocalizedString>

#include <QHash>
#include <QPair>
#include <QSet>
#include <QVariantMap>
#include <algorithm>
#include <iterator>

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

void SankeyModel::relayout(qreal width, qreal height)
{
    m_nodes.clear();
    m_links.clear();

    if (width <= 0 || height <= 0) {
        Q_EMIT changed();
        return;
    }

    const QList<StageTransition> transitions = m_db.stageTransitions();

    QHash<QPair<QString, QString>, int> counts;
    for (const StageTransition &t : transitions) {
        const QString from = t.fromStage.isEmpty() ? QString::fromLatin1(JobStage::Start) : t.fromStage;
        counts[{from, t.toStage}] += 1;
    }

    if (counts.isEmpty()) {
        Q_EMIT changed();
        return;
    }

    QHash<QString, int> inbound;
    QHash<QString, int> outbound;
    QSet<QString> stageNames;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
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
    int maxColumn = 0;
    for (int i = 0; i < nodeList.size(); ++i) {
        columnIndices[nodeList.at(i).column].append(i);
        maxColumn = qMax(maxColumn, nodeList.at(i).column);
    }

    constexpr qreal nodeWidth = 16.0;
    constexpr qreal padding = 10.0;

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

    for (auto it = columnIndices.begin(); it != columnIndices.end(); ++it) {
        const QList<int> &idxs = it.value();
        qreal totalHeight = 0;
        for (int idx : idxs) {
            totalHeight += qMax<qreal>(nodeList.at(idx).value * scale, 2.0);
        }
        const qreal gaps = padding * qMax(0, idxs.size() - 1);
        const qreal startY = qMax<qreal>(0.0, (height - totalHeight - gaps) / 2.0);
        const qreal x = maxColumn > 0 ? (it.key() * (width - nodeWidth) / maxColumn) : 0.0;

        qreal cursorY = startY;
        for (int idx : idxs) {
            NodeInfo &n = nodeList[idx];
            n.x = x;
            n.y = cursorY;
            n.width = nodeWidth;
            n.height = qMax<qreal>(n.value * scale, 2.0);
            cursorY += n.height + padding;
        }
    }

    QHash<QString, int> nodeIndexByStage;
    for (int i = 0; i < nodeList.size(); ++i) {
        nodeIndexByStage.insert(nodeList.at(i).stage, i);
    }

    QHash<QString, qreal> sourceCursor;
    QHash<QString, qreal> targetCursor;
    for (const NodeInfo &n : std::as_const(nodeList)) {
        sourceCursor.insert(n.stage, n.y);
        targetCursor.insert(n.stage, n.y);
    }

    QList<QPair<QString, QString>> linkKeys = counts.keys();
    std::sort(linkKeys.begin(), linkKeys.end(), [&](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
        const int aFrom = nodeIndexByStage.value(a.first);
        const int bFrom = nodeIndexByStage.value(b.first);
        if (aFrom != bFrom) {
            return aFrom < bFrom;
        }
        return nodeIndexByStage.value(a.second) < nodeIndexByStage.value(b.second);
    });

    for (const auto &key : std::as_const(linkKeys)) {
        const QString &from = key.first;
        const QString &to = key.second;
        const int value = counts.value(key);
        const qreal thickness = qMax<qreal>(value * scale, 1.5);

        const NodeInfo &fromNode = nodeList.at(nodeIndexByStage.value(from));
        const NodeInfo &toNode = nodeList.at(nodeIndexByStage.value(to));

        const qreal y0Top = sourceCursor.value(from);
        const qreal y0Bottom = y0Top + thickness;
        sourceCursor[from] = y0Bottom;

        const qreal y1Top = targetCursor.value(to);
        const qreal y1Bottom = y1Top + thickness;
        targetCursor[to] = y1Bottom;

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
        });
    }

    // Labels are drawn to the right of each node. Without a bound, a long
    // label (e.g. "Applications") can run into the next column's node and
    // its own label. Cap each node's label to the gap before the next
    // distinct column actually in use (skipping empty columns), so text
    // elides instead of overlapping.
    QList<qreal> columnStarts;
    for (const NodeInfo &n : std::as_const(nodeList)) {
        if (!columnStarts.contains(n.x)) {
            columnStarts.append(n.x);
        }
    }
    std::sort(columnStarts.begin(), columnStarts.end());

    for (const NodeInfo &n : std::as_const(nodeList)) {
        const QString label = n.stage == QLatin1String(JobStage::Start) ? i18n("Applications") : i18n(n.stage.toUtf8().constData());

        const int columnPos = static_cast<int>(std::distance(columnStarts.begin(), std::find(columnStarts.begin(), columnStarts.end(), n.x)));
        const qreal nextColumnX = columnPos + 1 < columnStarts.size() ? columnStarts.at(columnPos + 1) : width;
        const qreal labelWidth = qMax<qreal>(24.0, nextColumnX - (n.x + n.width) - 8.0);

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
        });
    }

    Q_EMIT changed();
}
