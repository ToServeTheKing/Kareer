/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "job.h"
#include "jobsdatabase.h"
#include "sankeymodel.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

// SankeyModel always opens JobsDatabase::defaultPath(), so each test points
// that at a fresh temporary file via the KAREER_DB_PATH override.
class SankeyLayoutTest : public QObject
{
    Q_OBJECT

    // Defaults of SankeyModel::reload()/relayout(), which every test uses.
    static constexpr qreal nodeWidth = 16.0;
    static constexpr qreal rowPadding = 10.0;
    // The label margin/floor baked into the layout (sankeymodel.cpp).
    static constexpr qreal labelMargin = 8.0;
    static constexpr qreal eps = 0.01;

private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        qputenv("KAREER_DB_PATH", (m_dir->path() + QStringLiteral("/test.sqlite")).toUtf8());
    }

    void layoutReflectsTransitionCounts()
    {
        JobsDatabase db;

        makeJob(db, QStringLiteral("Applied"));
        makeJob(db, QStringLiteral("Applied"));
        makeJob(db, QStringLiteral("Screening"));
        makeJob(db, QStringLiteral("Rejected"));

        SankeyModel model;
        model.reload(600, 400);

        QVERIFY(!model.isEmpty());

        const QVariantList nodes = model.nodes();
        const QVariantList links = model.links();

        QHash<QString, QVariantMap> nodeByStage;
        for (const QVariant &v : nodes) {
            const QVariantMap m = v.toMap();
            nodeByStage.insert(m.value(QStringLiteral("stage")).toString(), m);
            QVERIFY(m.value(QStringLiteral("x")).toReal() >= 0);
            QVERIFY(m.value(QStringLiteral("y")).toReal() >= 0);
            QVERIFY(m.value(QStringLiteral("width")).toReal() > 0);
            QVERIFY(m.value(QStringLiteral("height")).toReal() > 0);
        }

        QVERIFY(nodeByStage.contains(QStringLiteral("Start")));
        QCOMPARE(nodeByStage.value(QStringLiteral("Start")).value(QStringLiteral("value")).toInt(), 4);

        int totalLinkValue = 0;
        for (const QVariant &v : links) {
            totalLinkValue += v.toMap().value(QStringLiteral("value")).toInt();
        }
        // Start->Applied(4), Applied->Screening(1), Applied->Rejected(1)
        QCOMPARE(totalLinkValue, 6);

        const qreal startX = nodeByStage.value(QStringLiteral("Start")).value(QStringLiteral("x")).toReal();
        for (auto it = nodeByStage.constBegin(); it != nodeByStage.constEnd(); ++it) {
            QVERIFY(it.value().value(QStringLiteral("x")).toReal() >= startX);
        }
    }

    void emptyDatabaseProducesEmptyLayout()
    {
        JobsDatabase db;
        QVERIFY(db.isOpen());

        SankeyModel model;
        model.reload(400, 300);
        QVERIFY(model.isEmpty());
        QVERIFY(model.nodes().isEmpty());
        QVERIFY(model.links().isEmpty());
    }

    void geometryFitsViewport()
    {
        JobsDatabase db;
        seedDensePipeline(db);

        SankeyModel model;
        for (const QSizeF size : {QSizeF(600, 300), QSizeF(600, 40)}) {
            model.reload(size.width(), size.height());
            QVERIFY(!model.isEmpty());

            QHash<qreal, qreal> columnHeights;
            QHash<qreal, int> columnCounts;
            for (const QVariant &v : model.nodes()) {
                const QVariantMap m = v.toMap();
                const qreal x = m.value(QStringLiteral("x")).toReal();
                const qreal y = m.value(QStringLiteral("y")).toReal();
                const qreal w = m.value(QStringLiteral("width")).toReal();
                const qreal h = m.value(QStringLiteral("height")).toReal();
                QVERIFY(x >= -eps);
                QVERIFY(x + w <= size.width() + eps);
                QVERIFY(y >= -eps);
                QVERIFY(y + h <= size.height() + eps);
                columnHeights[x] += h;
                columnCounts[x] += 1;

                // The label rect must lie inside the viewport too.
                const qreal labelWidth = m.value(QStringLiteral("labelWidth")).toReal();
                if (m.value(QStringLiteral("labelOnRight")).toBool()) {
                    QVERIFY(x + w + labelMargin + labelWidth <= size.width() + eps);
                } else {
                    QVERIFY(x - labelMargin - labelWidth >= -eps);
                }
            }
            for (auto it = columnHeights.constBegin(); it != columnHeights.constEnd(); ++it) {
                const qreal gaps = rowPadding * (columnCounts.value(it.key()) - 1);
                QVERIFY(it.value() + gaps <= size.height() + eps);
            }
        }
    }

    void ribbonsStayInsideNodes()
    {
        JobsDatabase db;
        seedDensePipeline(db);

        SankeyModel model;
        model.reload(600, 300);
        QVERIFY(!model.isEmpty());

        QHash<QString, QVariantMap> nodeByStage;
        for (const QVariant &v : model.nodes()) {
            const QVariantMap m = v.toMap();
            nodeByStage.insert(m.value(QStringLiteral("stage")).toString(), m);
        }

        QHash<QString, qreal> outboundTotal;
        QHash<QString, qreal> inboundTotal;
        for (const QVariant &v : model.links()) {
            const QVariantMap m = v.toMap();
            const qreal thickness = m.value(QStringLiteral("thickness")).toReal();
            QVERIFY(thickness > 0);

            const QVariantMap fromNode = nodeByStage.value(m.value(QStringLiteral("fromStage")).toString());
            const QVariantMap toNode = nodeByStage.value(m.value(QStringLiteral("toStage")).toString());
            const qreal sourceY = m.value(QStringLiteral("sourceY")).toReal();
            const qreal targetY = m.value(QStringLiteral("targetY")).toReal();

            QVERIFY(sourceY >= fromNode.value(QStringLiteral("y")).toReal() - eps);
            QVERIFY(sourceY + thickness <= fromNode.value(QStringLiteral("y")).toReal() + fromNode.value(QStringLiteral("height")).toReal() + eps);
            QVERIFY(targetY >= toNode.value(QStringLiteral("y")).toReal() - eps);
            QVERIFY(targetY + thickness <= toNode.value(QStringLiteral("y")).toReal() + toNode.value(QStringLiteral("height")).toReal() + eps);

            outboundTotal[m.value(QStringLiteral("fromStage")).toString()] += thickness;
            inboundTotal[m.value(QStringLiteral("toStage")).toString()] += thickness;
        }
        for (auto it = outboundTotal.constBegin(); it != outboundTotal.constEnd(); ++it) {
            QVERIFY(it.value() <= nodeByStage.value(it.key()).value(QStringLiteral("height")).toReal() + eps);
        }
        for (auto it = inboundTotal.constBegin(); it != inboundTotal.constEnd(); ++it) {
            QVERIFY(it.value() <= nodeByStage.value(it.key()).value(QStringLiteral("height")).toReal() + eps);
        }
    }

    void ribbonSlotsFollowEndpointHeights()
    {
        JobsDatabase db;
        seedDensePipeline(db);

        SankeyModel model;
        model.reload(600, 300);
        QVERIFY(!model.isEmpty());

        QHash<QString, QVariantMap> nodeByStage;
        for (const QVariant &v : model.nodes()) {
            const QVariantMap m = v.toMap();
            nodeByStage.insert(m.value(QStringLiteral("stage")).toString(), m);
        }
        const auto center = [&](const QString &stage) {
            const QVariantMap n = nodeByStage.value(stage);
            return n.value(QStringLiteral("y")).toReal() + n.value(QStringLiteral("height")).toReal() / 2.0;
        };

        // Outgoing ribbons must stack top-to-bottom in order of their
        // target's height, incoming ones by their source's height, or the
        // ribbons twist over each other right at the node.
        QHash<QString, QList<QPair<qreal, qreal>>> outgoing; // sourceY -> target center
        QHash<QString, QList<QPair<qreal, qreal>>> incoming; // targetY -> source center
        for (const QVariant &v : model.links()) {
            const QVariantMap m = v.toMap();
            outgoing[m.value(QStringLiteral("fromStage")).toString()].append(
                {m.value(QStringLiteral("sourceY")).toReal(), center(m.value(QStringLiteral("toStage")).toString())});
            incoming[m.value(QStringLiteral("toStage")).toString()].append(
                {m.value(QStringLiteral("targetY")).toReal(), center(m.value(QStringLiteral("fromStage")).toString())});
        }
        const auto verifySorted = [](QList<QPair<qreal, qreal>> slots) {
            std::sort(slots.begin(), slots.end());
            for (int i = 1; i < slots.size(); ++i) {
                QVERIFY(slots.at(i).second >= slots.at(i - 1).second - 0.01);
            }
        };
        for (const auto &slots : std::as_const(outgoing)) {
            verifySorted(slots);
        }
        for (const auto &slots : std::as_const(incoming)) {
            verifySorted(slots);
        }
    }

    void sparseColumnsFillWidth()
    {
        JobsDatabase db;
        makeJob(db, QStringLiteral("Applied"));
        makeJob(db, QStringLiteral("Applied"));
        makeJob(db, QStringLiteral("Rejected"));

        SankeyModel model;
        model.reload(600, 300);

        // Only Start, Applied and Rejected are present: their columns should
        // spread evenly over the full width, not sit at canonical positions.
        QList<qreal> xs;
        for (const QVariant &v : model.nodes()) {
            const qreal x = v.toMap().value(QStringLiteral("x")).toReal();
            if (!xs.contains(x)) {
                xs.append(x);
            }
        }
        std::sort(xs.begin(), xs.end());
        QCOMPARE(xs.size(), 3);
        QVERIFY(qAbs(xs.at(0)) < eps);
        QVERIFY(qAbs(xs.at(1) - (600 - nodeWidth) / 2.0) < eps);
        QVERIFY(qAbs(xs.at(2) - (600 - nodeWidth)) < eps);
    }

    void caseVariantStageIsCanonicalized()
    {
        JobsDatabase db;

        Job job;
        job.company = QStringLiteral("Co");
        job.title = QStringLiteral("Title");
        QVERIFY(db.addJob(job));
        QVERIFY(db.setStage(job.id, QStringLiteral("rejected")));
        QCOMPARE(db.jobById(job.id)->stage, QStringLiteral("Rejected"));

        SankeyModel model;
        model.reload(600, 300);
        QStringList stages;
        for (const QVariant &v : model.nodes()) {
            stages.append(v.toMap().value(QStringLiteral("stage")).toString());
        }
        QVERIFY(stages.contains(QStringLiteral("Rejected")));
        QVERIFY(!stages.contains(QStringLiteral("rejected")));

        // Pre-canonicalization rows already in the database are repaired the
        // next time it is opened (migrate() normalization).
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("fixup"));
            raw.setDatabaseName(m_dir->path() + QStringLiteral("/test.sqlite"));
            QVERIFY(raw.open());
            QSqlQuery q(raw);
            QVERIFY(q.exec(QStringLiteral("UPDATE jobs SET stage = 'ghosted'")));
            QVERIFY(q.exec(QStringLiteral("UPDATE stage_history SET to_stage = 'ghosted' WHERE to_stage = 'Rejected'")));
            raw.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("fixup"));

        JobsDatabase reopened;
        QVERIFY(reopened.isOpen());
        QCOMPARE(reopened.jobById(job.id)->stage, QStringLiteral("Ghosted"));
        const auto transitions = reopened.stageTransitions();
        for (const StageTransition &t : transitions) {
            QVERIFY(t.toStage != QStringLiteral("ghosted"));
        }
    }

    void degenerateSizesDoNotCrash()
    {
        JobsDatabase db;
        seedDensePipeline(db);

        SankeyModel model;
        model.reload(0, 0);
        QVERIFY(model.isEmpty());
        model.relayout(-5, 100);
        QVERIFY(model.isEmpty());
        model.relayout(5, 5);
        model.relayout(2000, 2);
        model.relayout(600, 300);
        QVERIFY(!model.isEmpty());
    }

