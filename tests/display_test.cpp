// Smoke test for the embedded display pipeline:
// boots a disk-less QEMU (firmware screen only), connects through QMP +
// D-Bus p2p, renders into DisplayWidget and saves the guest frame.
//
//   display_test <out.png> [gl=on|off] [seconds] [bios|uefi]
#include "core/QmpClient.h"
#include "display/DBusDisplay.h"
#include "display/DisplayWidget.h"
#include "display/GlibIntegration.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("frame.png");
    const bool gl = argc > 2 ? QByteArray(argv[2]) != "gl=off" : true;
    const int seconds = argc > 3 ? atoi(argv[3]) : 6;
    const bool uefi = argc > 4 && QByteArray(argv[4]) == "uefi";

    GlibIntegration::ensure();
    std::printf("Qt dispatcher uses GLib: %s\n", GlibIntegration::qtUsesGlib() ? "yes" : "no");

    QTemporaryDir tmp;
    const QString qmpPath = tmp.path() + QStringLiteral("/qmp.sock");
    QStringList args{QStringLiteral("-nodefaults"), QStringLiteral("-machine"), QStringLiteral("q35,accel=kvm"),
                     QStringLiteral("-m"), QStringLiteral("512"),
                     QStringLiteral("-device"), gl ? QStringLiteral("virtio-vga-gl") : QStringLiteral("virtio-vga"),
                     QStringLiteral("-display"), QStringLiteral("dbus,p2p=yes,gl=%1").arg(gl ? "on" : "off"),
                     QStringLiteral("-device"), QStringLiteral("qemu-xhci"), QStringLiteral("-device"), QStringLiteral("usb-tablet"),
                     QStringLiteral("-qmp"), QStringLiteral("unix:%1,server=on,wait=off").arg(qmpPath)};
    if (uefi)
        args << QStringLiteral("-drive") << QStringLiteral("if=pflash,format=raw,readonly=on,file=/usr/share/edk2/x64/OVMF.4m.fd");
    QProcess qemu;
    qemu.setProcessChannelMode(QProcess::ForwardedChannels);
    qemu.start(QStringLiteral("qemu-system-x86_64"), args);
    if (!qemu.waitForStarted()) {
        std::fprintf(stderr, "qemu failed to start\n");
        return 2;
    }

    QmpClient qmp;
    DBusDisplay display(&qmp);
    DisplayWidget widget;
    widget.resize(1024, 768);
    widget.attach(&display);
    widget.setAutoResize(false);
    widget.show();

    int frames = 0;
    QObject::connect(&display, &DBusDisplay::updateDmabuf, [&] { ++frames; });
    QObject::connect(&display, &DBusDisplay::scanoutDmabuf, [&](const DmabufScanout &s) {
        std::printf("ScanoutDMABUF %ux%u fourcc=%.4s modifier=0x%llx planes=%lld y0top=%d\n", s.backingWidth,
                    s.backingHeight, reinterpret_cast<const char *>(&s.fourcc), (unsigned long long)s.modifier,
                    (long long)s.planes.size(), s.y0Top);
    });
    QObject::connect(&display, &DBusDisplay::scanoutImage, [&](const QImage &img) {
        std::printf("Scanout (2D) %dx%d\n", img.width(), img.height());
    });
    QObject::connect(&display, &DBusDisplay::updateImage, [&] { ++frames; });
    QObject::connect(&display, &DBusDisplay::connected, [&] { std::printf("display connected\n"); });
    QObject::connect(&display, &DBusDisplay::failed, [&](const QString &e) {
        std::fprintf(stderr, "display failed: %s\n", qPrintable(e));
        app.exit(3);
    });
    QObject::connect(&widget, &DisplayWidget::renderError, [&](const QString &e) {
        std::fprintf(stderr, "render error: %s\n", qPrintable(e));
    });
    QObject::connect(&qmp, &QmpClient::ready, [&] {
        std::printf("QMP ready\n");
        display.start();
    });

    QTimer poll;
    poll.setInterval(50);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        if (QFileInfo::exists(qmpPath) && qmp.connectTo(qmpPath))
            poll.stop();
    });
    poll.start();

    QTimer::singleShot(seconds * 1000, [&] {
        const QImage img = widget.grabGuestFrame();
        std::printf("frames updated: %d, guest size %dx%d, grabbed %dx%d\n", frames, widget.guestSize().width(),
                    widget.guestSize().height(), img.width(), img.height());
        if (!img.isNull())
            img.save(out);
        // Count non-black pixels as a crude "something rendered" check.
        int lit = 0;
        for (int y = 0; y < img.height(); y += 4)
            for (int x = 0; x < img.width(); x += 4)
                if (qGray(img.pixel(x, y)) > 30)
                    ++lit;
        std::printf("lit sample pixels: %d\n", lit);
        qmp.execute(QStringLiteral("quit"));
        QTimer::singleShot(500, &app, [&] { app.exit(lit > 0 ? 0 : 1); });
    });
    const int rc = app.exec();
    qemu.waitForFinished(3000);
    return rc;
}
