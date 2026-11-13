// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DiskOps.h"
#include "Paths.h"
#include "HostInfo.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

#include <sys/stat.h>

namespace DiskOps {

static QString qemuImg()
{
    const QString p = HostInfo::get().qemuImg;
    return p.isEmpty() ? QStringLiteral("qemu-img") : p;
}

ImageInfo imageInfo(const QString &path)
{
    ImageInfo info;
    QProcess p;
    p.setProcessEnvironment(Paths::hostEnvironment());
    // -U: don't take the image lock, so this also works while a VM uses it.
    p.start(qemuImg(), {QStringLiteral("info"), QStringLiteral("-U"), QStringLiteral("--output=json"), path});
    if (!p.waitForFinished(15000) || p.exitCode() != 0) {
        info.error = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
        if (info.error.isEmpty())
            info.error = QStringLiteral("qemu-img failed");
        return info;
    }
    const QJsonObject o = QJsonDocument::fromJson(p.readAllStandardOutput()).object();
    info.ok = true;
    info.format = o[QStringLiteral("format")].toString();
    info.virtualSize = qint64(o[QStringLiteral("virtual-size")].toDouble());
    info.actualSize = qint64(o[QStringLiteral("actual-size")].toDouble());
    return info;
}

bool createDisk(const QString &path, int sizeGiB, QString *error)
{
    QProcess p;
    p.setProcessEnvironment(Paths::hostEnvironment());
    p.start(qemuImg(), {QStringLiteral("create"), QStringLiteral("-q"), QStringLiteral("-f"), QStringLiteral("qcow2"),
                        QStringLiteral("-o"), QStringLiteral("lazy_refcounts=on,preallocation=metadata"),
                        path, QStringLiteral("%1G").arg(sizeGiB)});
    if (!p.waitForFinished(60000) || p.exitCode() != 0) {
        if (error)
            *error = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
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

DiskCopyJob::DiskCopyJob(const QString &src, const QString &dst, QObject *parent)
    : QObject(parent)
    , m_src(src)
    , m_dst(dst)
{
}

static qint64 allocatedBytes(const QString &path)
{
    struct stat st{};
    if (::stat(QFile::encodeName(path).constData(), &st) != 0)
        return 0;
    return qint64(st.st_blocks) * 512;
}

void DiskCopyJob::start()
{
    m_total = qMax<qint64>(1, allocatedBytes(m_src));
    m_proc = new QProcess(this);
    m_proc->setProcessEnvironment(Paths::hostEnvironment());
    m_timer = new QTimer(this);
    m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, [this] {
        Q_EMIT progress(int(qMin<qint64>(99, allocatedBytes(m_dst) * 100 / m_total)));
    });
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        m_timer->stop();
        const bool ok = st == QProcess::NormalExit && code == 0;
        if (!ok)
            QFile::remove(m_dst);
        Q_EMIT progress(100);
        Q_EMIT finished(ok, ok ? QString() : QString::fromLocal8Bit(m_proc->readAllStandardError()).trimmed());
    });
    m_proc->start(QStringLiteral("cp"), {QStringLiteral("--reflink=auto"), QStringLiteral("--sparse=always"), m_src, m_dst});
    m_timer->start();
}

void DiskCopyJob::cancel()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning)
        m_proc->kill();
}
