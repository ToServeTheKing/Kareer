/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "jobeditmodel.h"
#include "job.h"
#include "jobsdatabase.h"
#include "jobsmodel.h"

#include <QTemporaryDir>
#include <QtTest>
#include <memory>

using namespace Qt::Literals::StringLiterals;

// JobsModel always opens JobsDatabase::defaultPath(), so each test points
// that at a fresh temporary file via the KAREER_DB_PATH override.
class JobEditModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        qputenv("KAREER_DB_PATH", (m_dir->path() + u"/test.sqlite"_s).toUtf8());
    }

    // QML does not guarantee the order ApplicationEditPage's bindings assign
    // editingJobId and jobsModel; the form must load the job either way.
    void loadsJobWhenIdIsSetBeforeModel()
    {
        const int id = seedJob();

        JobsModel jobs;
        JobEditModel edit;
        edit.setEditingJobId(id);
        edit.setJobsModel(&jobs);

        QCOMPARE(valueOf(edit, u"company"_s).toString(), u"Acme Corp"_s);
        QCOMPARE(valueOf(edit, u"title"_s).toString(), u"Engineer"_s);
        QCOMPARE(valueOf(edit, u"salaryMin"_s).toInt(), 100000);

        // The history's first step shows the date applied, not when it was logged.
        StageHistoryModel *history = edit.history();
        QCOMPARE(history->rowCount(), 1);
        QCOMPARE(history->data(history->index(0), StageHistoryModel::StageRole).toString(), u"Applied"_s);
        QCOMPARE(history->data(history->index(0), StageHistoryModel::DateRole).toDate(), QDate(2026, 6, 1));
    }

    void savesEditsToExistingJob()
    {
        const int id = seedJob();

        JobsModel jobs;
        JobEditModel edit;
        edit.setEditingJobId(id);
        edit.setJobsModel(&jobs);

        setValueOf(edit, u"title"_s, u"Senior Engineer"_s);
        setValueOf(edit, u"notes"_s, u"Second round scheduled"_s);
        QVERIFY2(edit.save(), qPrintable(edit.lastError()));
        QVERIFY(edit.lastError().isEmpty());

        JobsDatabase db;
        const auto job = db.jobById(id);
        QVERIFY(job.has_value());
        QCOMPARE(job->company, u"Acme Corp"_s);
        QCOMPARE(job->title, u"Senior Engineer"_s);
        QCOMPARE(job->notes, u"Second round scheduled"_s);
        // Untouched salaries must survive the round trip through the form.
        QCOMPARE(job->salaryMin, 100000);
        QCOMPARE(job->salaryMax, -1);
    }

    void stageChangeIsSavedWithHistory()
    {
        const int id = seedJob();

        JobsModel jobs;
        JobEditModel edit;
        edit.setEditingJobId(id);
        edit.setJobsModel(&jobs);

        // Moving the application on is adding a step to its history.
        StageHistoryModel *history = edit.history();
        history->appendStep();
        history->setStage(history->rowCount() - 1, u"Interview"_s);
        QVERIFY2(edit.save(), qPrintable(edit.lastError()));

        JobsDatabase db;
        QCOMPARE(db.jobById(id)->stage, u"Interview"_s);
        QCOMPARE(db.jobById(id)->dateApplied, QDate(2026, 6, 1));
        const auto transitions = db.stageTransitions();
        QCOMPARE(transitions.size(), 2);
        QCOMPARE(transitions.last().fromStage, u"Applied"_s);
        QCOMPARE(transitions.last().toStage, u"Interview"_s);
    }

    // Applied, interviewed, then rejected: logged in one go after the fact.
    void logsApplicationAfterTheFact()
    {
        JobsModel jobs;
        JobEditModel edit;
        edit.setJobsModel(&jobs);
        setValueOf(edit, u"company"_s, u"Allstate"_s);
        setValueOf(edit, u"title"_s, u"Software Engineer"_s);

        StageHistoryModel *history = edit.history();
        const auto day = [](int month, int dayOfMonth) {
            return QDateTime(QDate(2026, month, dayOfMonth), QTime(9, 0));
        };
        history->setDate(0, day(8, 1));
        history->appendStep();
        history->setStage(1, u"Interview"_s);
        history->setDate(1, day(8, 15));
        history->appendStep();
        history->setStage(2, u"Rejected"_s);
        history->setDate(2, day(8, 20));
        QVERIFY2(edit.save(), qPrintable(edit.lastError()));

        JobsDatabase db;
        const QList<Job> all = db.allJobs();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all.first().stage, u"Rejected"_s);
        QCOMPARE(all.first().dateApplied, QDate(2026, 8, 1));
        const QList<StageStep> steps = db.stageHistory(all.first().id);
        QCOMPARE(steps.size(), 3);
        QCOMPARE(steps.at(1).stage, u"Interview"_s);
        QCOMPARE(steps.at(1).at.toLocalTime().date(), QDate(2026, 8, 15));
        QCOMPARE(steps.at(2).at.toLocalTime().date(), QDate(2026, 8, 20));
    }

    void addsNewJob()
    {
        JobsModel jobs;
        JobEditModel edit;
        edit.setJobsModel(&jobs);

        setValueOf(edit, u"company"_s, u"Globex"_s);
        setValueOf(edit, u"title"_s, u"Designer"_s);
        QVERIFY2(edit.save(), qPrintable(edit.lastError()));
        QCOMPARE(jobs.rowCount(), 1);

        JobsDatabase db;
        const QList<Job> all = db.allJobs();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all.first().company, u"Globex"_s);
        QCOMPARE(all.first().stage, u"Applied"_s);
    }

    // A new form's text fields must start empty; a missing value showed up
    // in QML as the word "undefined".
    void newFormFieldsStartEmpty()
    {
        JobsModel jobs;
        JobEditModel edit;
        edit.setJobsModel(&jobs);
        for (int row = 0; row < edit.rowCount(); ++row) {
            const QVariant value = edit.data(edit.index(row), JobEditModel::ValueRole);
            const QString id = edit.data(edit.index(row), JobEditModel::FieldIdRole).toString();
            QVERIFY2(value.isValid(), qPrintable(id));
        }
        for (const QString &id : {u"company"_s, u"title"_s, u"location"_s, u"source"_s, u"url"_s, u"contact"_s, u"notes"_s}) {
            const QVariant value = valueOf(edit, id);
            QCOMPARE(value.typeId(), QMetaType::QString);
            QVERIFY2(value.toString().isEmpty(), qPrintable(id));
        }
        QCOMPARE(valueOf(edit, u"currency"_s).toString(), u"USD"_s);
        QCOMPARE(valueOf(edit, u"remoteType"_s).toString(), u"Unspecified"_s);
    }

    void missingCompanyIsReported()
    {
        JobsModel jobs;
        JobEditModel edit;
        edit.setJobsModel(&jobs);

        setValueOf(edit, u"title"_s, u"Designer"_s);
        QVERIFY(!edit.save());
        QVERIFY(!edit.lastError().isEmpty());
        QCOMPARE(jobs.rowCount(), 0);

        // A later successful save clears the error.
        setValueOf(edit, u"company"_s, u"Globex"_s);
        QVERIFY(edit.save());
        QVERIFY(edit.lastError().isEmpty());
    }

    void saveWithoutModelReportsError()
    {
        JobEditModel edit;
        QVERIFY(!edit.save());
        QVERIFY(!edit.lastError().isEmpty());
    }

private:
    int seedJob()
    {
        JobsDatabase db;
        Job job;
        job.company = u"Acme Corp"_s;
        job.title = u"Engineer"_s;
        job.dateApplied = QDate(2026, 6, 1);
        job.salaryMin = 100000;
        job.stage = u"Applied"_s;
        if (!db.addJob(job)) {
            qWarning() << db.lastError();
            return -1;
        }
        return job.id;
    }

    static int rowOf(const JobEditModel &edit, const QString &fieldId)
    {
        for (int row = 0; row < edit.rowCount(); ++row) {
            if (edit.data(edit.index(row), JobEditModel::FieldIdRole).toString() == fieldId) {
                return row;
            }
        }
        return -1;
    }

    static QVariant valueOf(const JobEditModel &edit, const QString &fieldId)
    {
        return edit.data(edit.index(rowOf(edit, fieldId)), JobEditModel::ValueRole);
    }

    static void setValueOf(JobEditModel &edit, const QString &fieldId, const QVariant &value)
    {
        edit.setValue(rowOf(edit, fieldId), value);
    }

    std::unique_ptr<QTemporaryDir> m_dir;
};

QTEST_GUILESS_MAIN(JobEditModelTest)

#include "jobeditmodeltest.moc"
