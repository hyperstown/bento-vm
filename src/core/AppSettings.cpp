// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "AppSettings.h"
#include "Paths.h"

#include <QDir>

AppSettings &AppSettings::instance()
{
    static AppSettings s;
    return s;
}

AppSettings::AppSettings()
    : m_s(Paths::configDir() + QStringLiteral("/bento-vm.conf"), QSettings::IniFormat)
{
}

QVariant AppSettings::value(const QString &key, const QVariant &def) const
{
    return m_s.value(key, def);
}

void AppSettings::setValue(const QString &key, const QVariant &v)
{
    if (m_s.value(key) == v)
        return;
    m_s.setValue(key, v);
    m_s.sync();
    Q_EMIT changed(key);
}

static QString normalized(const QString &p)
{
    return QDir::cleanPath(QDir(p).absolutePath());
}

QStringList AppSettings::storageLocations() const
{
    QStringList list;
    for (const QString &p : m_s.value(QStringLiteral("storage/locations")).toStringList()) {
        const QString n = normalized(p);
        if (!n.isEmpty() && !list.contains(n))
            list << n;
    }
    if (list.isEmpty())
        list << Paths::defaultStorageLocation();
    return list;
}

void AppSettings::setStorageLocations(const QStringList &locations)
{
    QStringList list;
    for (const QString &p : locations) {
        const QString n = normalized(p);
        if (!n.isEmpty() && !list.contains(n))
            list << n;
    }
    setValue(QStringLiteral("storage/locations"), list);
    if (!list.contains(defaultLocation()) && !list.isEmpty())
        setDefaultLocation(list.first());
}

QString AppSettings::defaultLocation() const
{
    const QStringList locs = storageLocations();
    const QString d = normalized(m_s.value(QStringLiteral("storage/default")).toString());
    if (!d.isEmpty() && locs.contains(d))
        return d;
    return locs.first();
}

void AppSettings::setDefaultLocation(const QString &location)
{
    setValue(QStringLiteral("storage/default"), normalized(location));
}

QString AppSettings::theme() const { return m_s.value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString(); }
void AppSettings::setTheme(const QString &theme) { setValue(QStringLiteral("ui/theme"), theme); }

QString AppSettings::closeAction() const { return m_s.value(QStringLiteral("ui/closeAction"), QStringLiteral("ask")).toString(); }
void AppSettings::setCloseAction(const QString &action) { setValue(QStringLiteral("ui/closeAction"), action); }

QString AppSettings::defaultGraphics() const { return m_s.value(QStringLiteral("defaults/graphics"), QStringLiteral("venus")).toString(); }
QString AppSettings::defaultDisplay() const { return m_s.value(QStringLiteral("defaults/display"), QStringLiteral("embedded")).toString(); }
QString AppSettings::defaultClipboard() const { return m_s.value(QStringLiteral("defaults/clipboard"), QStringLiteral("both")).toString(); }
bool AppSettings::defaultFileTransfer() const { return m_s.value(QStringLiteral("defaults/fileTransfer"), true).toBool(); }
int AppSettings::defaultHostmemMiB() const { return m_s.value(QStringLiteral("defaults/hostmemMiB"), 4096).toInt(); }
QString AppSettings::defaultFirmware() const { return m_s.value(QStringLiteral("defaults/firmware"), QStringLiteral("uefi")).toString(); }

QString AppSettings::qemuBinary() const { return m_s.value(QStringLiteral("advanced/qemuBinary")).toString(); }
QString AppSettings::extraQemuArgs() const { return m_s.value(QStringLiteral("advanced/extraArgs")).toString(); }

QStringList AppSettings::ignoredDirs() const { return m_s.value(QStringLiteral("library/ignoredDirs")).toStringList(); }

void AppSettings::addIgnoredDir(const QString &dir)
{
    QStringList l = ignoredDirs();
    if (!l.contains(dir)) {
        l << dir;
        setValue(QStringLiteral("library/ignoredDirs"), l);
    }
}

void AppSettings::removeIgnoredDir(const QString &dir)
{
    QStringList l = ignoredDirs();
    if (l.removeAll(dir))
        setValue(QStringLiteral("library/ignoredDirs"), l);
}
