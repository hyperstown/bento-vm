// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "UsbDevices.h"
#include "Paths.h"
#include "ui/Icons.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <unistd.h>

static QString readAttr(const QString &dir, const char *attr)
{
    QFile f(dir + QLatin1Char('/') + QLatin1String(attr));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).simplified();
}

QString UsbDevice::node() const
{
    return QStringLiteral("/dev/bus/usb/%1/%2").arg(bus, 3, 10, QLatin1Char('0')).arg(address, 3, 10, QLatin1Char('0'));
}

QString UsbDevice::displayName() const
{
    QString name = product;
    if (name.isEmpty())
        name = Usb::nameFromDatabase(vendorId, productId);
    if (name.isEmpty())
        name = QStringLiteral("USB device");
    if (!manufacturer.isEmpty() && !name.contains(manufacturer.section(QLatin1Char(' '), 0, 0), Qt::CaseInsensitive))
        name = manufacturer + QLatin1Char(' ') + name;
    return name;
}

UsbDevice::Kind UsbDevice::kind() const
{
    bool hid = false;
    for (const QString &i : interfaces) {
        const QStringList p = i.split(QLatin1Char(':'));
        const QString cls = p.value(0), sub = p.value(1), proto = p.value(2);
        if (cls == QLatin1String("03") && sub == QLatin1String("01") && proto == QLatin1String("01"))
            return Kind::Keyboard;
        if (cls == QLatin1String("03") && sub == QLatin1String("01") && proto == QLatin1String("02"))
            return Kind::Mouse;
        if (cls == QLatin1String("08"))
            return Kind::Storage;
        if (cls == QLatin1String("01"))
            return Kind::Audio;
        if (cls == QLatin1String("0e"))
            return Kind::Video;
        if (cls == QLatin1String("e0"))
            return Kind::Wireless;
        // Xbox-style pads use a vendor class with subclass 0x5d / 0x47.
        if (cls == QLatin1String("ff") && (sub == QLatin1String("5d") || sub == QLatin1String("47")))
            return Kind::GameController;
        if (cls == QLatin1String("03"))
            hid = true;
    }
    if (hid) {
        static const QRegularExpression pad(QStringLiteral("game|pad|joy|controller|wheel|stick|dualshock|dualsense"),
                                            QRegularExpression::CaseInsensitiveOption);
        const QString name = product + QLatin1Char(' ') + Usb::nameFromDatabase(vendorId, productId);
        if (pad.match(name).hasMatch())
            return Kind::GameController;
    }
    return Kind::Other;
}

QIcon UsbDevice::icon() const
{
    return Usb::iconFor(kind());
}

bool UsbDevice::accessible() const
{
    return ::access(QFile::encodeName(node()).constData(), R_OK | W_OK) == 0;
}

bool UsbDevice::isKeyboardOrMouse() const
{
    const Kind k = kind();
    return k == Kind::Keyboard || k == Kind::Mouse;
}

