/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "jobstage.h"

#include <QHash>

namespace JobStage
{

QStringList canonicalStages()
{
    static const QStringList stages{
        QStringLiteral("Applied"),
        QStringLiteral("Screening"),
        QStringLiteral("Interview"),
        QStringLiteral("Onsite"),
        QStringLiteral("Offer"),
        QStringLiteral("Accepted"),
        QStringLiteral("Rejected"),
        QStringLiteral("Withdrawn"),
        QStringLiteral("Ghosted"),
    };
    return stages;
}

bool isValid(const QString &stage)
{
    return canonicalStages().contains(stage, Qt::CaseInsensitive);
}

QString canonical(const QString &stage)
{
    if (stage.compare(QLatin1String(Start), Qt::CaseInsensitive) == 0) {
        return QString::fromLatin1(Start);
    }
    for (const QString &candidate : canonicalStages()) {
        if (stage.compare(candidate, Qt::CaseInsensitive) == 0) {
            return candidate;
        }
    }
    return stage;
}

int column(const QString &stage)
{
    static const QHash<QString, int> columns{
        {QStringLiteral("Applied"), 1},
        {QStringLiteral("Screening"), 2},
        {QStringLiteral("Interview"), 3},
        {QStringLiteral("Onsite"), 4},
        {QStringLiteral("Offer"), 5},
        {QStringLiteral("Accepted"), 6},
        {QStringLiteral("Rejected"), 6},
        {QStringLiteral("Withdrawn"), 6},
        {QStringLiteral("Ghosted"), 6},
    };
    if (stage == QLatin1String(Start)) {
        return 0;
    }
    return columns.value(stage, 1);
}

int orderInColumn(const QString &stage)
{
    static const QHash<QString, int> order{
        {QStringLiteral("Accepted"), 0},
        {QStringLiteral("Offer"), 1},
        {QStringLiteral("Onsite"), 2},
        {QStringLiteral("Interview"), 3},
        {QStringLiteral("Screening"), 4},
        {QStringLiteral("Applied"), 5},
        {QStringLiteral("Rejected"), 6},
        {QStringLiteral("Withdrawn"), 7},
        {QStringLiteral("Ghosted"), 8},
    };
    if (stage == QLatin1String(Start)) {
        return -1;
    }
    return order.value(stage, 99);
}

QColor color(const QString &stage)
{
    static const QHash<QString, QColor> colors{
        {QStringLiteral("Applied"), QColor(0x3d, 0xae, 0xe9)},
        {QStringLiteral("Screening"), QColor(0x2e, 0xc4, 0xb6)},
        {QStringLiteral("Interview"), QColor(0x9b, 0x59, 0xb6)},
        {QStringLiteral("Onsite"), QColor(0x8e, 0x44, 0xad)},
        {QStringLiteral("Offer"), QColor(0xf3, 0x9c, 0x12)},
        {QStringLiteral("Accepted"), QColor(0x27, 0xae, 0x60)},
        {QStringLiteral("Rejected"), QColor(0xe7, 0x4c, 0x3c)},
        {QStringLiteral("Withdrawn"), QColor(0x95, 0xa5, 0xa6)},
        {QStringLiteral("Ghosted"), QColor(0x7f, 0x8c, 0x8d)},
    };
    if (stage == QLatin1String(Start)) {
        return QColor(0x5c, 0x63, 0x70);
    }
    return colors.value(stage, QColor(0x5c, 0x63, 0x70));
}

bool isTerminal(const QString &stage)
{
    return stage == QLatin1String("Accepted") || stage == QLatin1String("Rejected") || stage == QLatin1String("Withdrawn")
        || stage == QLatin1String("Ghosted");
}

}
