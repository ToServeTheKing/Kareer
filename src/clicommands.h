/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QString>
#include <QStringList>

class QCoreApplication;

/**
 * Headless command-line interface: `kareer add|list|show|update|stage|history|delete|stats|stages|sync ...`.
 * Lets other tools (a resume generator, a shell script) log and query
 * applications without ever starting the Kirigami GUI.
 */
namespace Cli
{
/// True if the first non-option argument names one of our subcommands, i.e.
/// whether main() should route to run() instead of starting the GUI.
bool isSubcommand(const QString &arg);

/// Dispatches to the matching subcommand handler and returns a process exit
/// code. Once signed in to a sync server, syncs before and after.
int run(QCoreApplication &app);

/// The dispatch itself, without syncing.
int runCommand(const QString &program, const QString &subcommand, const QStringList &rest);
}
