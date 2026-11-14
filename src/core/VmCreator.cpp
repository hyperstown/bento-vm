// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VmCreator.h"
#include "DiskOps.h"
#include "Paths.h"
#include "VmLibrary.h"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <QtConcurrent>

namespace {

using Report = std::function<void(const QString &step, int percent)>;

// Where the VM is stored, for bug reports.
QString storageDetails(const QString &path)
{
    const QStorageInfo si(path);
    if (!si.isValid())
        return QStringLiteral("Storage location: %1").arg(path);
    return QStringLiteral("Storage location: %1\nDrive: %2 (%3, %4 free of %5%6)")
        .arg(path, QString::fromLocal8Bit(si.device()), QString::fromLatin1(si.fileSystemType()),
             Paths::humanSize(si.bytesAvailable()), Paths::humanSize(si.bytesTotal()),
             si.isReadOnly() ? QStringLiteral(", read-only") : QString());
}

VmCreator::Result run(VmCreator::Request r, const std::shared_ptr<std::atomic_bool> &cancel, const Report &report)
{
    VmCreator::Result res;
    VmConfig &c = r.config;
    const auto canceled = [&cancel] { return cancel->load(); };
    QString err;

    report(QStringLiteral("Creating the VM folder…"), -1);
    const QString dir = VmLibrary::uniqueFolder(r.location, c.name);
    if (!Paths::ensureDir(dir, false, &err)) {
        res.title = QStringLiteral("Couldn't create the VM folder");
        res.error = err;
        res.details = storageDetails(r.location);
        return res;
    }
    c.dir = dir;
    // From here on, the new folder goes away again unless everything worked.
    const auto fail = [&](const QString &title, const QString &error, const QString &details) {
        QDir(dir).removeRecursively();
        res.canceled = canceled();
        res.title = title;
        res.error = error;
        res.details = (details.isEmpty() ? QString() : details + QLatin1Char('\n')) + QStringLiteral("VM folder: ") + dir
                      + QLatin1Char('\n') + storageDetails(r.location);
        return res;
    };

    if (r.newDiskGiB > 0) {
        const QString step = QStringLiteral("Creating the virtual disk (%1 GiB)…").arg(r.newDiskGiB);
        report(step, 0);
        const DiskOps::Progress progress{[&](int percent) { report(step, percent); }, canceled};
        const QString path = dir + QStringLiteral("/disk.qcow2");
        if (!DiskOps::createDisk(path, r.newDiskGiB, &err, progress))
            return fail(QStringLiteral("Couldn't create the virtual disk"), err,
                        QStringLiteral("Disk: %1 (%2 GiB, qcow2)").arg(path).arg(r.newDiskGiB));
        c.disks << DiskSpec{QStringLiteral("disk.qcow2"), QStringLiteral("qcow2"), r.newDiskBus};
    } else if (!r.existingDisk.isEmpty()) {
        const QString src = QFileInfo(r.existingDisk).absoluteFilePath();
        report(QStringLiteral("Reading the disk image…"), -1);
        const DiskOps::ImageInfo info = DiskOps::imageInfo(src);
        const QString format = info.ok ? info.format : DiskOps::formatForPath(src);
        if (r.copyExistingDisk) {
            const QString suffix = QFileInfo(src).suffix();
            const QString name = QStringLiteral("disk.") + (suffix.isEmpty() ? format : suffix);
            const QString step = QStringLiteral("Copying the disk image…");
            report(step, 0);
            const DiskOps::Progress progress{[&](int percent) { report(step, percent); }, canceled};
            if (!DiskOps::copyDisk(src, dir + QLatin1Char('/') + name, &err, progress))
                return fail(QStringLiteral("Couldn't copy the disk image"), err,
                            QStringLiteral("From: %1\nTo: %2/%3").arg(src, dir, name));
            c.disks << DiskSpec{name, format, r.existingDiskBus};
        } else {
            c.disks << DiskSpec{src, format, r.existingDiskBus};
        }
    }
    if (canceled())
        return fail(QString(), QStringLiteral("Canceled."), QString());

    report(QStringLiteral("Saving the VM settings…"), -1);
    if (!c.save(&err))
        return fail(QStringLiteral("Couldn't save the VM"), err, QString());
    res.ok = true;
    res.config = c;
    return res;
}

} // namespace

VmCreator::VmCreator(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this] { Q_EMIT finished(m_watcher.result()); });
}

VmCreator::~VmCreator()
{
    if (m_watcher.isRunning()) {
        cancel();
        m_watcher.waitForFinished();   // the worker reports to this object
    }
}

void VmCreator::start(const Request &request)
{
    if (isRunning())
        return;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    // Delivered in this object's thread; dropped if it's gone by then.
    const Report report = [this](const QString &step, int percent) {
        QMetaObject::invokeMethod(this, [this, step, percent] { Q_EMIT progress(step, percent); }, Qt::QueuedConnection);
    };
    m_watcher.setFuture(QtConcurrent::run([request, cancel = m_cancel, report] { return run(request, cancel, report); }));
}

void VmCreator::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
}

bool VmCreator::isRunning() const
{
    return m_watcher.isRunning();
}
