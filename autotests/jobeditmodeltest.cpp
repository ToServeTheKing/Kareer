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

        setValueOf(edit, u"stage"_s, u"Interview"_s);
        QVERIFY2(edit.save(), qPrintable(edit.lastError()));

        JobsDatabase db;
        QCOMPARE(db.jobById(id)->stage, u"Interview"_s);
        const auto transitions = db.stageTransitions();
        QCOMPARE(transitions.size(), 2);
        QCOMPARE(transitions.last().fromStage, u"Applied"_s);
        QCOMPARE(transitions.last().toStage, u"Interview"_s);
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
