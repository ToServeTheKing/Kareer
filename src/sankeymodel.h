// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "jobsdatabase.h"

#include <QObject>
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
    /// Recomputes node/link geometry to fit within (width, height) logical
    /// pixels. Call whenever the data or the available viewport changes.
    void relayout(qreal width, qreal height);

Q_SIGNALS:
    void changed();

private:
    JobsDatabase m_db;
    QVariantList m_nodes;
    QVariantList m_links;
};
