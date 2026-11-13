#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 hyperstown
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Smoke-tests dist/BentoVM-*.AppImage on several distributions. Each distro
# runs in a throwaway podman container with an ordinary desktop's libraries,
# connected to a headless KWin on the host. The check passes when Qt's Wayland
# platform, the Breeze style and Plasma's platform theme load and no bundled
# plugin fails to load (e.g. because a library is missing on that distro).
#
#   packaging/appimage/test-distros.sh [image ...]
#   (default: fedora:latest ubuntu:24.04 ubuntu:26.04)
set -euo pipefail
cd "$(dirname "$0")/../.."

APPIMAGE=$(ls dist/BentoVM-*-x86_64.AppImage 2>/dev/null | tail -1)
[ -n "$APPIMAGE" ] || { echo "No AppImage in dist/, run build.sh first"; exit 1; }
IMAGES=("$@")
[ ${#IMAGES[@]} -gt 0 ] || IMAGES=(fedora:latest ubuntu:24.04 ubuntu:26.04)

SOCK=bento-appimage-test
kwin_wayland --virtual --socket "$SOCK" --width 1280 --height 800 >/dev/null 2>&1 &
KWIN=$!
trap 'kill $KWIN 2>/dev/null || true' EXIT
for _ in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/$SOCK" ] && break; sleep 0.1; done

# Runs inside each container.
read -r -d '' INSIDE <<'EOF' || true
set -e
if command -v apt-get >/dev/null; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq >/dev/null
    apt-get install -y -qq --no-install-recommends libegl1 libgl1 libopengl0 libglx0 libgl1-mesa-dri \
        fontconfig fonts-noto-core libharfbuzz0b libx11-6 libx11-xcb1 libxcb1 libsm6 libice6 \
        libwayland-client0 libwayland-cursor0 libwayland-egl1 libdbus-1-3 libsystemd0 libudev1 \
        libgmp10 libcom-err2 libxkbcommon0 >/dev/null
    apt-get install -y -qq --no-install-recommends libglib2.0-0t64 >/dev/null 2>&1 \
        || apt-get install -y -qq --no-install-recommends libglib2.0-0 >/dev/null
elif command -v dnf >/dev/null; then
    dnf -q install -y mesa-libEGL mesa-dri-drivers libglvnd-opengl libglvnd-glx fontconfig harfbuzz \
        google-noto-sans-fonts libxkbcommon libX11-xcb \
        libSM libICE libwayland-client libwayland-cursor libwayland-egl glib2 dbus-libs systemd-libs >/dev/null
elif command -v pacman >/dev/null; then
    pacman -Syu --noconfirm --needed mesa libglvnd fontconfig harfbuzz noto-fonts libxkbcommon libx11 \
        libxcb libsm libice wayland glib2 >/dev/null
fi
. /etc/os-release
mkdir -p /root/.config /run/wl && chmod 700 /run/wl
printf '[KDE]\nwidgetStyle=Breeze\n[Icons]\nTheme=breeze\n' > /root/.config/kdeglobals
cd /tmp && /dist/$APPIMAGE_NAME --appimage-extract >/dev/null
export LANG=C.UTF-8 XDG_RUNTIME_DIR=/run/wl WAYLAND_DISPLAY=$SOCK XDG_CURRENT_DESKTOP=KDE \
       KDE_SESSION_VERSION=6 KDE_FULL_SESSION=true QT_FORCE_STDERR_LOGGING=1 \
       QT_LOGGING_RULES="qt.core.library=true;qt.qpa.*=false"
timeout 8 ./squashfs-root/AppRun >/tmp/run.log 2>&1 || true
check() {   # <label> <plugin file>
    if grep -q "$2\" loaded library" /tmp/run.log; then echo "  PASS  $1"; else echo "  FAIL  $1"; FAILED=1; fi
}
FAILED=0
echo "$PRETTY_NAME"
if ! grep -q "loaded library" /tmp/run.log; then
    echo "  FAIL  BentoVM didn't start:"
    grep -iE "error|cannot|not found|abort|fatal" /tmp/run.log | head -5 | sed 's/^/        /'
fi
check "Wayland platform plugin" libqwayland.so
check "Breeze style" breeze6.so
check "Plasma platform theme" KDEPlasmaPlatformTheme6.so
if grep -q "cannot load" /tmp/run.log; then
    FAILED=1
    grep "cannot load" /tmp/run.log | sed 's/^.*cannot load: /  FAIL  /' | sort -u
fi
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
