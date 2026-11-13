// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>
#include <QVersionNumber>

// Facts about the host machine, gathered once at startup.
struct HostInfo
{
    int logicalCpus = 1;
    int physicalCores = 1;
    int threadsPerCore = 1;
    bool amd = false;
    bool intel = false;
    QString cpuModel;
    qint64 totalRamMiB = 0;

    QString qemuImg;
    QString swtpm;
    QString virtiofsd;

    QString ovmfCode;        // UEFI firmware (no secure boot)
    QString ovmfCodeSecure;  // UEFI firmware with secure boot support
    QString ovmfVars;        // template for per-VM NVRAM

    bool kvmUsable = false;
    QString kvmProblem;

    static const HostInfo &get();

    // QEMU binary honoring the user override in settings.
    static QString qemuBinary();
    static QVersionNumber qemuVersion();   // of qemuBinary(), cached; null if unknown
    static qint64 availableRamMiB();  // MemAvailable right now
};
