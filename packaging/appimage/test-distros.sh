#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 hyperstown
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Smoke-tests dist/BentoVM-*.AppImage on several distributions. Each distro
# runs in a throwaway podman container with an ordinary desktop's libraries
# and icon themes, connected to a headless KWin on the host. BentoVM is started
# as in a KDE Plasma session and as in a GNOME session:
#   KDE Plasma  Qt's Wayland platform, the Breeze style and Plasma's platform
#               theme load, and the Automatic theme picks the native look
#   GNOME       the Automatic theme picks Modern Light/Dark
#   both        no bundled plugin fails to load (e.g. a library missing on that
#               distro) and the icon theme has every icon BentoVM asks for
#
#   packaging/appimage/test-distros.sh [image ...]
#   (default: fedora:latest ubuntu:24.04 ubuntu:26.04 archlinux:latest)
set -euo pipefail
cd "$(dirname "$0")/../.."

APPIMAGE=$(ls dist/BentoVM-*-x86_64.AppImage 2>/dev/null | tail -1)
[ -n "$APPIMAGE" ] || { echo "No AppImage in dist/, run build.sh first"; exit 1; }
IMAGES=("$@")
[ ${#IMAGES[@]} -gt 0 ] || IMAGES=(fedora:latest ubuntu:24.04 ubuntu:26.04 archlinux:latest)

SOCK=bento-appimage-test
kwin_wayland --virtual --socket "$SOCK" --width 1280 --height 800 >/dev/null 2>&1 &
KWIN=$!
trap 'kill $KWIN 2>/dev/null || true' EXIT
for _ in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/$SOCK" ] && break; sleep 0.1; done

# Runs inside each container.
read -r -d '' INSIDE <<'EOF' || true
set -e
exec 3>&1 >/tmp/install.log 2>&1   # quiet unless installing fails
trap '[ $? = 0 ] || cat /tmp/install.log >&3' EXIT
if command -v apt-get >/dev/null; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends libegl1 libgl1 libopengl0 libglx0 libgl1-mesa-dri \
        fontconfig fonts-noto-core libharfbuzz0b libx11-6 libx11-xcb1 libxcb1 libsm6 libice6 \
        libwayland-client0 libwayland-cursor0 libwayland-egl1 libdbus-1-3 libsystemd0 libudev1 \
        libgmp10 libcom-err2 libxkbcommon0 adwaita-icon-theme breeze-icon-theme
    apt-get install -y -qq --no-install-recommends libglib2.0-0t64 \
        || apt-get install -y -qq --no-install-recommends libglib2.0-0
elif command -v dnf >/dev/null; then
    dnf -q install -y mesa-libEGL mesa-dri-drivers libglvnd-opengl libglvnd-glx fontconfig harfbuzz \
        google-noto-sans-fonts libxkbcommon libX11-xcb libSM libICE libwayland-client libwayland-cursor \
        libwayland-egl glib2 dbus-libs systemd-libs adwaita-icon-theme breeze-icon-theme
elif command -v pacman >/dev/null; then
    pacman -Syu --noconfirm --needed mesa libglvnd fontconfig harfbuzz noto-fonts libxkbcommon libx11 \
        libxcb libsm libice wayland glib2 adwaita-icon-theme breeze-icons
fi
exec 1>&3 2>&1 3>&-
trap - EXIT
. /etc/os-release
echo "$PRETTY_NAME"
mkdir -p /run/wl && chmod 700 /run/wl
cd /tmp && /dist/$APPIMAGE_NAME --appimage-extract >/dev/null
export LANG=C.UTF-8 XDG_RUNTIME_DIR=/run/wl WAYLAND_DISPLAY=$SOCK QT_FORCE_STDERR_LOGGING=1 \
       QT_LOGGING_RULES="qt.core.library=true;qt.qpa.*=false;bento.*.debug=true"

FAILED=0
pass() { echo "    PASS  $1"; }
fail() { echo "    FAIL  $1"; FAILED=1; }
check() {   # <label> <pattern in the log>
    if grep -q -- "$2" "$LOG"; then pass "$1"; else fail "$1"; fi
}
# run <session> [VAR=value ...]: BentoVM for 8 s with its own configuration
run() {
    local session=$1; shift
    LOG=/tmp/$session.log
    echo "  $session session"
    env XDG_CONFIG_HOME=/tmp/$session/config XDG_DATA_HOME=/tmp/$session/data "$@" \
        timeout 8 ./squashfs-root/AppRun >"$LOG" 2>&1 || true
    if ! grep -q "loaded library" "$LOG"; then
        fail "BentoVM didn't start:"
        grep -iE "error|cannot|not found|abort|fatal" "$LOG" | head -5 | sed 's/^/          /'
        return
    fi
    check "Wayland platform plugin" 'libqwayland.so" loaded library'
    grep -h "^bento.theme:" "$LOG" | head -1 | sed 's/^/          /'
}
problems() {   # plugins that failed to load, icons the icon theme lacks
    if grep -q "cannot load" "$LOG"; then
        FAILED=1
        grep "cannot load" "$LOG" | sed 's/^.*cannot load: /    FAIL  /' | sort -u
    fi
    if grep -q "^bento.icons: no icon" "$LOG"; then
        FAILED=1
        grep "^bento.icons: no icon" "$LOG" | sed 's/^bento.icons: /    FAIL  /' | sort -u
    fi
}

mkdir -p /tmp/kde/config
printf '[KDE]\nwidgetStyle=Breeze\n[Icons]\nTheme=breeze\n' > /tmp/kde/config/kdeglobals
run kde XDG_CURRENT_DESKTOP=KDE KDE_SESSION_VERSION=6 KDE_FULL_SESSION=true
check "Breeze style" 'breeze6.so" loaded library'
check "Plasma platform theme" 'KDEPlasmaPlatformTheme6.so" loaded library'
check "native look" 'shows "system"'
problems

run gnome XDG_CURRENT_DESKTOP=GNOME
check "Modern theme" 'shows "modern-'
problems
exit $FAILED
EOF

STATUS=0
for image in "${IMAGES[@]}"; do
    echo "==> $image"
    podman run --rm -v "$XDG_RUNTIME_DIR/$SOCK:/run/wl/$SOCK" -v "$PWD/dist:/dist:ro" \
        -e SOCK="$SOCK" -e APPIMAGE_NAME="$(basename "$APPIMAGE")" \
        "docker.io/library/$image" bash -c "$INSIDE" || STATUS=1
done
exit $STATUS
