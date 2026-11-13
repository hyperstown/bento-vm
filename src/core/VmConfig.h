#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

struct DiskSpec
{
    QString path;                         // relative to the VM folder, or absolute
    QString format = QStringLiteral("qcow2");
    QString bus = QStringLiteral("virtio"); // virtio | sata | nvme
};

// A host USB device connected automatically whenever the VM starts.
struct UsbAutoConnect
{
    QString vendorId;   // 4 lowercase hex digits
    QString productId;
    QString name;
};

struct PortForward
{
    QString proto = QStringLiteral("tcp");
    QString hostAddress = QStringLiteral("127.0.0.1");
    int hostPort = 0;
    int guestPort = 0;
};

// Everything needed to boot a VM. Lives in <vm folder>/vm.json.
struct VmConfig
{
    static constexpr int kVersion = 1;
    static QString fileName() { return QStringLiteral("vm.json"); }

    QString id;                 // UUID without braces
    QString name;
    QString osId = QStringLiteral("linux");
    QString osLabel;            // e.g. "Arch Linux 2024.07"
    QString notes;

    int cores = 2;
    int threads = 1;
    int memoryMiB = 4096;

    QString firmware = QStringLiteral("uefi");   // uefi | bios
    bool secureBoot = false;
    bool tpm = false;

    QString graphics = QStringLiteral("venus");  // venus | virgl | 2d
    int hostmemMiB = 4096;                        // Venus host-visible memory window
    QString display = QStringLiteral("embedded"); // embedded | gtk | sdl
    bool autoResize = true;

    QList<DiskSpec> disks;
    QStringList cdroms;          // ISO paths ("" = empty drive)

    bool network = true;
    QString nicModel = QStringLiteral("virtio-net-pci");
    QList<PortForward> portForwards;

    QList<UsbAutoConnect> usbDevices;

    bool audio = true;
    QString clipboard = QStringLiteral("both");  // both | host-to-guest | guest-to-host | off
    bool fileTransfer = true;

    bool localtime = false;
    bool hyperv = false;
    QString extraArgs;

    QDateTime created;

    // Not serialized: folder that contains vm.json.
    QString dir;

    int vcpus() const { return cores * threads; }
    bool usesGl() const { return graphics != QLatin1String("2d"); }

    QString absolutePath(const QString &p) const;
    QString diskPath(int index) const;
    QString configFile() const { return dir + QLatin1Char('/') + fileName(); }

    QJsonObject toJson() const;
    static VmConfig fromJson(const QJsonObject &o);

    bool save(QString *error = nullptr) const;
    static bool load(const QString &dir, VmConfig *out, QString *error = nullptr);

    // Paths (disks, ISOs) that must exist for the VM to boot; first missing one is returned.
    QString firstMissingRequiredPath() const;
};

QString clipboardModeLabel(const QString &mode);
QString graphicsModeLabel(const QString &mode);
