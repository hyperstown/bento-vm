// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>
#include <QStringList>

struct VmConfig;

// A host folder shared with the guest through virtio-fs. QEMU talks to a
// virtiofsd process that runs as the user and serves only that folder. The
// guest mounts it by its tag (Linux: mount -t virtiofs; Windows: the
// VirtIO-FS service from the virtio-win guest tools plus WinFsp).
namespace SharedFolder {

// The virtio-fs tag the guest mounts: "mount -t virtiofs shared /mnt/shared".
QString tag();

// What the installed virtiofsd can do, read once from its --help.
struct Support
{
    bool installed = false;
    bool readOnly = false;      // --readonly
    bool translateIds = false;  // --translate-uid / --translate-gid
};
const Support &support();

// Why the VM's shared folder can't be used, worded as a warning that the VM
// starts without it. Empty if it can be used or the VM has none.
QString problem(const VmConfig &cfg);

// True if sharing `folder` would hand the guest the whole home folder or more.
bool exposesHome(const QString &folder);

// virtiofsd's arguments for the VM (without the program).
QStringList arguments(const VmConfig &cfg, const QString &runtimeDir, bool sandbox);

// Starts virtiofsd before QEMU and waits until it serves its socket. Like QEMU
// it runs detached, so it survives the GUI; it exits by itself once QEMU
// disconnects. Things worth a line in the VM log (e.g. running without the
// sandbox) are appended to *notes.
bool start(const VmConfig &cfg, const QString &runtimeDir, const QString &logFile, QStringList *notes,
           QString *error);

// Stops the VM's virtiofsd if it's still running, e.g. when QEMU never connected.
void stop(const QString &runtimeDir);

} // namespace SharedFolder
