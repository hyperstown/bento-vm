// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QIcon>
#include <QPixmap>

// OS artwork. Uses the icon theme's distributor-logo-* icons when present and
// otherwise draws a coloured badge with the OS initials.
namespace OsIcon {
QIcon icon(const QString &osId);
QPixmap badge(const QString &osId, int size, qreal dpr);
QPixmap placeholder(const QString &osId, const QSize &size, qreal dpr);   // thumbnail for VMs never run
}
