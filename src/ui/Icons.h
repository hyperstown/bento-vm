// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QIcon>
#include <QString>

// Icons from the desktop's icon theme, findable on non-KDE desktops too.
// BentoVM asks for KDE/freedesktop names ("configure", "list-add", …). Themes
// such as GNOME's Adwaita lack some of them or only ship single-colour
// "-symbolic" glyphs, which GTK draws in the text colour. This tries the name,
// then standard alternatives, then their symbolic variants, drawn in the
// palette's text colour. Desktops without a Qt style (GNOME, …) get the
// symbolic glyphs first, like their own applications.
namespace Icons {

QIcon fromTheme(const QString &name);
QIcon fromTheme(const QString &name, const QIcon &fallback);
bool isSymbolic(const QIcon &icon);

} // namespace Icons
