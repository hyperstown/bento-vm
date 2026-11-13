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
#include <QFileInfo>
#include <QProcess>
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

    if (mode == "passthrough") {
        // Real passthrough with one-time access: grant, attach, verify QEMU
        // opened it, hand it back, verify access and host driver are restored.
        GlibIntegration::ensure();
        VmLibrary::instance()->refresh();
        Vm *vm = VmLibrary::instance()->findByName(QString::fromLocal8Bit(argv[2]));
        const QString usbId = QString::fromLocal8Bit(argv[3]);
        UsbDevice dev;
        for (const UsbDevice &d : devs)
            if (d.usbId() == usbId)
                dev = d;
        if (!vm || dev.bus == 0)
            return 2;
        const QString driverLink = QStringLiteral("/sys/bus/usb/devices/%1:1.0/driver").arg(dev.sysName);
        auto hostDriver = [&] { return QFileInfo(driverLink).symLinkTarget().section(QLatin1Char('/'), -1); };
        std::printf("host driver before: %s, access before: %s\n", qPrintable(hostDriver()), dev.accessible() ? "rw" : "no");
        QString err;
        if (!vm->start(&err))
            return 1;
        int failures = 0;
        auto check = [&](const char *what, bool ok) {
            std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
            failures += ok ? 0 : 1;
        };
        QObject::connect(vm, &Vm::qmpReady, [&] {
            Usb::requestTemporaryAccess(dev, &app, [&](bool ok, const QString &e, QProcess *holder) {
                check("one-time access granted without a prompt", ok && holder);
                check("device node now accessible", dev.accessible());
                if (!ok)
                    return vm->forceStop();
                vm->attachUsb(dev, [&](const QString &e2) {
                    check("device_add", e2.isEmpty());
                    QTimer::singleShot(1500, [&] {
                        std::printf("host driver while attached: '%s'\n", qPrintable(hostDriver()));
                        vm->qmp()->execute(QStringLiteral("human-monitor-command"),
                                           {{QStringLiteral("command-line"), QStringLiteral("info usb")}},
                                           [&](const QJsonValue &r, const QString &) {
                            const QString info = r.toString();
                            std::printf("guest 'info usb':\n%s", qPrintable(info));
                            check("QEMU opened the real device", info.contains(QLatin1String("ID: usb-h"))
                                                                    && !info.contains(QLatin1String("Product USB Host Device")));
                            const UsbAttachment *a = vm->usbAttachmentFor(dev);
                            vm->detachUsb(a ? a->qdevId : QString(), [&](const QString &) {
                                QTimer::singleShot(1500, [&] {
                                    check("access revoked after handing it back", !dev.accessible());
                                    check("host driver re-bound", !hostDriver().isEmpty());
                                    std::printf("host driver after: %s\n", qPrintable(hostDriver()));
                                    vm->forceStop();
                                });
                            });
                        });
                    });
                }, holder);
            });
        });
        QObject::connect(vm, &Vm::stateChanged, [&](Vm::State s) {
            if (s == Vm::State::Stopped)
                app.exit(failures ? 1 : 0);
        });
        QTimer::singleShot(40000, &app, [&] { vm->forceStop(); app.exit(1); });
        return app.exec();
    }

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
