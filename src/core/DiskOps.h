// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>

#include <functional>

namespace DiskOps {

struct ImageInfo
{
    bool ok = false;
    QString format;
    qint64 virtualSize = 0;
    qint64 actualSize = 0;
    QString error;   // set when !ok
};

// Progress of a long disk operation.
struct Progress
{
    std::function<void(int percent)> report;   // 0–100
    std::function<bool()> canceled;            // polled; returning true stops the operation
};

ImageInfo imageInfo(const QString &path);

// The functions below block until done, which can take minutes for large disks
// on slower drives, so call them on a worker thread. They have no time limit;
// use Progress::canceled to stop them. On failure or cancellation the partial
// file is removed and *error says why.

// A new qcow2 image with preallocated metadata.
bool createDisk(const QString &path, int sizeGiB, QString *error, const Progress &progress = {});
// Copies an image (instant on btrfs/xfs thanks to reflinks; keeps it sparse).
bool copyDisk(const QString &source, const QString &destination, QString *error, const Progress &progress = {});

QString formatForPath(const QString &path);

} // namespace DiskOps
