// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>

// "system" keeps whatever the desktop provides (Breeze on KDE). The other
// themes are self-contained Fusion + stylesheet looks.
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
    QString current() const { return m_current; }
    bool isCustom() const { return m_current != QLatin1String("system"); }
    bool isDark() const;

    QColor accent() const;
    static QColor stateColor(const QString &state);   // running/paused/stopped/starting/unavailable

Q_SIGNALS:
    void changed();

private:
    Theme() = default;
    QString m_current = QStringLiteral("system");
    QString m_origStyle;
    QPalette *m_origPalette = nullptr;
};
