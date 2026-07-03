/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>

/// A single job application and everything tracked about it.
struct Job {
    int id = -1;
    QString company;
    QString title;
    QString location;
    QString remoteType; ///< "Onsite", "Hybrid", "Remote", or empty if unknown.
    QString source; ///< Where the lead came from (referral, LinkedIn, ...).
    QString url;
    QDate dateApplied;
    int salaryMin = -1; ///< -1 means unset.
    int salaryMax = -1;
    int salaryExpectation = -1;
    QString currency = QStringLiteral("USD");
    QString notes; ///< Free text: expectations, interview notes, etc.
    QString contact;
    QString stage;
    QDateTime createdAt;
    QDateTime updatedAt;
};

/// One recorded move from one stage to another (or from "Start" for the
/// initial application), used to build the Sankey diagram.
struct StageTransition {
    int jobId = -1;
    QString fromStage; ///< Empty means JobStage::Start.
    QString toStage;
    QDateTime changedAt;
};
