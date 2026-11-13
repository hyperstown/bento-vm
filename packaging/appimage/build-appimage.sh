#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 hyperstown
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds the BentoVM AppImage. Runs inside an Ubuntu 24.04 container (see
# build.sh) so the result works on distributions with glibc >= 2.39.
set -euo pipefail

SRC=${SRC:-/src}
OUT=${OUT:-$SRC/dist}
WORK=/tmp/appimage
export DEBIAN_FRONTEND=noninteractive

# Qt, KDE Frameworks and Plasma's Breeze style + platform integration come
# from KDE neon (built for Ubuntu 24.04), so the AppImage looks native on KDE
# while keeping the glibc 2.39 baseline.
echo "==> Installing build dependencies"
apt-get update -qq
apt-get install -y -qq --no-install-recommends curl gnupg ca-certificates >/dev/null
curl -fsSL https://archive.neon.kde.org/public.key | gpg --batch --yes --dearmor -o /usr/share/keyrings/neon.gpg
echo "deb [signed-by=/usr/share/keyrings/neon.gpg] http://archive.neon.kde.org/user noble main" \
    > /etc/apt/sources.list.d/neon.list
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    build-essential cmake ninja-build pkg-config wget file \
    qt6-base-dev qt6-wayland qt6-svg kde-style-breeze plasma-integration \
    libglib2.0-dev libegl-dev libgl-dev libopengl-dev libspice-protocol-dev libxkbcommon-dev >/dev/null

QMAKE=/usr/lib/qt6/bin/qmake
QT_PLUGINS=$($QMAKE -query QT_INSTALL_PLUGINS)

echo "==> Building BentoVM"
rm -rf "$WORK"
mkdir -p "$WORK"
cmake -S "$SRC" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr >/dev/null
cmake --build "$WORK/build"
DESTDIR="$WORK/AppDir" cmake --install "$WORK/build" >/dev/null

echo "==> Packaging"
cd "$WORK"
wget -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
wget -q https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x linuxdeploy*.AppImage

VERSION=$(sed -n 's/^project(bento-vm VERSION \([0-9.]*\).*/\1/p' "$SRC/CMakeLists.txt")
APP_ID=io.github.hyperstown.BentoVM
export APPIMAGE_EXTRACT_AND_RUN=1          # no FUSE inside containers
export QMAKE
export EXTRA_QT_MODULES="svg"              # SVG icons (icon themes, app icon)
export EXTRA_QT_PLUGINS="svg"
EXTRA_PLATFORM_PLUGINS=$(cd "$QT_PLUGINS/platforms" && ls libqwayland*.so | paste -sd ';')
export EXTRA_PLATFORM_PLUGINS
export LDAI_OUTPUT="BentoVM-$VERSION-x86_64.AppImage"
export OUTPUT="$LDAI_OUTPUT"

# Libraries that must come from the host. EGL/GL are excluded by linuxdeploy
# already; Mesa's EGL talks to the compositor through the host's libwayland.
# The rest are core system libraries every glibc 2.39+ distribution has, with
# stable ABIs. Bundling older copies of them breaks newer host libraries that
# get loaded too (GLib, Mesa's drivers, …). The Qt plugin ignores
# --exclude-library, so deploy first, prune, then pack.
HOST_LIBS=('libwayland-*' libglib-2.0 libgio-2.0 libgobject-2.0 libgmodule-2.0 libgthread-2.0
           libmount libblkid libselinux libpcre2-8 libffi libsystemd libudev libdbus-1
           libgcrypt libgpg-error libcap liblzma libzstd liblz4 libbz2 libz libexpat libuuid)
EXCLUDES=()
for lib in "${HOST_LIBS[@]}"; do EXCLUDES+=(--exclude-library "$lib.so*"); done
./linuxdeploy-x86_64.AppImage --appdir AppDir --plugin qt "${EXCLUDES[@]}" \
    --desktop-file "AppDir/usr/share/applications/$APP_ID.desktop" \
    --icon-file "AppDir/usr/share/icons/hicolor/scalable/apps/$APP_ID.svg"

# Plugins linuxdeploy-plugin-qt doesn't know about:
#  * Wayland client-buffer integrations (without wayland-egl no OpenGL on Wayland)
#  * Breeze style + Plasma platform theme and the KF6 plugins they load, so the
#    "System" look matches a native KDE app (fonts, colours, dialogs).
DEPS_ONLY=()
copy_plugins() {   # <dir relative to the plugin root> <file glob>
    mkdir -p "AppDir/usr/plugins/$1"
    cp $QT_PLUGINS/$1/$2 "AppDir/usr/plugins/$1/"
    # linuxdeploy only scans what it deployed itself; have it pull in
    # (e.g. KDE Frameworks) dependencies and fix the RUNPATH of these too.
    DEPS_ONLY+=(--deploy-deps-only "AppDir/usr/plugins/$1")
}
copy_plugins wayland-graphics-integration-client '*.so'
copy_plugins styles breeze6.so
copy_plugins platformthemes KDEPlasmaPlatformTheme6.so
copy_plugins kf6 FrameworkIntegrationPlugin.so
copy_plugins kf6/kwindowsystem '*.so'
copy_plugins kf6/kio kio_file.so

# Notices for the bundled Qt/KDE libraries (system packages' copyright
# files are copied by linuxdeploy already).
mkdir -p AppDir/usr/share/doc/bento-vm
cp "$SRC"/packaging/appimage/licenses/* AppDir/usr/share/doc/bento-vm/
for lib in "${HOST_LIBS[@]}"; do rm -f AppDir/usr/lib/$lib.so*; done
./linuxdeploy-x86_64.AppImage --appdir AppDir --output appimage "${DEPS_ONLY[@]}" "${EXCLUDES[@]}"

mkdir -p "$OUT"
mv "$LDAI_OUTPUT" "$OUT/"
echo "==> $OUT/$LDAI_OUTPUT"
