// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

// Screenshots of the USB passthrough UI (menu + settings page). Read-only:
// it never toggles a device, so no permission prompt appears.
//   usb_ui_test <out-dir> <vm-name> <theme>
#include "core/UsbDevices.h"
#include "core/Vm.h"
#include "core/VmLibrary.h"
#include "display/GlibIntegration.h"
#include "ui/Theme.h"
#include "ui/VmSettingsDialog.h"
#include "ui/VmWindow.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("bento-vm"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/bento-vm.svg")));
    if (argc < 4)
        return 2;
    const QString out = QString::fromLocal8Bit(argv[1]);
    GlibIntegration::ensure();
    Theme::instance()->init();
    Theme::instance()->apply(QString::fromLocal8Bit(argv[3]));
    VmLibrary::instance()->refresh();
    Vm *vm = VmLibrary::instance()->findByName(QString::fromLocal8Bit(argv[2]));
    if (!vm)
        return 2;

    // One auto-connect entry so the settings page has content.
    const QList<UsbDevice> host = Usb::enumerate();
    VmConfig c = vm->config();
    c.usbDevices.clear();
    for (const UsbDevice &d : host)
        if (d.kind() == UsbDevice::Kind::GameController)
            c.usbDevices << UsbAutoConnect{d.vendorId, d.productId, d.displayName()};
    c.usbDevices << UsbAutoConnect{QStringLiteral("054c"), QStringLiteral("09cc"), QStringLiteral("Wireless Controller")};
    vm->updateConfig(c);

    auto *w = new VmWindow(vm);
    w->resize(1280, 760);
    w->show();
    QString err;
    if (!vm->start(&err)) {
        std::printf("start failed: %s\n", qPrintable(err));
        return 1;
    }

    QTimer::singleShot(9000, [&] {
        QToolButton *usbButton = nullptr;
        for (QToolButton *b : w->findChildren<QToolButton *>())
            if (b->defaultAction() && b->defaultAction()->text().startsWith(QLatin1String("USB")))
                usbButton = b;
        if (!usbButton)
            return;
        QMenu *menu = usbButton->defaultAction()->menu();
        menu->popup(usbButton->mapToGlobal(QPoint(0, usbButton->height())));
        QTimer::singleShot(800, [=] {
            w->grab().save(out + QStringLiteral("/usb-window.png"));
            menu->grab().save(out + QStringLiteral("/usb-menu.png"));
            std::printf("menu items:\n");
            for (QAction *a : menu->actions())
                if (!a->text().isEmpty())
                    std::printf("  [%s] %s%s\n", a->isCheckable() ? (a->isChecked() ? "x" : " ") : "-", qPrintable(a->text()),
                                a->isEnabled() ? "" : " (disabled)");
            menu->close();
            auto *dlg = new VmSettingsDialog(vm, w);
            dlg->showPage(QStringLiteral("usb"));
            dlg->show();
            QTimer::singleShot(800, [=] {
                dlg->grab().save(out + QStringLiteral("/usb-settings.png"));
                dlg->close();
                vm->forceStop();
            });
        });
    });
    QObject::connect(vm, &Vm::stateChanged, [&](Vm::State s) {
        if (s == Vm::State::Stopped) {
            VmConfig back = vm->config();
            back.usbDevices.clear();
            vm->updateConfig(back);
            app.quit();
        }
    });
    QTimer::singleShot(40000, &app, [&] {
        vm->forceStop();
        app.exit(1);
    });
    return app.exec();
}