private:
    static void makeJob(JobsDatabase &db, const QString &stage)
    {
        Job job;
        job.company = QStringLiteral("Co");
        job.title = QStringLiteral("Title");
        QVERIFY(db.addJob(job));
        if (stage != QStringLiteral("Applied")) {
            QVERIFY(db.setStage(job.id, stage));
        }
    }

    static void walkJob(JobsDatabase &db, const QStringList &stages)
    {
        Job job;
        job.company = QStringLiteral("Co");
        job.title = QStringLiteral("Title");
        QVERIFY(db.addJob(job));
        for (const QString &stage : stages) {
            QVERIFY(db.setStage(job.id, stage));
        }
    }

    /// Populates every column, including all four terminal outcomes sharing
    /// the last one.
    static void seedDensePipeline(JobsDatabase &db)
    {
        walkJob(db, {QStringLiteral("Screening"), QStringLiteral("Interview"), QStringLiteral("Onsite"), QStringLiteral("Offer"), QStringLiteral("Accepted")});
        walkJob(db, {QStringLiteral("Screening"), QStringLiteral("Rejected")});
        walkJob(db, {QStringLiteral("Ghosted")});
        walkJob(db, {QStringLiteral("Screening"), QStringLiteral("Interview"), QStringLiteral("Rejected")});
        walkJob(db, {QStringLiteral("Withdrawn")});
        walkJob(db, {QStringLiteral("Screening"), QStringLiteral("Interview"), QStringLiteral("Onsite"), QStringLiteral("Rejected")});
        walkJob(db, {});
        walkJob(db, {});
        walkJob(db, {});
        walkJob(db, {});
    }

    std::unique_ptr<QTemporaryDir> m_dir;
};

QTEST_GUILESS_MAIN(SankeyLayoutTest)
#include "sankeylayouttest.moc"
