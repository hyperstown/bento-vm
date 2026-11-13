// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QColor>
#include <QList>
#include <QString>

struct HostInfo;

enum class OsFamily { Linux, Windows, Bsd, Android, Other };

// Per-OS defaults used by the wizard. Resources are "what the OS wants";
// the actual recommendation is scaled to the host in OsDb::recommend().
struct OsProfile
{
    QString id;
    QString name;
    OsFamily family = OsFamily::Other;
    QColor color;
    QString badge;            // 1-2 characters shown in the generated logo

    int minRamMiB = 1024;
    int recRamMiB = 4096;
    int minCpus = 1;
    int recCpus = 2;
    int recDiskGiB = 64;

    QString graphics = QStringLiteral("venus");    // venus | virgl | 2d
    QString firmware = QStringLiteral("uefi");     // uefi | bios
    bool secureBoot = false;
    bool tpm = false;
    QString diskBus = QStringLiteral("virtio");    // virtio | sata | nvme
    QString nicModel = QStringLiteral("virtio-net-pci");
    bool localtime = false;
    bool hyperv = false;

    QString hint;             // tip shown in the wizard
};

struct Recommendation
{
    int cores = 2;
    int threads = 1;
    int memoryMiB = 4096;
    int diskGiB = 64;
};

namespace OsDb {

const QList<OsProfile> &profiles();
const OsProfile &profile(const QString &id);   // unknown ids map to "other"

// Matches distro names, ISO volume labels and file names. Returns "" if nothing matched.
QString guessFromText(const QString &text);

struct IsoInfo
{
    bool valid = false;
    QString systemId;
    QString volumeId;
    QString publisher;
    QString application;
};
IsoInfo readIsoInfo(const QString &path);

struct Detection
{
    QString profileId;     // always set (falls back to "linux"/"other")
    QString label;         // human readable, e.g. "Arch Linux 2024.07"
    QString volumeId;
    bool recognized = false;
    bool bootable = true;
    QString osinfoName;    // raw osinfo-detect answer, if any
};

// Blocking: reads the ISO header and runs osinfo-detect. Call from a worker thread.
Detection detect(const QString &isoPath);

Recommendation recommend(const OsProfile &p, const HostInfo &host);

} // namespace OsDb
