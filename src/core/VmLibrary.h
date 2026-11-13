// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "VmConfig.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>

class QTimer;
class Vm;

// All VMs Bento knows about. VMs are discovered by scanning the configured
// storage locations; a cache of every VM seen keeps machines on disconnected
// drives visible (marked unavailable) until the drive comes back.
class VmLibrary : public QObject
{
    Q_OBJECT
public:
    static VmLibrary *instance();

    const QList<Vm *> &vms() const { return m_vms; }
    Vm *find(const QString &id) const;
    Vm *findByName(const QString &name) const;
    bool nameInUse(const QString &name) const;

    void refresh();
    Vm *add(const VmConfig &cfg);
    bool importFolder(const QString &dir, QString *error);
    bool remove(Vm *vm, bool deleteFiles, QString *error);

    // Picks a folder name for a new VM inside `location` that doesn't exist yet.
    static QString uniqueFolder(const QString &location, const QString &name);
    // The configured storage location that contains `path`, if any.
    static QString locationFor(const QString &path);

Q_SIGNALS:
    void vmAdded(Vm *vm);
    void vmRemoved(Vm *vm);

private:
    VmLibrary();
    void loadCache();
    void saveCache();
    Vm *createVm(const VmConfig &cfg);
    void markUnavailable(Vm *vm, const VmConfig &cfg, bool dirMissing, const QString &missingPath) const;

    QList<Vm *> m_vms;
    QTimer *m_timer;
    QHash<QString, QJsonObject> m_cache;      // id -> {dir, config}
    QHash<QString, QDateTime> m_mtimes;       // config file -> mtime (skip re-parsing)
};
