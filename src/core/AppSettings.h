// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QVariant>

// Application-wide preferences, stored in ~/.config/bento-vm/bento-vm.conf.
class AppSettings : public QObject
{
    Q_OBJECT
public:
    static AppSettings &instance();

    // Storage locations where VM folders live. Always contains at least one entry.
    QStringList storageLocations() const;
    void setStorageLocations(const QStringList &locations);
    QString defaultLocation() const;
    void setDefaultLocation(const QString &location);

    QString theme() const;                // "system" | "modern-dark" | "modern-light"
    void setTheme(const QString &theme);

    QString closeAction() const;          // "ask" | "keep" | "shutdown"
    void setCloseAction(const QString &action);

    // Defaults applied to new VMs.
    QString defaultGraphics() const;      // "venus" | "virgl" | "2d"
    QString defaultDisplay() const;       // "embedded" | "gtk" | "sdl"
    QString defaultClipboard() const;     // "both" | "host-to-guest" | "guest-to-host" | "off"
    bool defaultFileTransfer() const;
    int defaultHostmemMiB() const;
    QString defaultFirmware() const;      // "uefi" | "bios"

    QString qemuBinary() const;           // empty = auto
    QString extraQemuArgs() const;

    QStringList ignoredDirs() const;
    void addIgnoredDir(const QString &dir);
    void removeIgnoredDir(const QString &dir);

    QVariant value(const QString &key, const QVariant &def = {}) const;
    void setValue(const QString &key, const QVariant &v);

Q_SIGNALS:
    void changed(const QString &key);

private:
    AppSettings();
    mutable QSettings m_s;
};
