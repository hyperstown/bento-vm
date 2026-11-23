// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>
#include <QStringList>

struct VmConfig;

struct QemuCommand
{
    QString program;
    QStringList args;
    QStringList warnings;   // non-fatal: feature dropped, etc.
    QString error;          // fatal: can't boot
    // Set when the command includes the VM's shared folder: virtiofsd must be
    // serving this host folder on QemuRuntime::virtiofsSocket() before QEMU starts.
    QString sharedFolder;

    QString toShellString() const;
};

namespace QemuRuntime {
inline QString qmpSocket(const QString &rt) { return rt + QStringLiteral("/qmp.sock"); }
inline QString agentSocket(const QString &rt) { return rt + QStringLiteral("/vdagent.sock"); }
inline QString guestAgentSocket(const QString &rt) { return rt + QStringLiteral("/qga.sock"); }
inline QString swtpmSocket(const QString &rt) { return rt + QStringLiteral("/swtpm.sock"); }
inline QString virtiofsSocket(const QString &rt) { return rt + QStringLiteral("/virtiofs.sock"); }
inline QString virtiofsdPidFile(const QString &rt) { return rt + QStringLiteral("/virtiofsd.pid"); }
inline QString pidFile(const QString &rt) { return rt + QStringLiteral("/qemu.pid"); }
inline QString efiVarsFile(const QString &vmDir) { return vmDir + QStringLiteral("/efivars.fd"); }
inline QString tpmStateDir(const QString &vmDir) { return vmDir + QStringLiteral("/tpm"); }
inline const char *cdromDeviceId(int i) { return i == 0 ? "cdrom0" : "cdrom1"; }
} // namespace QemuRuntime

// Builds the full qemu-system-x86_64 command line for a VM.
QemuCommand buildQemuCommand(const VmConfig &cfg, const QString &runtimeDir);
