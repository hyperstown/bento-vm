#!/usr/bin/env bash
# Builds dist/BentoVM-<version>-x86_64.AppImage in a throwaway Ubuntu 24.04
# container, so nothing needs to be installed on the host besides podman.
set -euo pipefail
cd "$(dirname "$0")/../.."
podman run --rm -v "$PWD:/src:Z" -e QT_VERSION \
    docker.io/library/ubuntu:24.04 bash /src/packaging/appimage/build-appimage.sh
