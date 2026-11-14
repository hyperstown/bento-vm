// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "VmConfig.h"

#include <QFutureWatcher>
#include <QObject>

#include <atomic>
#include <memory>

// Creates a new VM's folder, disk and vm.json on a worker thread, so slow or
// sleeping drives don't freeze the UI.
class VmCreator : public QObject
{
    Q_OBJECT
public:
    struct Request
    {
        VmConfig config;              // complete except for dir and disks
        QString location;             // storage location; the VM gets a new folder in it
        int newDiskGiB = 0;           // > 0: create disk.qcow2 of this size…
        QString newDiskBus;
        QString existingDisk;         // …otherwise use this disk image,
        bool copyExistingDisk = false;   // optionally copied into the VM folder
        QString existingDiskBus;
    };

    struct Result
    {
        bool ok = false;
        bool canceled = false;
        VmConfig config;    // the saved VM when ok
        QString title;      // what failed, e.g. "Couldn't create the virtual disk"
        QString error;      // why; never empty when !ok && !canceled
        QString details;    // technical details for bug reports
    };

    explicit VmCreator(QObject *parent = nullptr);
    ~VmCreator() override;   // cancels and waits for the worker

    void start(const Request &request);
    void cancel();
    bool isRunning() const;

Q_SIGNALS:
    void progress(const QString &step, int percent);   // percent < 0: unknown
    void finished(const VmCreator::Result &result);

private:
    std::shared_ptr<std::atomic_bool> m_cancel;
    QFutureWatcher<Result> m_watcher;
};
