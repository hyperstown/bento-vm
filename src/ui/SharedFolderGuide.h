// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/OsDb.h"

class QWidget;
class Vm;

// How to open a VM's shared folder, on the host and inside the guest.
namespace SharedFolderGuide {

// The steps for a guest of this OS family, with copyable commands.
QWidget *create(OsFamily family, QWidget *parent);

// Opens the shared folder in the host's file manager. Returns an error to
// show if it can't (no folder set, or it doesn't exist), otherwise "".
QString openOnHost(const Vm *vm);

// A dialog with the host folder and the guest steps.
void showDialog(Vm *vm, QWidget *parent);

} // namespace SharedFolderGuide
