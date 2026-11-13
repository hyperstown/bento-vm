#include "VmLibrary.h"
#include "AppSettings.h"
#include "Paths.h"
#include "Vm.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

VmLibrary *VmLibrary::instance()
{
    static VmLibrary *lib = new VmLibrary;
    return lib;
}

VmLibrary::VmLibrary()
    : m_timer(new QTimer(this))
{
    loadCache();
    // Cheap stat()-based polling notices drives being plugged/unplugged.
    m_timer->setInterval(3000);
    connect(m_timer, &QTimer::timeout, this, &VmLibrary::refresh);
    m_timer->start();
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this](const QString &key) {
        if (key.startsWith(QLatin1String("storage/")) || key.startsWith(QLatin1String("library/")))
            refresh();
    });
}

Vm *VmLibrary::find(const QString &id) const
{
    for (Vm *vm : m_vms)
        if (vm->id() == id)
            return vm;
    return nullptr;
}

Vm *VmLibrary::findByName(const QString &name) const
{
    for (Vm *vm : m_vms)
        if (vm->name().compare(name, Qt::CaseInsensitive) == 0)
            return vm;
    return nullptr;
}

bool VmLibrary::nameInUse(const QString &name) const
{
    return findByName(name.trimmed()) != nullptr;
}

QString VmLibrary::locationFor(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    QString best;
    for (const QString &loc : AppSettings::instance().storageLocations()) {
        if ((clean == loc || clean.startsWith(loc + QLatin1Char('/'))) && loc.size() > best.size())
            best = loc;
    }
    return best;
}

QString VmLibrary::uniqueFolder(const QString &location, const QString &name)
{
    QString base = name.trimmed();
    base.replace(QLatin1Char('/'), QLatin1Char('-'));
    if (base.isEmpty() || base.startsWith(QLatin1Char('.')))
        base = QStringLiteral("vm") + base;
    QString candidate = location + QLatin1Char('/') + base;
    for (int n = 2; QFileInfo::exists(candidate); ++n)
        candidate = location + QLatin1Char('/') + base + QStringLiteral(" %1").arg(n);
    return candidate;
}

void VmLibrary::loadCache()
{
    QFile f(Paths::libraryCacheFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject vms = root[QStringLiteral("vms")].toObject();
    for (auto it = vms.begin(); it != vms.end(); ++it)
        m_cache.insert(it.key(), it.value().toObject());
}

void VmLibrary::saveCache()
{
    QJsonObject vms;
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it)
        vms.insert(it.key(), it.value());
    Paths::ensureDir(Paths::dataDir());
    QSaveFile f(Paths::libraryCacheFile());
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("vms"), vms}}).toJson(QJsonDocument::Compact));
    f.commit();
}

Vm *VmLibrary::createVm(const VmConfig &cfg)
{
    auto *vm = new Vm(cfg, this);
    m_vms << vm;
    Q_EMIT vmAdded(vm);
    vm->tryAttach();
    return vm;
}

static QString tailOf(const QString &path)
{
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.size() <= 2 ? path : QStringLiteral("…/") + parts.mid(parts.size() - 2).join(QLatin1Char('/'));
}

void VmLibrary::markUnavailable(Vm *vm, const VmConfig &cfg, bool dirMissing, const QString &missingPath) const
{
    if (dirMissing) {
        const QString loc = locationFor(cfg.dir);
        if (!loc.isEmpty() && !QFileInfo::exists(loc)) {
            vm->setAvailability(false,
                                QStringLiteral("Stored in %1, which isn't available right now. Connect the drive to boot this machine.").arg(loc),
                                QStringLiteral("Drive not connected\n%1").arg(tailOf(loc)));
            return;
        }
        vm->setAvailability(false,
                            QStringLiteral("The VM folder can't be found: %1. If it lives on an external drive, connect it.").arg(cfg.dir),
                            QStringLiteral("Folder not found\n%1").arg(tailOf(cfg.dir)));
        return;
    }
    vm->setAvailability(false,
                        QStringLiteral("Disk image not available: %1. If it lives on an external drive, connect it.").arg(missingPath),
                        QStringLiteral("Disk image not found\n%1").arg(tailOf(missingPath)));
}

