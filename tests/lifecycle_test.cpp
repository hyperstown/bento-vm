// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

// VMs run as detached QEMU processes. This checks that a VM keeps running
// after the GUI exits and that a new GUI process re-attaches to it:
//
//   lifecycle_test start  <vm-name>
//   lifecycle_test attach <vm-name> <frame.png>
#include "core/Vm.h"
#include "core/VmLibrary.h"
#include "display/DBusDisplay.h"
#include "display/DisplayWidget.h"
#include "display/GlibIntegration.h"

#include <QApplication>
#include <QTimer>

#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("bento-vm"));
    if (argc < 3)
        return 2;
    const QByteArray mode = argv[1];
    const QString name = QString::fromLocal8Bit(argv[2]);
    GlibIntegration::ensure();
    VmLibrary::instance()->refresh();
    Vm *vm = VmLibrary::instance()->findByName(name);
    if (!vm) {
        std::fprintf(stderr, "no VM called %s\n", qPrintable(name));
        return 2;
    }

    if (mode == "start") {
        QString err;
        if (!vm->start(&err)) {
            std::fprintf(stderr, "start failed: %s\n", qPrintable(err));
            return 1;
        }
        QObject::connect(vm, &Vm::stateChanged, [&](Vm::State s) {
            std::printf("state: %s\n", qPrintable(Vm::stateLabel(s)));
            if (s == Vm::State::Running)
                QTimer::singleShot(500, &app, [&] { app.exit(0); });   // leave it running
        });
        QTimer::singleShot(30000, &app, [&] { app.exit(1); });
        return app.exec();
    }

    // attach
    const QString out = QString::fromLocal8Bit(argv[3]);
    std::printf("after refresh: %s\n", qPrintable(Vm::stateLabel(vm->state())));
    DisplayWidget w;
    w.resize(1024, 700);
    w.show();
    DBusDisplay *display = nullptr;
    auto connectDisplay = [&] {
        display = new DBusDisplay(vm->qmp(), &app);
        w.attach(display);
        QObject::connect(display, &DBusDisplay::connected, [] { std::printf("display re-connected\n"); });
        QObject::connect(display, &DBusDisplay::failed, [](const QString &e) { std::printf("display failed: %s\n", qPrintable(e)); });
        display->start();
    };
    QObject::connect(vm, &Vm::qmpReady, connectDisplay);
    QObject::connect(vm, &Vm::stateChanged, [](Vm::State s) { std::printf("state: %s\n", qPrintable(Vm::stateLabel(s))); });

    QTimer::singleShot(6000, &app, [&] {
        const QImage img = w.grabGuestFrame();
        std::printf("frame %dx%d\n", img.width(), img.height());
        img.save(out);
        vm->forceStop();
    });
    QTimer::singleShot(10000, &app, [&] {
        std::printf("final: %s\n", qPrintable(Vm::stateLabel(vm->state())));
        app.exit(vm->state() == Vm::State::Stopped ? 0 : 1);
    });
    return app.exec();
}