QStringList UsbDevice::mountPoints() const
{
    // Block devices whose sysfs path runs through this USB device.
    QStringList disks;
    for (const QFileInfo &b : QDir(QStringLiteral("/sys/block")).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System)) {
        if (b.canonicalFilePath().contains(QLatin1Char('/') + sysName + QLatin1Char('/')))
            disks << QStringLiteral("/dev/") + b.fileName();
    }
    if (disks.isEmpty())
        return {};
    QStringList mounts;
    QFile f(QStringLiteral("/proc/mounts"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    for (const QByteArray &line : f.readAll().split('\n')) {
        const QList<QByteArray> parts = line.split(' ');
        if (parts.size() < 2)
            continue;
        const QString dev = QString::fromLocal8Bit(parts[0]);
        for (const QString &d : std::as_const(disks)) {
            if (dev == d || (dev.startsWith(d) && dev.size() > d.size() && (dev[d.size()].isDigit() || dev[d.size()] == QLatin1Char('p'))))
                mounts << QString::fromLocal8Bit(parts[1]).replace(QStringLiteral("\\040"), QStringLiteral(" "));
        }
    }
    return mounts;
}

namespace Usb {

QList<UsbDevice> enumerate()
{
    QList<UsbDevice> out;
    const QString base = QStringLiteral("/sys/bus/usb/devices");
    const QStringList names = QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System);
    for (const QString &name : names) {
        // Interfaces look like "1-4:1.0", root hubs like "usb1".
        if (name.contains(QLatin1Char(':')) || name.startsWith(QLatin1String("usb")))
            continue;
        const QString dir = base + QLatin1Char('/') + name;
        UsbDevice d;
        d.sysName = name;
        d.bus = readAttr(dir, "busnum").toInt();
        d.address = readAttr(dir, "devnum").toInt();
        d.port = name.section(QLatin1Char('-'), 1);
        d.vendorId = readAttr(dir, "idVendor").toLower();
        d.productId = readAttr(dir, "idProduct").toLower();
        d.manufacturer = readAttr(dir, "manufacturer");
        d.product = readAttr(dir, "product");
        d.deviceClass = readAttr(dir, "bDeviceClass").toInt(nullptr, 16);
        if (d.bus == 0 || d.vendorId.isEmpty() || d.deviceClass == 0x09)
            continue;   // hubs can't usefully be passed through
        for (const QString &iface : QDir(base).entryList({name + QStringLiteral(":*")}, QDir::Dirs | QDir::System)) {
            const QString idir = base + QLatin1Char('/') + iface;
            d.interfaces << QStringLiteral("%1:%2:%3")
                                .arg(readAttr(idir, "bInterfaceClass").toLower(), readAttr(idir, "bInterfaceSubClass").toLower(),
                                     readAttr(idir, "bInterfaceProtocol").toLower());
        }
        out << d;
    }
    std::sort(out.begin(), out.end(), [](const UsbDevice &a, const UsbDevice &b) {
        return a.displayName().compare(b.displayName(), Qt::CaseInsensitive) < 0;
    });
    return out;
}

QString nameFromDatabase(const QString &vendorId, const QString &productId)
{
    static QHash<QString, QString> names;   // "vvvv" and "vvvv:pppp"
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        for (const char *path : {"/usr/share/hwdata/usb.ids", "/usr/share/misc/usb.ids", "/usr/share/usb.ids"}) {
            QFile f(QString::fromLatin1(path));
            if (!f.open(QIODevice::ReadOnly))
                continue;
            QString vendor;
            for (const QByteArray &raw : f.readAll().split('\n')) {
                if (raw.isEmpty() || raw.startsWith('#'))
                    continue;
                if (raw.startsWith("C "))
                    break;   // device classes follow the vendor list
                const QString line = QString::fromUtf8(raw);
                if (!line.startsWith(QLatin1Char('\t'))) {
                    vendor = line.left(4).toLower();
                    names.insert(vendor, line.mid(4).trimmed());
                } else if (!line.startsWith(QLatin1String("\t\t"))) {
                    names.insert(vendor + QLatin1Char(':') + line.mid(1, 4).toLower(), line.mid(5).trimmed());
                }
            }
            break;
        }
    }
    const QString product = names.value(vendorId + QLatin1Char(':') + productId);
    if (!product.isEmpty())
        return product;
    return names.value(vendorId);
}

QIcon iconFor(UsbDevice::Kind kind)
{
    const auto icon = [](const char *name, const char *fallback) {
        return Icons::fromTheme(QLatin1String(name), Icons::fromTheme(QLatin1String(fallback)));
    };
    switch (kind) {
    case UsbDevice::Kind::GameController: return icon("input-gaming", "applications-games");
    case UsbDevice::Kind::Storage: return icon("drive-removable-media-usb", "drive-removable-media");
    case UsbDevice::Kind::Keyboard: return icon("input-keyboard", "preferences-desktop-keyboard");
    case UsbDevice::Kind::Mouse: return icon("input-mouse", "preferences-desktop-mouse");
    case UsbDevice::Kind::Audio: return icon("audio-card", "audio-headphones");
    case UsbDevice::Kind::Video: return icon("camera-web", "camera-photo");
    case UsbDevice::Kind::Wireless: return icon("network-bluetooth", "network-wireless");
    default: return icon("drive-removable-media-usb", "drive-removable-media");
    }
}

QString aclHelperPath()
{
    for (const char *p : {"/usr/libexec/spice-client-glib-usb-acl-helper", "/usr/lib/spice-client-glib-usb-acl-helper",
                          "/usr/lib64/spice-client-glib-usb-acl-helper", "/usr/lib/spice-gtk/spice-client-glib-usb-acl-helper"}) {
        const QString path = QString::fromLatin1(p);
        if (QFileInfo(path).isExecutable())
            return path;
    }
    return {};
}