void VmLibrary::refresh()
{
    const AppSettings &s = AppSettings::instance();
    const QStringList ignored = s.ignoredDirs();
    QHash<QString, VmConfig> found;
    bool cacheDirty = false;

    auto loadDir = [&](const QString &dir) {
        const QString file = dir + QLatin1Char('/') + VmConfig::fileName();
        const QFileInfo fi(file);
        if (!fi.exists())
            return;
        VmConfig cfg;
        if (!VmConfig::load(dir, &cfg))
            return;
        if (!found.contains(cfg.id))
            found.insert(cfg.id, cfg);
        m_mtimes.insert(file, fi.lastModified());
    };

    for (const QString &loc : s.storageLocations()) {
        QDir d(loc);
        if (!d.exists())
            continue;
        for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString dir = QDir::cleanPath(loc + QLatin1Char('/') + sub);
            if (!ignored.contains(dir))
                loadDir(dir);
        }
    }
    // VMs imported from folders outside the storage locations.
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
        if (found.contains(it.key()))
            continue;
        const QString dir = it.value()[QStringLiteral("dir")].toString();
        if (!ignored.contains(dir))
            loadDir(dir);
    }

    for (auto it = found.cbegin(); it != found.cend(); ++it) {
        const VmConfig &cfg = it.value();
        Vm *vm = find(cfg.id);
        if (!vm) {
            vm = createVm(cfg);
        } else if (!vm->isActive() && vm->config().toJson() != cfg.toJson()) {
            vm->reloadConfig(cfg);
        } else if (vm->config().dir != cfg.dir && !vm->isActive()) {
            vm->reloadConfig(cfg);
        }
        const QString missing = cfg.firstMissingRequiredPath();
        if (!vm->isActive()) {
            if (missing.isEmpty())
                vm->setAvailability(true);
            else
                markUnavailable(vm, cfg, false, missing);
        }

        const QJsonObject entry{{QStringLiteral("dir"), cfg.dir}, {QStringLiteral("config"), cfg.toJson()}};
        if (m_cache.value(cfg.id) != entry) {
            m_cache.insert(cfg.id, entry);
            cacheDirty = true;
        }
    }

    // Known VMs that weren't found: their drive is probably unplugged.
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
        if (found.contains(it.key()))
            continue;
        const QString dir = it.value()[QStringLiteral("dir")].toString();
        if (ignored.contains(dir))
            continue;
        VmConfig cfg = VmConfig::fromJson(it.value()[QStringLiteral("config")].toObject());
        cfg.dir = dir;
        if (cfg.id.isEmpty())
            cfg.id = it.key();
        Vm *vm = find(cfg.id);
        if (!vm)
            vm = createVm(cfg);
        if (!vm->isActive())
            markUnavailable(vm, cfg, true, QString());
    }

    // VMs whose folder was explicitly ignored/forgotten disappear from the list.
    const QList<Vm *> current = m_vms;
    for (Vm *vm : current) {
        if (ignored.contains(vm->config().dir) && !vm->isActive()) {
            m_vms.removeOne(vm);
            m_cache.remove(vm->id());
            cacheDirty = true;
            Q_EMIT vmRemoved(vm);
            vm->deleteLater();
        }
    }

    if (cacheDirty)
        saveCache();
}

Vm *VmLibrary::add(const VmConfig &cfg)
{
    AppSettings::instance().removeIgnoredDir(cfg.dir);
    m_cache.insert(cfg.id, QJsonObject{{QStringLiteral("dir"), cfg.dir}, {QStringLiteral("config"), cfg.toJson()}});
    saveCache();
    Vm *vm = find(cfg.id);
    if (!vm)
        vm = createVm(cfg);
    refresh();
    return vm;
}

bool VmLibrary::importFolder(const QString &dir, QString *error)
{
    VmConfig cfg;
    if (!VmConfig::load(dir, &cfg, error))
        return false;
    if (Vm *existing = find(cfg.id)) {
        if (existing->config().dir != cfg.dir) {
            if (error)
                *error = QStringLiteral("A VM with the same identity is already in the library (%1).").arg(existing->name());
            return false;
        }
    }
    add(cfg);
    return true;
}

bool VmLibrary::remove(Vm *vm, bool deleteFiles, QString *error)
{
    if (vm->isActive()) {
        if (error)
            *error = QStringLiteral("Shut the VM down first.");
        return false;
    }
    const VmConfig cfg = vm->config();
    if (deleteFiles) {
        // Only delete a real Bento VM folder (must contain vm.json).
        if (QFileInfo::exists(cfg.configFile())) {
            if (!QDir(cfg.dir).removeRecursively()) {
                if (error)
                    *error = QStringLiteral("Couldn't delete %1 completely.").arg(cfg.dir);
                return false;
            }
        }
        AppSettings::instance().removeIgnoredDir(cfg.dir);
    } else {
        AppSettings::instance().addIgnoredDir(cfg.dir);
    }
    QFile::remove(Paths::thumbnailFile(cfg.id));
    QFile::remove(Paths::logFile(cfg.id));
    m_cache.remove(cfg.id);
    saveCache();
    m_vms.removeOne(vm);
    Q_EMIT vmRemoved(vm);
    vm->deleteLater();
    return true;
}
