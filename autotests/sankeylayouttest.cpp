/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "job.h"
#include "jobsdatabase.h"
#include "sankeymodel.h"

#include <QPointF>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
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

    void ribbonsDoNotCross_data()
    {
        QTest::addColumn<int>("seed");
        QTest::newRow("dense pipeline") << int(DenseSeed);
        QTest::newRow("links skipping stages") << int(SkipSeed);
        QTest::newRow("back and sideways moves") << int(BackMoveSeed);
    }

    // The "braid" bug: ribbons twisting over each other. Checked on the drawn
    // geometry itself: no two ribbons may overlap anywhere between their ends.
    void ribbonsDoNotCross()
    {
        QFETCH(int, seed);
        JobsDatabase db;
        seedPipeline(db, seed);

        SankeyModel model;
        for (const QSizeF size : {QSizeF(600, 300), QSizeF(900, 500), QSizeF(400, 800)}) {
            model.reload(size.width(), size.height());
            QVERIFY(!model.isEmpty());
            verifyNoOverlaps(model, std::numeric_limits<qreal>::max());
        }
    }

    // With one node per outcome, ribbons from different stages into different
    // outcomes can't always keep their order; any such crossing must stay in
    // the final gap where they rejoin, never braid across the diagram.
    void crossingsOnlyWhereRibbonsRejoin()
    {
        JobsDatabase db;
        seedPipeline(db, LargeSeed);

        SankeyModel model;
        model.reload(900, 500);
        QVERIFY(!model.isEmpty());

        QList<qreal> xs;
        for (const QVariant &v : model.nodes()) {
            const qreal x = v.toMap().value(QStringLiteral("x")).toReal();
            if (!xs.contains(x)) {
                xs.append(x);
            }
        }
        std::sort(xs.begin(), xs.end());
        QVERIFY(xs.size() >= 2);
        verifyNoOverlaps(model, xs.at(xs.size() - 2) + nodeWidth);
    }

    // Each application is one left-to-right path: the funnel stages it
    // reached, then its current stage if that is an outcome.
    void historyCollapsesToForwardPaths()
    {
        JobsDatabase db;
        seedPipeline(db, BackMoveSeed);

        SankeyModel model;
        model.reload(600, 300);

        QHash<QString, int> links;
        for (const QVariant &v : model.links()) {
            const QVariantMap m = v.toMap();
            links.insert(m.value(QStringLiteral("fromStage")).toString() + QStringLiteral(">") + m.value(QStringLiteral("toStage")).toString(),
                         m.value(QStringLiteral("value")).toInt());
        }
        const QHash<QString, int> expected{
            {QStringLiteral("Start>Applied"), 4},
            {QStringLiteral("Applied>Screening"), 2},
            {QStringLiteral("Applied>Interview"), 2},
            {QStringLiteral("Interview>Offer"), 1},
            {QStringLiteral("Screening>Rejected"), 1},
            {QStringLiteral("Screening>Withdrawn"), 1},
        };
        QCOMPARE(links, expected);

        for (const QVariant &v : model.nodes()) {
            const QVariantMap m = v.toMap();
            // Ghosted was superseded by Rejected, so it isn't an outcome anyone ended in.
            QVERIFY(m.value(QStringLiteral("stage")).toString() != QStringLiteral("Ghosted"));
            if (m.value(QStringLiteral("stage")).toString() == QStringLiteral("Start")) {
                QCOMPARE(m.value(QStringLiteral("value")).toInt(), 4);
            }
        }
    }

    void contentIsVerticallyCentered()
    {
        JobsDatabase db;
        seedPipeline(db, DenseSeed);

        SankeyModel model;
        model.reload(600, 300);

        qreal top = std::numeric_limits<qreal>::max();
        qreal bottom = std::numeric_limits<qreal>::lowest();
        for (const QVariant &v : model.nodes()) {
            const QVariantMap node = v.toMap();
            top = qMin(top, node.value(QStringLiteral("y")).toReal());
            bottom = qMax(bottom, node.value(QStringLiteral("y")).toReal() + node.value(QStringLiteral("height")).toReal());
        }
        for (const QVariant &v : model.links()) {
            const Ribbon ribbon = parseRibbon(v.toMap());
            for (const QList<QPointF> *edge : {&ribbon.top, &ribbon.bottom}) {
                for (const QPointF &p : *edge) {
                    top = qMin(top, p.y());
                    bottom = qMax(bottom, p.y());
                }
            }
        }
        QVERIFY2(qAbs(top - (300.0 - bottom)) < 0.05, qPrintable(QStringLiteral("top margin %1, bottom margin %2").arg(top).arg(300.0 - bottom)));
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
    enum Seed {
        DenseSeed,
        SkipSeed,
        BackMoveSeed,
        LargeSeed,
    };

    static void seedPipeline(JobsDatabase &db, int seed)
    {
        const auto S = [](const char *stage) {
            return QString::fromLatin1(stage);
        };
        switch (seed) {
        case DenseSeed:
            seedDensePipeline(db);
            break;
        case SkipSeed:
            walkJob(db, {S("Interview"), S("Offer"), S("Accepted")});
            walkJob(db, {S("Screening"), S("Interview"), S("Rejected")});
            walkJob(db, {S("Screening"), S("Rejected")});
            walkJob(db, {S("Screening"), S("Interview"), S("Onsite"), S("Offer"), S("Accepted")});
            walkJob(db, {S("Ghosted")});
            walkJob(db, {});
            addJobAt(db, S("Interview"));
            break;
        case BackMoveSeed:
            walkJob(db, {S("Screening"), S("Ghosted"), S("Rejected")});
            walkJob(db, {S("Interview"), S("Offer"), S("Interview")});
            walkJob(db, {S("Rejected"), S("Interview")});
            walkJob(db, {S("Screening"), S("Withdrawn")});
            break;
        case LargeSeed:
            for (int i = 0; i < 12; ++i) {
                walkJob(db, {});
            }
            for (int i = 0; i < 6; ++i) {
                walkJob(db, {S("Ghosted")});
            }
            for (int i = 0; i < 5; ++i) {
                walkJob(db, {S("Rejected")});
            }
            walkJob(db, {S("Withdrawn")});
            for (int i = 0; i < 4; ++i) {
                walkJob(db, {S("Screening"), S("Rejected")});
            }
            walkJob(db, {S("Screening"), S("Ghosted")});
            walkJob(db, {S("Screening")});
            for (int i = 0; i < 3; ++i) {
                walkJob(db, {S("Screening"), S("Interview"), S("Rejected")});
            }
            walkJob(db, {S("Screening"), S("Interview"), S("Withdrawn")});
            walkJob(db, {S("Interview"), S("Onsite"), S("Rejected")});
            walkJob(db, {S("Screening"), S("Interview"), S("Onsite"), S("Offer"), S("Accepted")});
            walkJob(db, {S("Screening"), S("Interview"), S("Onsite"), S("Offer"), S("Rejected")});
            walkJob(db, {S("Screening"), S("Interview"), S("Onsite"), S("Offer")});
            break;
        }
    }

    static void addJobAt(JobsDatabase &db, const QString &stage)
    {
        Job job;
        job.company = QStringLiteral("Co");
        job.title = QStringLiteral("Title");
        job.stage = stage;
        QVERIFY(db.addJob(job));
    }

    /// A ribbon's outline as drawn: its top and bottom edges, each sampled
    /// left to right.
    struct Ribbon {
        QString name;
        QList<QPointF> top;
        QList<QPointF> bottom;
    };

    /// Parses the absolute M/C/L/Z path SankeyModel generates. The outline
    /// runs along the top edge, drops straight down at the target, and runs
    /// back along the bottom edge.
    static Ribbon parseRibbon(const QVariantMap &link)
    {
        Ribbon ribbon;
        ribbon.name = link.value(QStringLiteral("fromStage")).toString() + QStringLiteral("->") + link.value(QStringLiteral("toStage")).toString();

        static const QRegularExpression token(QStringLiteral("[MCLZ]|-?\\d+(?:\\.\\d+)?"));
        QStringList tokens;
        auto it = token.globalMatch(link.value(QStringLiteral("pathData")).toString());
        while (it.hasNext()) {
            tokens.append(it.next().captured());
        }

        QList<QPointF> *edge = &ribbon.top;
        QPointF current;
        int i = 0;
        // Read coordinates one at a time: argument evaluation order is
        // unspecified, so QPointF(next(), next()) could swap x and y.
        const auto nextPoint = [&]() {
            const qreal x = tokens.value(i++).toDouble();
            const qreal y = tokens.value(i++).toDouble();
            return QPointF(x, y);
        };
        while (i < tokens.size()) {
            const QString command = tokens.at(i++);
            if (command == QLatin1String("M")) {
                current = nextPoint();
                edge->append(current);
            } else if (command == QLatin1String("L")) {
                const QPointF next = nextPoint();
                if (edge == &ribbon.top && qAbs(next.x() - current.x()) < 1e-6 && qAbs(next.y() - current.y()) > 1e-6) {
                    edge = &ribbon.bottom; // the drop at the target
                }
                edge->append(next);
                current = next;
            } else if (command == QLatin1String("C")) {
                const QPointF c1 = nextPoint();
                const QPointF c2 = nextPoint();
                const QPointF end = nextPoint();
                constexpr int steps = 32;
                for (int step = 1; step <= steps; ++step) {
                    const qreal t = static_cast<qreal>(step) / steps;
                    const qreal u = 1.0 - t;
                    edge->append(current * (u * u * u) + c1 * (3 * u * u * t) + c2 * (3 * u * t * t) + end * (t * t * t));
                }
                current = end;
            }
        }
        std::reverse(ribbon.bottom.begin(), ribbon.bottom.end());
        return ribbon;
    }

    static qreal yAt(const QList<QPointF> &edge, qreal x)
    {
        for (int i = 1; i < edge.size(); ++i) {
            const QPointF &a = edge.at(i - 1);
            const QPointF &b = edge.at(i);
            if (x >= a.x() && x <= b.x()) {
                return b.x() - a.x() < 1e-9 ? a.y() : a.y() + (b.y() - a.y()) * (x - a.x()) / (b.x() - a.x());
            }
        }
        return x < edge.first().x() ? edge.first().y() : edge.last().y();
    }

    /// Fails if any two ribbons overlap vertically anywhere in the x range
    /// they share, left of xLimit (their shared end points excluded).
    static void verifyNoOverlaps(const SankeyModel &model, qreal xLimit)
    {
        QList<Ribbon> ribbons;
        for (const QVariant &v : model.links()) {
            ribbons.append(parseRibbon(v.toMap()));
        }
        for (int a = 0; a < ribbons.size(); ++a) {
            for (int b = a + 1; b < ribbons.size(); ++b) {
                const Ribbon &ra = ribbons.at(a);
                const Ribbon &rb = ribbons.at(b);
                const qreal lo = qMax(ra.top.first().x(), rb.top.first().x()) + 0.25;
                const qreal hi = std::min({ra.top.last().x(), rb.top.last().x(), xLimit}) - 0.25;
                for (int step = 0; step <= 400 && lo < hi; ++step) {
                    const qreal x = lo + (hi - lo) * step / 400.0;
                    const qreal overlap = qMin(yAt(ra.bottom, x), yAt(rb.bottom, x)) - qMax(yAt(ra.top, x), yAt(rb.top, x));
                    QVERIFY2(overlap < 0.5, qPrintable(QStringLiteral("%1 and %2 overlap by %3px at x=%4").arg(ra.name, rb.name).arg(overlap).arg(x)));
                }
            }
        }
    }

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
