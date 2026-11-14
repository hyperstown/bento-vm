// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DiskOps.h"
#include "Errors.h"
#include "HostInfo.h"
#include "Paths.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <sys/stat.h>

namespace DiskOps {

static QString qemuImg()
{
    const QString p = HostInfo::get().qemuImg;
    return p.isEmpty() ? QStringLiteral("qemu-img") : p;
}

static qint64 allocatedBytes(const QString &path)
{
    struct stat st{};
    if (::stat(QFile::encodeName(path).constData(), &st) != 0)
        return 0;
    return qint64(st.st_blocks) * 512;
}

static bool failWith(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

// Runs the process to completion. While it runs, reports how much of `total`
// bytes `watchFile` has allocated, and stops it when the operation is canceled.
static bool runWithProgress(QProcess &p, const QString &program, const QString &watchFile, qint64 total,
                            const Progress &progress, QString *error)
{
    p.setProcessEnvironment(Paths::hostEnvironment());
    p.start();
    if (!p.waitForStarted(-1))
        return failWith(error, Errors::fromProcess(p, program));
    int reported = -1;
    while (p.state() != QProcess::NotRunning && !p.waitForFinished(200)) {
        if (progress.canceled && progress.canceled()) {
            p.kill();
            p.waitForFinished(-1);
            return failWith(error, QStringLiteral("Canceled."));
        }
        if (progress.report && total > 0) {
            const int percent = int(qBound(qint64(0), allocatedBytes(watchFile) * 100 / total, qint64(99)));
            if (percent != reported)
                progress.report(reported = percent);
        }
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return failWith(error, Errors::fromProcess(p, program));
    if (progress.report)
        progress.report(100);
    return true;
}

ImageInfo imageInfo(const QString &path)
{
    ImageInfo info;
    QProcess p;
    p.setProcessEnvironment(Paths::hostEnvironment());
    // -U: don't take the image lock, so this also works while a VM uses it.
    p.start(qemuImg(), {QStringLiteral("info"), QStringLiteral("-U"), QStringLiteral("--output=json"), path});
    if (!p.waitForFinished(30000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        info.error = Errors::fromProcess(p, QStringLiteral("qemu-img"));
        if (p.state() != QProcess::NotRunning) {
            info.error = QStringLiteral("Reading the image took longer than 30 seconds. Is its drive asleep, slow or "
                                        "disconnected?");
            p.kill();
            p.waitForFinished(1000);
        }
        return info;
    }
    QJsonParseError pe;
    const QJsonObject o = QJsonDocument::fromJson(p.readAllStandardOutput(), &pe).object();
    if (pe.error != QJsonParseError::NoError || !o.contains(QStringLiteral("format"))) {
        info.error = QStringLiteral("qemu-img returned information BentoVM couldn't read (%1).").arg(pe.errorString());
        return info;
    }
    info.ok = true;
    info.format = o[QStringLiteral("format")].toString();
    info.virtualSize = qint64(o[QStringLiteral("virtual-size")].toDouble());
    info.actualSize = qint64(o[QStringLiteral("actual-size")].toDouble());
    return info;
}

static const QString kCreateOptions = QStringLiteral("lazy_refcounts=on,preallocation=metadata");

// Bytes `qemu-img create` writes for the preallocated metadata; 0 if unknown.
static qint64 metadataBytes(int sizeGiB)
{
    QProcess p;
    p.setProcessEnvironment(Paths::hostEnvironment());
    p.start(qemuImg(), {QStringLiteral("measure"), QStringLiteral("-O"), QStringLiteral("qcow2"), QStringLiteral("--size"),
                        QStringLiteral("%1G").arg(sizeGiB), QStringLiteral("-o"), kCreateOptions,
                        QStringLiteral("--output=json")});
    if (!p.waitForFinished(10000) || p.exitCode() != 0) {
        p.kill();
        p.waitForFinished(1000);
        return 0;
    }
    return qint64(QJsonDocument::fromJson(p.readAllStandardOutput()).object()[QStringLiteral("required")].toDouble());
}

bool createDisk(const QString &path, int sizeGiB, QString *error, const Progress &progress)
{
    if (QFileInfo::exists(path))
        return failWith(error, QStringLiteral("“%1” already exists.").arg(path));
    QProcess p;
    p.setProgram(qemuImg());
    p.setArguments({QStringLiteral("create"), QStringLiteral("-q"), QStringLiteral("-f"), QStringLiteral("qcow2"),
                    QStringLiteral("-o"), kCreateOptions, path, QStringLiteral("%1G").arg(sizeGiB)});
    const qint64 total = progress.report ? metadataBytes(sizeGiB) : 0;
    if (!runWithProgress(p, QStringLiteral("qemu-img"), path, total, progress, error)) {
        QFile::remove(path);
        return false;
    }
    return true;
}

bool copyDisk(const QString &source, const QString &destination, QString *error, const Progress &progress)
{
    if (QFileInfo::exists(destination))
        return failWith(error, QStringLiteral("“%1” already exists.").arg(destination));
    QProcess p;
    p.setProgram(QStringLiteral("cp"));
    p.setArguments({QStringLiteral("--reflink=auto"), QStringLiteral("--sparse=always"), source, destination});
    if (!runWithProgress(p, QStringLiteral("cp"), destination, qMax<qint64>(1, allocatedBytes(source)), progress, error)) {
        QFile::remove(destination);
        return false;
    }
    return true;
}

QString formatForPath(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("qcow2") || ext == QLatin1String("qcow"))
        return QStringLiteral("qcow2");
    if (ext == QLatin1String("vmdk"))
        return QStringLiteral("vmdk");
    if (ext == QLatin1String("vdi"))
        return QStringLiteral("vdi");
    if (ext == QLatin1String("vhd") || ext == QLatin1String("vpc"))
        return QStringLiteral("vpc");
    if (ext == QLatin1String("vhdx"))
        return QStringLiteral("vhdx");
    return QStringLiteral("raw");
}

} // namespace DiskOps