void requestTemporaryAccess(const UsbDevice &device, QObject *context,
                            std::function<void(bool, const QString &, QProcess *)> done)
{
    const QString helper = aclHelperPath();
    if (!helper.isEmpty()) {
        // Protocol: write "<bus> <dev>\n", read "SUCCESS" or an error line.
        // The ACL stays while the helper runs; closing its stdin removes it.
        auto *p = new QProcess(context);
        p->setProcessEnvironment(Paths::hostEnvironment());
        auto finished = std::make_shared<bool>(false);
        QObject::connect(p, &QProcess::readyReadStandardOutput, context, [p, done, finished] {
            if (*finished || !p->canReadLine())
                return;
            *finished = true;
            const QString line = QString::fromUtf8(p->readLine()).trimmed();
            if (line == QLatin1String("SUCCESS")) {
                done(true, QString(), p);
            } else {
                releaseTemporaryAccess(p);
                done(false, line.isEmpty() ? QStringLiteral("Access was denied.") : line, nullptr);
            }
        });
        QObject::connect(p, &QProcess::finished, context, [p, done, finished](int, QProcess::ExitStatus) {
            if (!*finished) {
                *finished = true;
                const QString err = QString::fromLocal8Bit(p->readAllStandardError() + p->readAllStandardOutput()).trimmed();
                done(false, err.isEmpty() ? QStringLiteral("Access was denied.") : err, nullptr);
            }
            p->deleteLater();
        });
        p->start(helper, QStringList());
        p->write(QStringLiteral("%1 %2\n").arg(device.bus).arg(device.address).toLatin1());
        return;
    }

    // Fallback: a plain ACL entry for us on this device node (asks for the
    // administrator password). It disappears with the node.
    static const QRegularExpression nodeRe(QStringLiteral(R"(^/dev/bus/usb/\d{3}/\d{3}$)"));
    const QString setfacl = QStandardPaths::findExecutable(QStringLiteral("setfacl"));
    if (setfacl.isEmpty() || !nodeRe.match(device.node()).hasMatch()) {
        done(false, QStringLiteral("setfacl is not available; use “Always Allow” instead."), nullptr);
        return;
    }
    auto *p = new QProcess(context);
    p->setProcessEnvironment(Paths::hostEnvironment());
    QObject::connect(p, &QProcess::finished, context, [p, done](int code, QProcess::ExitStatus st) {
        p->deleteLater();
        if (st == QProcess::NormalExit && code == 0)
            done(true, QString(), nullptr);
        else if (code == 126 || code == 127)
            done(false, QStringLiteral("Authorization was cancelled or denied."), nullptr);
        else
            done(false, QString::fromLocal8Bit(p->readAllStandardError()).trimmed(), nullptr);
    });
    p->start(QStringLiteral("pkexec"), {setfacl, QStringLiteral("-m"), QStringLiteral("u:%1:rw").arg(::getuid()), device.node()});
}

void releaseTemporaryAccess(QProcess *holder)
{
    if (!holder)
        return;
    holder->disconnect();
    if (holder->state() == QProcess::NotRunning) {
        holder->deleteLater();
        return;
    }
    QObject::connect(holder, &QProcess::finished, holder, &QObject::deleteLater);
    holder->closeWriteChannel();   // the helper removes the ACL and exits
    QTimer::singleShot(3000, holder, [holder] { holder->kill(); });
}

QString rulesFile()
{
    return QStringLiteral("/etc/udev/rules.d/70-bento-usb.rules");
}

void grantAccess(const QString &vendorId, const QString &productId, QObject *context,
                 std::function<void(bool, const QString &)> done)
{
    static const QRegularExpression hex4(QStringLiteral("^[0-9a-f]{4}$"));
    if (!hex4.match(vendorId).hasMatch() || !hex4.match(productId).hasMatch()) {
        done(false, QStringLiteral("Invalid USB id %1:%2").arg(vendorId, productId));
        return;
    }
    // "uaccess" makes systemd-logind give the active local user an ACL on the
    // device node. The rule must sort before 73-seat-late.rules.
    const QString rule = QStringLiteral(
        "SUBSYSTEM==\"usb\", ENV{DEVTYPE}==\"usb_device\", ATTR{idVendor}==\"%1\", ATTR{idProduct}==\"%2\", TAG+=\"uaccess\"")
                             .arg(vendorId, productId);
    const QString script = QStringLiteral(
        "set -e; f=\"$1\"; "
        "[ -f \"$f\" ] || printf '%s\\n' '# Managed by BentoVM: USB devices the logged-in user may pass through to VMs.' > \"$f\"; "
        "grep -qxF \"$2\" \"$f\" || printf '%s\\n' \"$2\" >> \"$f\"; "
        "udevadm control --reload-rules; "
        "udevadm trigger --action=change --subsystem-match=usb --attr-match=idVendor=\"$3\" --attr-match=idProduct=\"$4\"; "
        "udevadm settle -t 5 || true");

    auto *p = new QProcess(context);

    p->setProcessEnvironment(Paths::hostEnvironment());
    QObject::connect(p, &QProcess::finished, context, [p, vendorId, productId, done](int code, QProcess::ExitStatus st) {
        p->deleteLater();
        if (st != QProcess::NormalExit || code != 0) {
            QString err = QString::fromLocal8Bit(p->readAllStandardError()).trimmed();
            if (code == 126 || code == 127)
                err = QStringLiteral("Authorization was cancelled or denied.");
            done(false, err.isEmpty() ? QStringLiteral("pkexec exited with code %1").arg(code) : err);
            return;
        }
        done(true, QString());
    });
    QObject::connect(p, &QProcess::errorOccurred, context, [p, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            done(false, QStringLiteral("pkexec is not available. Add this rule to %1 manually.").arg(rulesFile()));
        }
    });
    p->start(QStringLiteral("pkexec"), {QStringLiteral("/bin/sh"), QStringLiteral("-c"), script, QStringLiteral("bento-usb"),
                                        rulesFile(), rule, vendorId, productId});
}

} // namespace Usb
