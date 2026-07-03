/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "job.h"
#include "jobsdatabase.h"
#include "sankeymodel.h"

#include <QTemporaryDir>
#include <QtTest>
#include <memory>

// SankeyModel always opens JobsDatabase::defaultPath(), so each test points
// that at a fresh temporary file via the KAREER_DB_PATH override.
class SankeyLayoutTest : public QObject
{
    Q_OBJECT

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

        auto makeJob = [&](const QString &stage) {
            Job job;
            job.company = QStringLiteral("Co");
            job.title = QStringLiteral("Title");
            QVERIFY(db.addJob(job));
            if (stage != QStringLiteral("Applied")) {
                QVERIFY(db.setStage(job.id, stage));
            }
        };

        makeJob(QStringLiteral("Applied"));
        makeJob(QStringLiteral("Applied"));
        makeJob(QStringLiteral("Screening"));
        makeJob(QStringLiteral("Rejected"));

        SankeyModel model;
        model.relayout(600, 400);

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
        model.relayout(400, 300);
        QVERIFY(model.isEmpty());
        QVERIFY(model.nodes().isEmpty());
        QVERIFY(model.links().isEmpty());
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
};

QTEST_GUILESS_MAIN(SankeyLayoutTest)
#include "sankeylayouttest.moc"
