/*
    SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kareer-version.h"

#include "clicommands.h"
#include "databaselocation.h"
#include "jobsdatabase.h"

#include <KAboutData>
#include <KCrash>
#include <KIconTheme>
#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

using namespace Qt::Literals::StringLiterals;

// Filter out a couple of well-known benign framework artifacts rather than
// spamming every run. Everything else is passed through untouched.
static bool isBenignFrameworkNoise(const QString &message)
{
    // Qt Quick emits this while Kirigami's PageRow incubates pages. It fires even
    // for a trivial empty page, is harmless, and cannot be avoided from app code.
    if (message.contains(QLatin1String("was not placed in the graphics scene"))) {
        return true;
    }
    // An internal PageRow/StackView implementation detail, not something app
    // code can influence.
    if (message.contains(QLatin1String("StackView has detected conflicting anchors"))) {
        return true;
    }
    // Qt's Wayland integration tries to self-register with xdg-desktop-portal for
    // optional desktop features (global shortcuts, background). Kareer doesn't use
    // any of those, and it fires harmlessly on hosts where portal app-info
    // resolution is finicky.
    if (message.contains(QLatin1String("Failed to register with host portal"))) {
        return true;
    }
    return false;
}

static QtMessageHandler s_defaultMessageHandler = nullptr;
static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (isBenignFrameworkNoise(message)) {
        return;
    }
    // Malformed path data in third-party icon SVGs (system/app icon themes) is not
    // actionable from here; drop the noise rather than spam every render.
    if (context.category && qstrcmp(context.category, "qt.svg") == 0) {
        return;
    }
    if (s_defaultMessageHandler) {
        s_defaultMessageHandler(type, context, message);
    }
}

/// Removes "--db <path>" / "--db=<path>" from argv (anywhere on the command
/// line, for both the GUI and every subcommand) and applies it as the
/// database override. Stripping it up front keeps "kareer --db x list"
/// routing to the CLI. Returns false if --db is missing its value.
static bool takeDbOption(int &argc, char **argv)
{
    int out = 1;
    for (int in = 1; in < argc; ++in) {
        const QString arg = QString::fromLocal8Bit(argv[in]);
        QString value;
        if (arg == u"--db"_s) {
            if (in + 1 >= argc) {
                fprintf(stderr, "kareer: --db requires a path\n");
                return false;
            }
            value = QString::fromLocal8Bit(argv[++in]);
        } else if (arg.startsWith(u"--db="_s)) {
            value = arg.mid(5);
        } else {
            argv[out++] = argv[in];
            continue;
        }
        if (value.isEmpty()) {
            fprintf(stderr, "kareer: --db requires a path\n");
            return false;
        }
        JobsDatabase::setPathOverride(QFileInfo(value).absoluteFilePath());
    }
    argv[out] = nullptr;
    argc = out;
    return true;
}

int main(int argc, char *argv[])
{
    s_defaultMessageHandler = qInstallMessageHandler(messageHandler);

    if (!takeDbOption(argc, argv)) {
        return 1;
    }

    if (argc >= 2 && Cli::isSubcommand(QString::fromLocal8Bit(argv[1]))) {
        QCoreApplication app(argc, argv);
        DatabaseLocation::loadConfiguredPath();
        return Cli::run(app);
    }

    KIconTheme::initTheme();

    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("kareer"));
    QCoreApplication::setOrganizationName(u"toservetheking"_s);

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(u"org.kde.desktop"_s);
        QQuickStyle::setFallbackStyle(u"Fusion"_s);
    }

    KAboutData aboutData(u"kareer"_s,
                         i18nc("@title", "Kareer"),
                         QStringLiteral(KAREER_VERSION_STRING),
                         i18n("Track your job applications"),
                         KAboutLicense::GPL_V3,
                         i18n("© 2026 Kareer contributors"));
    aboutData.addAuthor(u"toservetheking"_s, i18nc("@label", "Author"), u"austin@thebennett.net"_s);
    aboutData.setDesktopFileName(u"io.github.toservetheking.Kareer"_s);
    KAboutData::setApplicationData(aboutData);

    QApplication::setWindowIcon(QIcon::fromTheme(u"io.github.toservetheking.Kareer"_s, QIcon::fromTheme(u"office-address-book"_s)));

    KCrash::initialize();

    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);
    // Handled (and stripped) by takeDbOption(); declared here for --help.
    parser.addOption(QCommandLineOption(u"db"_s, i18n("Use this database file instead of the configured one."), i18n("path")));
    parser.process(app);
    aboutData.processCommandLine(&parser);

    DatabaseLocation::loadConfiguredPath();
    // First run (or the configured file has gone missing): open nothing until
    // the user picks a location in DatabaseSetupDialog. The CLI never waits.
    JobsDatabase::setSelectionPending(DatabaseLocation::needsSetup());

    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);

    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine, [](const QList<QQmlError> &warnings) {
        for (const QQmlError &error : warnings) {
            if (isBenignFrameworkNoise(error.description())) {
                continue;
            }
            fprintf(stderr, "QML-WARNING: %s\n", qPrintable(error.toString()));
        }
        fflush(stderr);
    });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &engine, [](const QUrl &url) {
        fprintf(stderr, "QML-OBJECT-CREATION-FAILED: %s\n", qPrintable(url.toString()));
        fflush(stderr);
    });

    engine.loadFromModule("io.github.toservetheking.Kareer", u"Main"_s);
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
