// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

// Checks the desktop integration of a build (e.g. the AppImage's bundled Qt):
// prints the widget style, font, icon theme and colours Qt picked, and saves
// screenshots of the main window and a file dialog.
//   theme_shot <out-dir> <name-prefix>
#include "core/VmLibrary.h"
#include "display/GlibIntegration.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QStyle>
#include <QTimer>

#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("bento-vm"));
    QApplication::setApplicationDisplayName(QStringLiteral("BentoVM"));
    QApplication::setDesktopFileName(QStringLiteral("io.github.hyperstown.BentoVM"));
    if (argc < 3)
        return 2;
    const QString out = QString::fromLocal8Bit(argv[1]);
    const QString prefix = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(out);

    GlibIntegration::ensure();
    Theme::instance()->init();
    Theme::instance()->apply(QStringLiteral("system"));
    VmLibrary::instance()->refresh();

    std::printf("style:       %s\n", qPrintable(QApplication::style()->name()));
    std::printf("font:        %s\n", qPrintable(QApplication::font().toString()));
    std::printf("icon theme:  %s\n", qPrintable(QIcon::themeName()));
    std::printf("window bg:   %s\n", qPrintable(QApplication::palette().window().color().name()));
    std::printf("platform:    %s\n", qPrintable(QGuiApplication::platformName()));
    std::fflush(stdout);

    MainWindow w;
    w.resize(1200, 760);
    w.show();
    QTimer::singleShot(2000, [&] {
        w.grab().save(out + QLatin1Char('/') + prefix + QStringLiteral("-main.png"));
        auto *dlg = new QFileDialog(&w, QStringLiteral("Choose an installation image"), QDir::homePath());
        dlg->show();
    });
    QTimer::singleShot(5000, [&] {
        int n = 0;
        for (QWidget *t : QApplication::topLevelWidgets()) {
            if (t == &w || !t->isVisible())
                continue;
            std::printf("dialog:      %s\n", t->metaObject()->className());
            t->grab().save(out + QStringLiteral("/%1-dialog%2.png").arg(prefix).arg(n++));
        }
        app.quit();
    });
    return app.exec();
}
