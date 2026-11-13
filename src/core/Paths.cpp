#include "Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QStorageInfo>

namespace Paths {

static QString appSub(QStandardPaths::StandardLocation loc)
{
    return QStandardPaths::writableLocation(loc) + QStringLiteral("/bento-vm");
}

QString configDir() { return appSub(QStandardPaths::GenericConfigLocation); }
QString dataDir() { return appSub(QStandardPaths::GenericDataLocation); }
QString cacheDir() { return appSub(QStandardPaths::GenericCacheLocation); }
QString stateDir() { return appSub(QStandardPaths::GenericStateLocation); }

QString runtimeDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (base.isEmpty())
        base = QDir::tempPath();
    return base + QStringLiteral("/bento-vm");
}

QString vmRuntimeDir(const QString &vmId) { return runtimeDir() + QLatin1Char('/') + vmId; }
QString logFile(const QString &vmId) { return stateDir() + QStringLiteral("/logs/") + vmId + QStringLiteral(".log"); }
QString thumbnailFile(const QString &vmId) { return cacheDir() + QStringLiteral("/thumbnails/") + vmId + QStringLiteral(".png"); }
QString libraryCacheFile() { return dataDir() + QStringLiteral("/library.json"); }
QString defaultStorageLocation() { return dataDir() + QStringLiteral("/machines"); }

bool ensureDir(const QString &path, bool privateDir)
{
    if (!QDir().mkpath(path))
        return false;
    if (privateDir)
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return true;
}

QString mountPointFor(const QString &path)
{
    // Walk up until we find an existing directory, then ask QStorageInfo about it.
    QFileInfo fi(path);
    QString p = fi.absoluteFilePath();
    while (!p.isEmpty() && !QFileInfo::exists(p)) {
        const int slash = p.lastIndexOf(QLatin1Char('/'));
        if (slash <= 0)
            return QStringLiteral("/");
        p.truncate(slash);
    }
    return QStorageInfo(p).rootPath();
}

QString humanSize(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

} // namespace Paths
