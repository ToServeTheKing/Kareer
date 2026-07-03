// SPDX-License-Identifier: GPL-3.0-or-later
#include "kareer-version.h"

#include "clicommands.h"

#include <KAboutData>
#include <KCrash>
#include <KIconTheme>
#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

using namespace Qt::Literals::StringLiterals;

// Filter out a couple of well-known benign framework artifacts rather than
// spamming every run. Everything else is passed through untouched.
static QtMessageHandler s_defaultMessageHandler = nullptr;
static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // Qt Quick emits this while Kirigami's PageRow incubates pages. It fires even
    // for a trivial empty page, is harmless, and cannot be avoided from app code.
    if (message.contains(QLatin1String("was not placed in the graphics scene"))) {
        return;
    }
    // Malformed path data in third-party icon SVGs (system/app icon themes) is not
    // actionable from here; drop the noise rather than spam every render.
    if (context.category && qstrcmp(context.category, "qt.svg") == 0) {
        return;
    }
    // Qt's Wayland integration tries to self-register with xdg-desktop-portal for
    // optional desktop features (global shortcuts, background). Kareer doesn't use
    // any of those, and it fires harmlessly on hosts where portal app-info
    // resolution is finicky.
    if (message.contains(QLatin1String("Failed to register with host portal"))) {
        return;
    }
    if (s_defaultMessageHandler) {
        s_defaultMessageHandler(type, context, message);
    }
}

int main(int argc, char *argv[])
{
    s_defaultMessageHandler = qInstallMessageHandler(messageHandler);

    if (argc >= 2 && Cli::isSubcommand(QString::fromLocal8Bit(argv[1]))) {
        QCoreApplication app(argc, argv);
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
    parser.process(app);
    aboutData.processCommandLine(&parser);

    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);

    engine.loadFromModule("io.github.toservetheking.Kareer", u"Main"_s);
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
