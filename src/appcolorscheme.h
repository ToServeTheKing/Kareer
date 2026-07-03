/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QAbstractItemModel>
#include <QObject>
#include <QQmlEngine>

class KColorSchemeManager;

/**
 * Thin QML-facing wrapper around KColorSchemeManager: exposes the list of
 * installed color schemes and the currently active one. KColorSchemeManager
 * autosaves the active scheme itself, so there is nothing else to persist.
 */
class AppColorScheme : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QAbstractItemModel *colorSchemesModel READ colorSchemesModel CONSTANT)
    Q_PROPERTY(QString activeColorSchemeName READ activeColorSchemeName WRITE setActiveColorSchemeName NOTIFY activeColorSchemeNameChanged)

public:
    explicit AppColorScheme(QObject *parent = nullptr);

    QAbstractItemModel *colorSchemesModel();
    QString activeColorSchemeName() const;
    void setActiveColorSchemeName(const QString &name);

Q_SIGNALS:
    void activeColorSchemeNameChanged();

private:
    KColorSchemeManager *mSchemes;
};
