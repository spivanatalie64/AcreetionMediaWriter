/*
 * AcreetionOS Media Writer
 * Copyright (C) 2026 Natalie <natalie@acreetionos.org>
 * Copyright (C) 2024 Jan Grulich <jgrulichredhat.com>
 * Copyright (C) 2016 Martin Bříza <mbriza@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <cstdlib>

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStyleHints>
#include <QTranslator>
#ifdef Q_OS_LINUX
#include <QGuiApplication>
#endif

#include "crashhandler.h"
#include "drivemanager.h"
#include "portalfiledialog.h"
#include "releasemanager.h"

int main(int argc, char **argv)
{
    MessageHandler::install();
    CrashHandler::install();

#ifdef __linux
    // Detect desktop environment for optimal configuration
    const QString desktopEnv = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toUpper();
    const QString sessionType = qEnvironmentVariable("XDG_SESSION_TYPE").toLower();
    const bool isCinnamon = desktopEnv.contains("CINNAMON");

    // Use GTK3 platform theme for native integration on Cinnamon/GNOME/etc
    // This makes Qt apps respect the system GTK theme (dark/light mode, fonts, etc.)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME")) {
        qputenv("QT_QPA_PLATFORMTHEME", "gtk3");
    }

    // Wayland support for Cinnamon on Wayland sessions
    if (sessionType == "wayland") {
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
            qputenv("QT_QPA_PLATFORM", "wayland;xcb");
        }
    }

    // Use threaded renderer for smoother animations on modern hardware
    if (qEnvironmentVariableIsEmpty("QSG_RENDER_LOOP")) {
        if (isCinnamon) {
            // Cinnamon's Muffin compositor works well with threaded rendering
            qputenv("QSG_RENDER_LOOP", "threaded");
        }
    }

    // Force Qt Quick to use the RHI backend (hardware-accelerated) when possible
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND")) {
        qputenv("QT_QUICK_BACKEND", "rhi");
    }

    // Log system info for debugging
    qInfo().noquote() << "[Sprungles] Desktop integration:" << (isCinnamon ? "Cinnamon" : desktopEnv);
    qInfo().noquote() << "[Sprungles] Session type:" << sessionType;
    qInfo().noquote() << "[Sprungles] QT_QPA_PLATFORMTHEME=gtk3 enabled for native theming";
#endif

    // Respect system dark mode preference via GTK3 theme integration

    QApplication::setOrganizationDomain("acreetionos.org");
    QApplication::setOrganizationName("acreetionos.org");
    QApplication::setApplicationName("AcreetionMediaWriter");

    QApplication app(argc, argv);
    const int parseResult = options.parse(app.arguments());
    if (parseResult >= 0) {
        return parseResult;
    }

    mDebug() << "Application constructed";

    QTranslator translator;
    QLocale locale(QLocale::system().language(), QLocale::system().script(), QLocale::system().territory());
    if (translator.load(locale, QLatin1String(), QLatin1String(), ":/translations")) {
        mDebug() << "Localization " << locale;
        app.installTranslator(&translator);
    }

    QGuiApplication::setDesktopFileName("org.acreetionos.MediaWriter");

    mDebug() << "Injecting QML context properties";
    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("downloadManager", DownloadManager::instance());
    engine.rootContext()->setContextProperty("drives", DriveManager::instance());
    engine.rootContext()->setContextProperty("portalFileDialog", new PortalFileDialog(&app));
    engine.rootContext()->setContextProperty("mediawriterVersion", MEDIAWRITER_VERSION);
    engine.rootContext()->setContextProperty("releases", new ReleaseManager());

    mDebug() << "Loading the QML source code";

    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));

    mDebug() << "Starting the application";
    int status = app.exec();
    mDebug() << "Quitting with status" << status;

    return status;
}
