// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>

class QWidget;

namespace ErrorDialog {

// Shows a warning that says what failed (`title`, e.g. "Couldn't create the
// virtual disk") and why (`reason`). Technical `details` (paths, commands,
// program output) go into the collapsible details along with version
// information, for bug reports. Never shows a blank message, and logs
// everything to stderr (category bento.errors).
void show(QWidget *parent, const QString &title, const QString &reason, const QString &details = {});

} // namespace ErrorDialog
