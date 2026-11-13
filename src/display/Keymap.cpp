#include "Keymap.h"

#include <unordered_map>

namespace Keymap {

uint32_t evdevToQnum(uint32_t ev)
{
    // KEY_ESC (1) .. KEY_KPDOT (83) and KEY_102ND/F11/F12 match XT set 1 directly.
    if ((ev >= 1 && ev <= 83) || ev == 86 || ev == 87 || ev == 88)
        return ev;

    static const std::unordered_map<uint32_t, uint32_t> table = {
        {85, 0x76},   // KEY_ZENKAKUHANKAKU
        {89, 0x73},   // KEY_RO
        {90, 0x78},   // KEY_KATAKANA
        {91, 0x77},   // KEY_HIRAGANA
        {92, 0x79},   // KEY_HENKAN
        {93, 0x70},   // KEY_KATAKANAHIRAGANA
        {94, 0x7b},   // KEY_MUHENKAN
        {95, 0x5c},   // KEY_KPJPCOMMA
        {96, 0x9c},   // KEY_KPENTER
        {97, 0x9d},   // KEY_RIGHTCTRL
        {98, 0xb5},   // KEY_KPSLASH
        {99, 0xb7},   // KEY_SYSRQ (Print Screen)
        {100, 0xb8},  // KEY_RIGHTALT
        {102, 0xc7},  // KEY_HOME
        {103, 0xc8},  // KEY_UP
        {104, 0xc9},  // KEY_PAGEUP
        {105, 0xcb},  // KEY_LEFT
        {106, 0xcd},  // KEY_RIGHT
        {107, 0xcf},  // KEY_END
        {108, 0xd0},  // KEY_DOWN
        {109, 0xd1},  // KEY_PAGEDOWN
        {110, 0xd2},  // KEY_INSERT
        {111, 0xd3},  // KEY_DELETE
        {113, 0xa0},  // KEY_MUTE
        {114, 0xae},  // KEY_VOLUMEDOWN
        {115, 0xb0},  // KEY_VOLUMEUP
        {116, 0xde},  // KEY_POWER
        {117, 0x59},  // KEY_KPEQUAL
        {119, 0xc6},  // KEY_PAUSE
        {121, 0x7e},  // KEY_KPCOMMA
        {122, 0xf2},  // KEY_HANGEUL
        {123, 0xf1},  // KEY_HANJA
        {124, 0x7d},  // KEY_YEN
        {125, 0xdb},  // KEY_LEFTMETA
        {126, 0xdc},  // KEY_RIGHTMETA
        {127, 0xdd},  // KEY_COMPOSE (Menu)
        {140, 0xa1},  // KEY_CALC
        {142, 0xdf},  // KEY_SLEEP
        {143, 0xe3},  // KEY_WAKEUP
        {155, 0xec},  // KEY_MAIL
        {156, 0xe6},  // KEY_BOOKMARKS
        {157, 0xeb},  // KEY_COMPUTER
        {158, 0xea},  // KEY_BACK
        {159, 0xe9},  // KEY_FORWARD
        {163, 0x99},  // KEY_NEXTSONG
        {164, 0xa2},  // KEY_PLAYPAUSE
        {165, 0x90},  // KEY_PREVIOUSSONG
        {166, 0xa4},  // KEY_STOPCD
        {172, 0xb2},  // KEY_HOMEPAGE
        {173, 0xe7},  // KEY_REFRESH
        {183, 0x5d},  // KEY_F13
        {184, 0x5e},  // KEY_F14
        {185, 0x5f},  // KEY_F15
        {217, 0xe5},  // KEY_SEARCH
        {226, 0xed},  // KEY_MEDIA
    };
    const auto it = table.find(ev);
    return it == table.end() ? 0 : it->second;
}

} // namespace Keymap
