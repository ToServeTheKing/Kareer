/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "jobfieldcatalog.h"
#include "jobstage.h"

using namespace Qt::Literals::StringLiterals;

namespace JobFieldCatalog
{

QList<Category> categories()
{
    return {
        {u"company"_s, QStringLiteral("Company")},
        {u"pipeline"_s, QStringLiteral("Pipeline")},
        {u"salary"_s, QStringLiteral("Salary")},
        {u"details"_s, QStringLiteral("Additional Details")},
    };
}

QList<Field> fields()
{
    return {
        {u"company"_s, u"company"_s, QStringLiteral("Company:"), TextRow, {}, {}, 0, 0},
        {u"title"_s, u"company"_s, QStringLiteral("Job Title:"), TextRow, {}, {}, 0, 0},
        {u"location"_s, u"company"_s, QStringLiteral("Location:"), TextRow, {}, {}, 0, 0},
        {u"remoteType"_s, u"company"_s, QStringLiteral("Remote Type:"), ComboRow, {QStringLiteral("Unspecified"), QStringLiteral("Onsite"), QStringLiteral("Hybrid"), QStringLiteral("Remote")}, {}, 0, 0},

        {u"stage"_s, u"pipeline"_s, QStringLiteral("Stage:"), ComboRow, JobStage::canonicalStages(), {}, 0, 0},
        {u"dateApplied"_s, u"pipeline"_s, QStringLiteral("Date Applied:"), DateRow, {}, {}, 0, 0},

        {u"salaryMin"_s, u"salary"_s, QStringLiteral("Range Minimum:"), SpinBoxRow, {}, {}, 0, 5000000},
        {u"salaryMax"_s, u"salary"_s, QStringLiteral("Range Maximum:"), SpinBoxRow, {}, {}, 0, 5000000},
        {u"salaryExpectation"_s, u"salary"_s, QStringLiteral("Your Expectation:"), SpinBoxRow, {}, {}, 0, 5000000},
        {u"currency"_s, u"salary"_s, QStringLiteral("Currency:"), TextRow, {}, {}, 0, 0},

        {u"source"_s, u"details"_s, QStringLiteral("Source:"), TextRow, {}, QStringLiteral("Referral, LinkedIn, company site..."), 0, 0},
        {u"url"_s, u"details"_s, QStringLiteral("Job Posting URL:"), TextRow, {}, {}, 0, 0},
        {u"contact"_s, u"details"_s, QStringLiteral("Contact:"), TextRow, {}, {}, 0, 0},
        {u"notes"_s, u"details"_s, QStringLiteral("Notes:"), TextAreaRow, {}, {}, 0, 0},
    };
}

}
