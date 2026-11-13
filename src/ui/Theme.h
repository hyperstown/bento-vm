// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QColor>
#include <QIcon>
#include <QList>
#include <QObject>
#include <QString>

// Themes, by id:
//   "auto"          (default) "system" where the desktop provides a Qt style
//                   (Breeze on KDE Plasma, or one the user set up), otherwise
//                   the Modern theme matching the desktop's colour scheme
//   "system"        whatever the desktop provides
//   "modern-dark"   self-contained Fusion + stylesheet looks
//   "modern-light"
class QPalette;

class Theme : public QObject
{
    Q_OBJECT
public:
    struct Info
    {
        QString id;
        QString name;
        QString description;
    };

    static Theme *instance();
    static QList<Info> available();

    void init();                       // remember the desktop's style/palette
    void apply(const QString &id);
    QString selected() const { return m_selected; }   // the id applied, e.g. "auto"
    QString current() const { return m_current; }     // the look shown: "system" or a Modern theme
    bool isCustom() const { return m_current != QLatin1String("system"); }
    bool isDark() const;

    bool hasDesktopStyle() const;      // false where Qt falls back to its plain built-in style
    QString desktopStyleName() const;  // e.g. "Breeze"
    QString automaticLook() const;     // what "auto" shows right now

    QColor accent() const;
    static QIcon onAccent(const QIcon &icon);   // drawn white on the Modern themes' green buttons
    static QColor stateColor(const QString &state);   // running/paused/stopped/starting/unavailable

Q_SIGNALS:
    void changed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Theme() = default;
    QString m_selected = QStringLiteral("auto");
    QString m_current = QStringLiteral("system");
    QString m_origStyle;
    QPalette *m_origPalette = nullptr;
};
