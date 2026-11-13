#pragma once

#include <QProcessEnvironment>
#include <QString>

// Well-known locations used by Bento. Everything follows the XDG base dir spec.
namespace Paths {

QString configDir();   // ~/.config/bento-vm
QString dataDir();     // ~/.local/share/bento-vm
QString cacheDir();    // ~/.cache/bento-vm
QString stateDir();    // ~/.local/state/bento-vm
QString runtimeDir();  // $XDG_RUNTIME_DIR/bento-vm  (sockets, pid files)

QString vmRuntimeDir(const QString &vmId);
QString logFile(const QString &vmId);
QString thumbnailFile(const QString &vmId);
QString libraryCacheFile();
QString defaultStorageLocation();

// mkpath + optional permission tightening; returns false if the directory can't be created.
bool ensureDir(const QString &path, bool privateDir = false);

// "/run/media/user/Disk/VMs/foo" -> "/run/media/user/Disk" style mount root, best effort.
QString mountPointFor(const QString &path);

QString humanSize(qint64 bytes);

// Environment for host programs (QEMU, qemu-img, pkexec, …). When running
// from an AppImage, entries pointing into the bundle are dropped so host
// tools never pick up our bundled libraries or Qt plugins.
QProcessEnvironment hostEnvironment();

} // namespace Paths
