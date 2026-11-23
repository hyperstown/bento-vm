#!/bin/bash
# SPDX-FileCopyrightText: 2026 hyperstown
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Builds the tiny Linux guest sharedfolder_test boots: Debian's kernel (virtio-fs
# built in) and an initramfs whose init mounts the shared folder, copies
# from-host.txt to from-guest.txt as user 1000, reports on the serial console
# and powers off. Everything happens in a container; nothing is installed on
# the host.
#
#   tests/virtiofs-guest.sh <out-dir>    # writes <out-dir>/vmlinuz and <out-dir>/initrd.img
set -euo pipefail

OUT=$(realpath -m "${1:?usage: $0 <out-dir>}")
mkdir -p "$OUT"

cat > "$OUT/init" <<'EOF'
#!/bin/busybox sh
/bin/busybox --install -s /bin
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev
say() { echo "bento-test: $*"; }
if mount -t virtiofs shared /mnt; then
    say "mounted"
    say "owner $(stat -c %u:%g /mnt/from-host.txt)"
    if su user -s /bin/sh -c 'cat /mnt/from-host.txt > /mnt/from-guest.txt'; then
        say "write ok"
    else
        say "write failed"
    fi
else
    say "mount failed"
fi
say "done"
poweroff -f
EOF

podman run --rm --arch amd64 -e DEBIAN_FRONTEND=noninteractive -v "$OUT:/out:Z" docker.io/library/debian:trixie bash -euc '
    apt-get update -qq >/dev/null
    apt-get install -y -qq --no-install-recommends linux-image-amd64 busybox-static cpio >/dev/null
    cp /boot/vmlinuz-* /out/vmlinuz
    root=$(mktemp -d) && chmod 755 "$root"
    mkdir -p "$root"/{bin,dev,etc,mnt,proc,sys}
    cp /bin/busybox "$root/bin/"
    install -m 755 /out/init "$root/init"
    printf "root:x:0:0::/:/bin/sh\nuser:x:1000:1000::/:/bin/sh\n" > "$root/etc/passwd"
    printf "root:x:0:\nuser:x:1000:\n" > "$root/etc/group"
    (cd "$root" && find . | cpio -o -H newc --quiet | gzip -9) > /out/initrd.img
    chown "$(stat -c %u:%g /out)" /out/vmlinuz /out/initrd.img
'
rm -f "$OUT/init"
echo "$OUT/vmlinuz"
echo "$OUT/initrd.img"
