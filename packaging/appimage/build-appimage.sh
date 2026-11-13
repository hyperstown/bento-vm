#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 hyperstown
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds the BentoVM AppImage. Runs inside an Ubuntu 24.04 container (see
# build.sh) so the result works on distributions with glibc >= 2.39.
set -euo pipefail

SRC=${SRC:-/src}
OUT=${OUT:-$SRC/dist}
QT_VERSION=${QT_VERSION:-6.8.3}
WORK=/tmp/appimage
export DEBIAN_FRONTEND=noninteractive

echo "==> Installing build dependencies"
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    build-essential cmake ninja-build pkg-config ca-certificates wget file python3-venv \
    libglib2.0-dev libegl-dev libgl-dev libopengl-dev libspice-protocol-dev \
    libxkbcommon-dev libxkbcommon-x11-0 libfontconfig1 libfreetype6 libdbus-1-3 \
    libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
    libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libxcb-xinput0 libx11-xcb1 \
    libwayland-client0 libwayland-cursor0 libwayland-egl1 libvulkan1 >/dev/null

echo "==> Installing Qt $QT_VERSION"
python3 -m venv /opt/aqt
/opt/aqt/bin/pip install -q aqtinstall
/opt/aqt/bin/aqt install-qt -O /opt/qt linux desktop "$QT_VERSION" linux_gcc_64 >/dev/null
QTDIR=/opt/qt/$QT_VERSION/gcc_64

echo "==> Building BentoVM"
rm -rf "$WORK"
mkdir -p "$WORK"
cmake -S "$SRC" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$QTDIR" -DCMAKE_INSTALL_PREFIX=/usr >/dev/null
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
export QMAKE="$QTDIR/bin/qmake"
export LD_LIBRARY_PATH="$QTDIR/lib"
export EXTRA_QT_MODULES="svg"              # SVG icons (icon themes, app icon)
export EXTRA_QT_PLUGINS="svg"
EXTRA_PLATFORM_PLUGINS=$(cd "$QTDIR/plugins/platforms" && ls libqwayland*.so | paste -sd ';')
export EXTRA_PLATFORM_PLUGINS
export LDAI_OUTPUT="BentoVM-$VERSION-x86_64.AppImage"
export OUTPUT="$LDAI_OUTPUT"

# EGL/GL, Wayland and GLib must come from the host: Mesa's EGL talks to the
# compositor through the host's libwayland, and GLib is ABI-stable everywhere.
# The Qt plugin ignores --exclude-library, so deploy first, prune, then pack.
EXCLUDES=(--exclude-library 'libwayland-*.so*'
          --exclude-library 'libglib-2.0.so*' --exclude-library 'libgio-2.0.so*'
          --exclude-library 'libgobject-2.0.so*' --exclude-library 'libgmodule-2.0.so*'
          --exclude-library 'libgthread-2.0.so*')
./linuxdeploy-x86_64.AppImage --appdir AppDir --plugin qt "${EXCLUDES[@]}" \
    --desktop-file "AppDir/usr/share/applications/$APP_ID.desktop" \
    --icon-file "AppDir/usr/share/icons/hicolor/scalable/apps/$APP_ID.svg"
# linuxdeploy-plugin-qt skips the Wayland client-buffer plugins; without
# wayland-egl Qt can't create OpenGL contexts on Wayland.
for dir in wayland-graphics-integration-client; do
    mkdir -p "AppDir/usr/plugins/$dir"
    cp "$QTDIR/plugins/$dir/"*.so "AppDir/usr/plugins/$dir/"
done
# Notices for the bundled Qt/ICU (system libraries' copyright files are
# copied by linuxdeploy already).
mkdir -p AppDir/usr/share/doc/qt6
cp "$SRC"/packaging/appimage/licenses/* AppDir/usr/share/doc/qt6/
rm -f AppDir/usr/lib/libwayland-*.so* AppDir/usr/lib/libglib-2.0.so* AppDir/usr/lib/libgio-2.0.so* \
      AppDir/usr/lib/libgobject-2.0.so* AppDir/usr/lib/libgmodule-2.0.so* AppDir/usr/lib/libgthread-2.0.so*
./linuxdeploy-x86_64.AppImage --appdir AppDir --output appimage "${EXCLUDES[@]}"

mkdir -p "$OUT"
mv "$LDAI_OUTPUT" "$OUT/"
echo "==> $OUT/$LDAI_OUTPUT"
