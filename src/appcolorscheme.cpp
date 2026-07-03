/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "appcolorscheme.h"

#include <KColorSchemeManager>

AppColorScheme::AppColorScheme(QObject *parent)
    : QObject(parent)
    , mSchemes(KColorSchemeManager::instance())
{
}

QAbstractItemModel *AppColorScheme::colorSchemesModel()
{
    return mSchemes->model();
}

QString AppColorScheme::activeColorSchemeName() const
{
    return mSchemes->activeSchemeName();
}

void AppColorScheme::setActiveColorSchemeName(const QString &name)
{
    if (name == activeColorSchemeName()) {
        return;
    }
    mSchemes->activateScheme(mSchemes->indexForScheme(name));
    Q_EMIT activeColorSchemeNameChanged();
}
