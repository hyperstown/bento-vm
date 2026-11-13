// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <QString>

class QProcess;
class QTimer;

namespace DiskOps {

struct ImageInfo
{
    bool ok = false;
    QString format;
    qint64 virtualSize = 0;
    qint64 actualSize = 0;
    QString error;
};

ImageInfo imageInfo(const QString &path);
bool createDisk(const QString &path, int sizeGiB, QString *error);
QString formatForPath(const QString &path);

} // namespace DiskOps

// Copies (or converts) a disk image in the background, reporting progress.
// Uses `cp --reflink=auto` so copies on btrfs/xfs are instant.
class DiskCopyJob : public QObject
{
    Q_OBJECT
public:
    DiskCopyJob(const QString &src, const QString &dst, QObject *parent = nullptr);
    void start();
    void cancel();

Q_SIGNALS:
    void progress(int percent);
    void finished(bool ok, const QString &error);

private:
    QString m_src, m_dst;
    qint64 m_total = 0;
    QProcess *m_proc = nullptr;
    QTimer *m_timer = nullptr;
};
