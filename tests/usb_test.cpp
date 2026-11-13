// USB passthrough plumbing test:
//   usb_test list
//   usb_test run <vm-name> <usb-id vvvv:pppp>
// "run" boots the VM with an auto-connect entry for the device, hot-plugs it
// by port, checks QEMU's view (qom + "info usb"), then unplugs it.
#include "core/QmpClient.h"
#include "core/UsbDevices.h"
#include "core/Vm.h"
#include "core/VmLibrary.h"
#include "display/GlibIntegration.h"

#include <QApplication>
#include <QJsonObject>
#include <QTimer>

#include <cstdio>

static void printAttachments(Vm *vm, const char *when)
{
    std::printf("%s: %lld attached\n", when, (long long)vm->usbAttachments().size());
    for (const UsbAttachment &a : vm->usbAttachments())
        std::printf("  %-14s bus=%d port=%-5s id=%s:%s auto=%d\n", qPrintable(a.qdevId), a.hostBus, qPrintable(a.hostPort),
                    qPrintable(a.vendorId), qPrintable(a.productId), a.autoConnect());
    std::fflush(stdout);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("bento-vm"));
    const QByteArray mode = argc > 1 ? argv[1] : "list";
    const QList<UsbDevice> devs = Usb::enumerate();
    for (const UsbDevice &d : devs)
        std::printf("%-7s %s  %-34s kind=%d access=%s mounts=%s\n", qPrintable(d.sysName), qPrintable(d.usbId()),
                    qPrintable(d.displayName()), int(d.kind()), d.accessible() ? "rw" : "no",
                    qPrintable(d.mountPoints().join(QLatin1Char(','))));
    if (mode == "list")
        return 0;

    GlibIntegration::ensure();
    VmLibrary::instance()->refresh();
    Vm *vm = VmLibrary::instance()->findByName(QString::fromLocal8Bit(argv[2]));
    const QString usbId = QString::fromLocal8Bit(argv[3]);
    const UsbDevice *dev = nullptr;
    for (const UsbDevice &d : devs)
        if (d.usbId() == usbId)
            dev = &d;
    if (!vm || !dev)
        return 2;

    VmConfig c = vm->config();
    c.usbDevices = {UsbAutoConnect{dev->vendorId, dev->productId, dev->displayName()}};
    c.display = QStringLiteral("embedded");
    vm->updateConfig(c);
    QString err;
    if (!vm->start(&err)) {
        std::printf("start failed: %s\n", qPrintable(err));
        return 1;
    }
    for (const QString &w : vm->lastWarnings())
        std::printf("start warning: %s\n", qPrintable(w));

    int step = 0;
    QObject::connect(vm, &Vm::usbChanged, [&] {
        if (step == 0) {
            step = 1;
            printAttachments(vm, "after boot (auto-connect)");
            // Replace the auto entry with a hot-plugged one bound to the port.
            vm->detachUsb(QStringLiteral("usbauto0"), [&](const QString &e) {
                if (!e.isEmpty())
                    std::printf("detach auto failed: %s\n", qPrintable(e));
            });
        } else if (step == 1 && vm->usbAttachments().isEmpty()) {
            step = 2;
            printAttachments(vm, "after removing auto entry");
            vm->attachUsb(*dev, [&](const QString &e) { std::printf("hot-plug: %s\n", e.isEmpty() ? "ok" : qPrintable(e)); });
        } else if (step == 2 && !vm->usbAttachments().isEmpty()) {
            step = 3;
            printAttachments(vm, "after hot-plug");
            const UsbAttachment *a = vm->usbAttachmentFor(*dev);
            std::printf("matches host device: %s\n", a ? "yes" : "no");
            const QString id = a ? a->qdevId : QString();
            vm->qmp()->execute(QStringLiteral("human-monitor-command"), {{QStringLiteral("command-line"), QStringLiteral("info usb")}},
                               [vm, id](const QJsonValue &r, const QString &) {
                                   std::printf("guest 'info usb':\n%s", qPrintable(r.toString()));
                                   vm->detachUsb(id);
                               });
        } else if (step == 3 && vm->usbAttachments().isEmpty()) {
            step = 4;
            printAttachments(vm, "after unplug");
            vm->forceStop();
        }
    });
    QObject::connect(vm, &Vm::stateChanged, [&](Vm::State s) {
        if (s == Vm::State::Stopped) {
            VmConfig back = vm->config();
            back.usbDevices.clear();
            vm->updateConfig(back);
            app.exit(step == 4 ? 0 : 1);
        }
    });
    QTimer::singleShot(40000, &app, [&] {
        std::printf("timeout at step %d\n", step);
        vm->forceStop();
        app.exit(1);
    });
    return app.exec();
}
