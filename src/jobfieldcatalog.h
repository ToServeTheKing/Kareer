/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/**
 * The static structure of the job edit form: which categories exist, and
 * which fields belong to each, in display order. Mirrors FlatKontrol's
 * PermissionCatalog - JobEditModel supplies the per-job values, this
 * supplies the shape.
 */
namespace JobFieldCatalog
{
enum RowType {
    TextRow = 0,
    ComboRow,
    SpinBoxRow,
    DateRow,
    TextAreaRow,
};

struct Category {
    QString id;
    QString title;
};

struct Field {
    QString id; ///< Matches a Job/JobsModel field key (see JobsModel::mapFromJob).
    QString categoryId;
    QString label;
    RowType rowType;
    QStringList comboOptions; ///< Only meaningful for ComboRow.
    QString placeholder; ///< Only meaningful for TextRow.
    int spinMin = 0; ///< Only meaningful for SpinBoxRow.
    int spinMax = 0;
};

QList<Category> categories();
QList<Field> fields();
}
