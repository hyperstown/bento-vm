#pragma once

#include <QIcon>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class QObject;
class QProcess;

// A USB device plugged into the host, read from sysfs.
struct UsbDevice
{
    QString sysName;        // "1-4", "3-4.2"
    int bus = 0;
    int address = 0;
    QString port;           // physical port path on the bus: "4", "4.2"
    QString vendorId;       // 4 lowercase hex digits
    QString productId;
    QString manufacturer;
    QString product;
    int deviceClass = 0;
    QStringList interfaces; // "cc:ss:pp" per interface (class:subclass:protocol, hex)

    enum class Kind { GameController, Storage, Keyboard, Mouse, Audio, Video, Wireless, Other };

    QString node() const;          // /dev/bus/usb/BBB/DDD
    QString usbId() const { return vendorId + QLatin1Char(':') + productId; }
    QString displayName() const;
    Kind kind() const;
    QIcon icon() const;
    bool accessible() const;       // QEMU (running as us) can open it
    bool isKeyboardOrMouse() const;
    QStringList mountPoints() const;   // host mounts of partitions on this device
};

namespace Usb {

QList<UsbDevice> enumerate();   // hubs excluded
QString nameFromDatabase(const QString &vendorId, const QString &productId);
QIcon iconFor(UsbDevice::Kind kind);

// Installs a udev rule (via pkexec, the user is asked for an admin password)
// that gives the logged-in user access to devices with this vendor:product,
// then re-triggers udev. `done(ok, error)` is called asynchronously.
void grantAccess(const QString &vendorId, const QString &productId, QObject *context,
                 std::function<void(bool ok, const QString &error)> done);

QString rulesFile();

// One-time access to a device, without a persistent rule.
//  * With spice-gtk's ACL helper (polkit "org.spice-space.lowlevelusbaccess",
//    no password for the active session) `holder` is a running helper process:
//    access lasts until it is released (see releaseTemporaryAccess).
//  * Otherwise falls back to `pkexec setfacl` (administrator password); access
//    then lasts until the device is unplugged or the host reboots, and
//    `holder` is null.
QString aclHelperPath();
void requestTemporaryAccess(const UsbDevice &device, QObject *context,
                            std::function<void(bool ok, const QString &error, QProcess *holder)> done);
void releaseTemporaryAccess(QProcess *holder);

} // namespace Usb
