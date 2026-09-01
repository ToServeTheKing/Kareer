/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "jobsdatabase.h"
#include "job.h"

#include <QTemporaryDir>
#include <QtTest>

namespace
{
QString dbPathIn(const QTemporaryDir &dir)
{
    return dir.path() + QStringLiteral("/test.sqlite");
}
}

class JobsDatabaseTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void addAndRetrieve()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        JobsDatabase db(dbPathIn(dir));
        QVERIFY(db.isOpen());

        Job job;
        job.company = QStringLiteral("Acme Corp");
        job.title = QStringLiteral("Software Engineer");
        job.dateApplied = QDate(2026, 6, 1);
        job.salaryMin = 120000;
        job.salaryMax = 150000;

        QVERIFY(db.addJob(job));
        QVERIFY(job.id > 0);
        QCOMPARE(job.stage, QStringLiteral("Applied"));
        QVERIFY(job.createdAt.isValid());

        const auto fetched = db.jobById(job.id);
        QVERIFY(fetched.has_value());
        QCOMPARE(fetched->company, QStringLiteral("Acme Corp"));
        QCOMPARE(fetched->salaryMin, 120000);
        QCOMPARE(fetched->salaryMax, 150000);
        QCOMPARE(fetched->stage, QStringLiteral("Applied"));

        const auto transitions = db.stageTransitions();
        QCOMPARE(transitions.size(), 1);
        QVERIFY(transitions.first().fromStage.isEmpty());
        QCOMPARE(transitions.first().toStage, QStringLiteral("Applied"));
    }

    void rejectsUnknownStage()
    {
        QTemporaryDir dir;
        JobsDatabase db(dbPathIn(dir));

        Job job;
        job.company = QStringLiteral("Acme");
        job.title = QStringLiteral("Engineer");
        job.stage = QStringLiteral("Bogus");

        QVERIFY(!db.addJob(job));
        QVERIFY(!db.lastError().isEmpty());
    }

    void updateJobDoesNotTouchStage()
    {
        QTemporaryDir dir;
        JobsDatabase db(dbPathIn(dir));

        Job job;
        job.company = QStringLiteral("A");
        job.title = QStringLiteral("B");
        QVERIFY(db.addJob(job));

        Job edited = *db.jobById(job.id);
        edited.company = QStringLiteral("Updated Co");
        edited.stage = QStringLiteral("Rejected"); // updateJob must ignore this
        QVERIFY(db.updateJob(edited));

        const auto fetched = db.jobById(job.id);
        QCOMPARE(fetched->company, QStringLiteral("Updated Co"));
        QCOMPARE(fetched->stage, QStringLiteral("Applied"));
    }

    void stageTransitionsRecordHistory()
    {
        QTemporaryDir dir;
        JobsDatabase db(dbPathIn(dir));

        Job job;
        job.company = QStringLiteral("A");
        job.title = QStringLiteral("B");
        QVERIFY(db.addJob(job));

        QVERIFY(db.setStage(job.id, QStringLiteral("Screening")));
        QVERIFY(db.setStage(job.id, QStringLiteral("Screening"))); // no-op: same stage
        QVERIFY(db.setStage(job.id, QStringLiteral("Rejected")));

        const auto transitions = db.stageTransitions();
        // Start->Applied, Applied->Screening, Screening->Rejected (the no-op adds nothing)
        QCOMPARE(transitions.size(), 3);
        QCOMPARE(transitions.at(1).fromStage, QStringLiteral("Applied"));
        QCOMPARE(transitions.at(1).toStage, QStringLiteral("Screening"));
        QCOMPARE(transitions.at(2).fromStage, QStringLiteral("Screening"));
        QCOMPARE(transitions.at(2).toStage, QStringLiteral("Rejected"));

        const auto fetched = db.jobById(job.id);
        QCOMPARE(fetched->stage, QStringLiteral("Rejected"));
    }

    void setStageRejectsUnknownStage()
    {
        QTemporaryDir dir;
        JobsDatabase db(dbPathIn(dir));

        Job job;
        job.company = QStringLiteral("A");
        job.title = QStringLiteral("B");
        QVERIFY(db.addJob(job));

        QVERIFY(!db.setStage(job.id, QStringLiteral("Bogus")));
        QCOMPARE(db.jobById(job.id)->stage, QStringLiteral("Applied"));
    }

    void deleteCascadesHistory()
    {
        QTemporaryDir dir;
        JobsDatabase db(dbPathIn(dir));

        Job job;
        job.company = QStringLiteral("A");
        job.title = QStringLiteral("B");
        QVERIFY(db.addJob(job));
        QVERIFY(db.setStage(job.id, QStringLiteral("Screening")));

        QVERIFY(db.deleteJob(job.id));
        QVERIFY(!db.jobById(job.id).has_value());
        QVERIFY(db.stageTransitions().isEmpty());
    }
};

QTEST_GUILESS_MAIN(JobsDatabaseTest)
#include "jobsdatabasetest.moc"
