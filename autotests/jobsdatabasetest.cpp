/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "jobsdatabase.h"
#include "job.h"

#include <QSqlDatabase>
#include <QSqlQuery>
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

    void ghostsStaleApplications()
    {
        QTemporaryDir dir;
        const QString path = dbPathIn(dir);
        JobsDatabase db(path);
        const QDateTime now(QDate(2026, 9, 11), QTime(12, 0), QTimeZone::UTC);
        const auto daysAgo = [&](int days) {
            return now.addDays(-days);
        };

        // Still at Applied, nothing for 40 days: ghosted.
        const int stale = addJob(db, {});
        backdate(path, stale, {daysAgo(40)}, daysAgo(40).date());
        // Applied 10 days ago: too recent.
        const int fresh = addJob(db, {});
        backdate(path, fresh, {daysAgo(10)}, daysAgo(10).date());
        // Got a response (Screening) 40 days ago: not ours to ghost.
        const int screening = addJob(db, {QStringLiteral("Screening")});
        backdate(path, screening, {daysAgo(45), daysAgo(40)}, daysAgo(45).date());
        // Applied 60 days ago but only logged 5 days ago: the month starts at logging.
        const int loggedLate = addJob(db, {});
        backdate(path, loggedLate, {daysAgo(5)}, daysAgo(60).date());
        // Logged 40 days ago, date applied later edited to 10 days ago.
        const int appliedLater = addJob(db, {});
        backdate(path, appliedLater, {daysAgo(40)}, daysAgo(10).date());
        // Ghosted before, then moved back to Applied 5 days ago: a fresh month.
        const int reopened = addJob(db, {QStringLiteral("Ghosted"), QStringLiteral("Applied")});
        backdate(path, reopened, {daysAgo(60), daysAgo(50), daysAgo(5)}, daysAgo(60).date());

        QCOMPARE(db.ghostStaleApplications(0, now), 0);
        QCOMPARE(db.ghostStaleApplications(30, now), 1);

        QCOMPARE(db.jobById(stale)->stage, QStringLiteral("Ghosted"));
        for (int id : {fresh, loggedLate, appliedLater, reopened}) {
            QCOMPARE(db.jobById(id)->stage, QStringLiteral("Applied"));
        }
        QCOMPARE(db.jobById(screening)->stage, QStringLiteral("Screening"));

        // Recorded like any other move, so the pipeline shows it.
        bool recorded = false;
        for (const StageTransition &t : db.stageTransitions()) {
            recorded |= t.jobId == stale && t.fromStage == QStringLiteral("Applied") && t.toStage == QStringLiteral("Ghosted");
        }
        QVERIFY(recorded);

        // Nothing left to do on a second run; a shorter threshold catches more.
        QCOMPARE(db.ghostStaleApplications(30, now), 0);
        QCOMPARE(db.ghostStaleApplications(7, now), 2); // fresh and appliedLater, both 10 days
    }

    // Logging an application after the fact: Applied, Interview, Rejected.
    void replacesStageHistory()
    {
        QTemporaryDir dir;
        JobsDatabase db(dbPathIn(dir));
        const int id = addJob(db, {});
        const auto at = [](int month, int day) {
            return QDateTime(QDate(2026, month, day), QTime(12, 0));
        };

        // Out of order and with a repeated stage, as an editor might hand it over.
        QVERIFY2(db.replaceStageHistory(id,
                                        {{QStringLiteral("rejected"), at(8, 20)},
                                         {QStringLiteral("Applied"), at(8, 1)},
                                         {QStringLiteral("Interview"), at(8, 15)},
                                         {QStringLiteral("Interview"), at(8, 16)}}),
                 qPrintable(db.lastError()));

        const QList<StageStep> steps = db.stageHistory(id);
        QCOMPARE(steps.size(), 3);
        QCOMPARE(steps.at(0).stage, QStringLiteral("Applied"));
        QCOMPARE(steps.at(1).stage, QStringLiteral("Interview"));
        QCOMPARE(steps.at(2).stage, QStringLiteral("Rejected"));
        QCOMPARE(steps.at(1).at.toLocalTime().date(), QDate(2026, 8, 15));

        const auto job = db.jobById(id);
        QCOMPARE(job->stage, QStringLiteral("Rejected"));
        QCOMPARE(job->dateApplied, QDate(2026, 8, 1));

        // The moves chain from the synthetic Start, so the pipeline sees the path.
        QStringList moves;
        for (const StageTransition &t : db.stageTransitions()) {
            if (t.jobId == id) {
                moves.append((t.fromStage.isEmpty() ? QStringLiteral("Start") : t.fromStage) + QStringLiteral(">") + t.toStage);
            }
        }
        QCOMPARE(moves, (QStringList{QStringLiteral("Start>Applied"), QStringLiteral("Applied>Interview"), QStringLiteral("Interview>Rejected")}));

        // Bad input changes nothing.
        QVERIFY(!db.replaceStageHistory(id, {}));
        QVERIFY(!db.replaceStageHistory(id, {{QStringLiteral("Applied"), at(8, 1)}, {QStringLiteral("Bogus"), at(8, 2)}}));
        QVERIFY(!db.replaceStageHistory(id, {{QStringLiteral("Applied"), QDateTime()}}));
        QVERIFY(!db.replaceStageHistory(9999, {{QStringLiteral("Applied"), at(8, 1)}}));
        QCOMPARE(db.stageHistory(id).size(), 3);
        QCOMPARE(db.jobById(id)->stage, QStringLiteral("Rejected"));
    }

private:
    static int addJob(JobsDatabase &db, const QStringList &stages)
    {
        Job job;
        job.company = QStringLiteral("Co");
        job.title = QStringLiteral("Title");
        if (!db.addJob(job)) {
            return -1;
        }
        for (const QString &stage : stages) {
            db.setStage(job.id, stage);
        }
        return job.id;
    }

    /// Rewrites a job's history timestamps (oldest first, one per recorded
    /// move) and its date applied, through a separate connection.
    static void backdate(const QString &path, int jobId, const QList<QDateTime> &history, const QDate &applied)
    {
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("backdate"));
            raw.setDatabaseName(path);
            QVERIFY(raw.open());
            QSqlQuery ids(raw);
            QVERIFY(ids.exec(QStringLiteral("SELECT id FROM stage_history WHERE job_id = %1 ORDER BY id").arg(jobId)));
            QList<int> rows;
            while (ids.next()) {
                rows.append(ids.value(0).toInt());
            }
            QCOMPARE(rows.size(), history.size());
            QSqlQuery update(raw);
            for (int i = 0; i < rows.size(); ++i) {
                update.prepare(QStringLiteral("UPDATE stage_history SET changed_at = ? WHERE id = ?"));
                update.addBindValue(history.at(i).toString(Qt::ISODate));
                update.addBindValue(rows.at(i));
                QVERIFY(update.exec());
            }
            update.prepare(QStringLiteral("UPDATE jobs SET date_applied = ?, created_at = ? WHERE id = ?"));
            update.addBindValue(applied.toString(Qt::ISODate));
            update.addBindValue(history.first().toString(Qt::ISODate));
            update.addBindValue(jobId);
            QVERIFY(update.exec());
            raw.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("backdate"));
    }
};

QTEST_GUILESS_MAIN(JobsDatabaseTest)
#include "jobsdatabasetest.moc"
