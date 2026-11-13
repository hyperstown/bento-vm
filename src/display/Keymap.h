// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

// Converts Linux evdev key codes (what Qt reports as nativeScanCode() - 8 on
// both Wayland and X11) to QEMU "qnum" codes: XT set-1 scancodes with the
// 0xE0 prefix folded into the high bit. Returns 0 for unmapped keys.
namespace Keymap {
uint32_t evdevToQnum(uint32_t evdev);
}
