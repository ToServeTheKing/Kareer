/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "jobsdatabase.h"

#include <QHash>
#include <QObject>
#include <QPair>
#include <QQmlEngine>
#include <QVariantList>

/**
 * Turns the recorded stage_history transitions into a laid-out Sankey
 * diagram: a column per pipeline stage, nodes sized by how many
 * applications passed through them, and ribbon-shaped links (as SVG path
 * data, ready for QtQuick.Shapes' PathSvg) sized by transition counts.
 *
 * All geometry is computed here rather than in QML so the layout itself is
 * unit-testable (see autotests/sankeylayouttest.cpp) and the QML side stays
 * a plain renderer.
 */
class SankeyModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY changed)
    Q_PROPERTY(QVariantList links READ links NOTIFY changed)
    Q_PROPERTY(bool empty READ isEmpty NOTIFY changed)

public:
    explicit SankeyModel(QObject *parent = nullptr);

    QVariantList nodes() const;
    QVariantList links() const;
    bool isEmpty() const;

public Q_SLOTS:
    /// Re-reads the stage history from the database, then lays it out to fit
    /// within (width, height) logical pixels. Call whenever the data changes.
    void reload(qreal width, qreal height, qreal nodeWidth = 16.0, qreal padding = 10.0);

    /// Recomputes node/link geometry from the cached stage history. Call on
    /// viewport resizes; unlike reload() this never touches the database.
    void relayout(qreal width, qreal height, qreal nodeWidth = 16.0, qreal padding = 10.0);

Q_SIGNALS:
    void changed();

private:
    JobsDatabase m_db;
    QHash<QPair<QString, QString>, int> m_counts;
    QVariantList m_nodes;
    QVariantList m_links;
};
