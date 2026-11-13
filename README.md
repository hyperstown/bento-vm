# Bento

A Qt front end for QEMU/KVM that treats **Venus** (Vulkan) and VirGL GPU
acceleration as first-class citizens, so GPU-accelerated VMs no longer need a
hand-written `qemu-system-x86_64` command line.

VMs run as **your user** (no root, no libvirt), each as an independent QEMU
process. Closing Bento, or Bento crashing, doesn't take your VMs down; Bento
reconnects to them on its next start. It looks like a regular KDE application
on Plasma and works on other desktops too.

## Features

- **Venus / VirGL / 2D graphics** per VM, shown in a built-in display (QEMU's
  D-Bus display with DMA-BUF frames imported through EGL into the window),
  or optionally in QEMU's own GTK/SDL window.
- **Storage locations**: one or more folders for VMs, one of them the default.
  VMs on a disconnected drive stay in the list as *Temporarily unavailable*
  and can't be booted until the drive is back.
- **Clipboard sharing** with a per-VM direction: both ways, host→guest,
  guest→host or off (switchable live from the VM window).
- **Drag & drop files** onto the VM screen (or onto its card) to copy them into
  the guest; they land in the guest user's Downloads folder.
- **Setup wizard**: pick an ISO (ISOs in `~/Downloads` and recently used ones
  are listed) or an existing disk image. The OS is detected from the ISO's
  volume label, with `osinfo-detect` as a fallback, and CPU (cores × threads)
  and RAM are recommended from the OS and your hardware. Everything stays adjustable.
- **Themes**: the native desktop style by default; *Modern Dark* and *Modern
  Light* give a self-contained look with rounded cards and a green gradient accent.
- UEFI/BIOS, Secure Boot, TPM 2.0 (with `swtpm`), port forwarding (SSH by
  default for Linux guests), PipeWire/PulseAudio sound, CD/ISO hot-swap,
  screenshots, "send key" combos, and the generated QEMU command line in VM settings.

## Requirements

Bento itself runs anywhere QEMU/KVM does; plain 2D and VirGL VMs work on any
recent distribution. **Venus** depends on fairly new host and guest software,
so on LTS distributions it may not be available yet.

### Host

| Component | Minimum | Notes |
|---|---|---|
| Linux kernel | any with KVM | **6.13+ for Venus**, 6.16+ in the Intel cases under [GPU notes](#gpu-notes) |
| QEMU | 9.2 | First release with Venus. Needs the D-Bus display (`ui-dbus`) and virtio-gpu GL modules. 11.0+ recommended |
| virglrenderer | 1.0.0 | Built with Venus enabled (OpenGL-only VirGL works from 0.8.2) |
| Vulkan driver | see [GPU notes](#gpu-notes) | Only needed for Venus |
| OVMF (edk2) | – | For UEFI guests (BIOS guests work without it) |
| libosinfo | optional | Better ISO detection (`osinfo-detect`) |
| swtpm | optional | TPM 2.0, e.g. for Windows 11 |

Your user needs read/write access to `/dev/kvm` (usually via the `kvm` group).

### Linux guests

| For | Needs |
|---|---|
| Venus (Vulkan) | Linux 5.16+ and Mesa 24.2+ with the Venus driver (Arch: `vulkan-virtio`; Fedora/Debian/Ubuntu: `mesa-vulkan-drivers`) |
| Clipboard and file drop | `spice-vdagent` |

Check Venus inside the guest with `vulkaninfo --summary`: the device should
read *Virtio-GPU Venus (…your host GPU…)*. If it only lists llvmpipe, see the
notes below and the VM's log (*⋮ menu on the VM card → Open Log*).

Windows guests have no Venus/VirGL driver and use the 2D display. Install the
virtio-win guest tools there for clipboard and file drop.

### GPU notes

Venus maps host GPU memory directly into the guest, so it depends on the host
GPU driver and KVM. Bento is developed and tested on AMD (RADV). The rest of
this list follows [Mesa's Venus documentation](https://docs.mesa3d.org/drivers/venus.html).

- **AMD and Intel (Mesa RADV / ANV 21.1+)**: host kernel 6.13+.
- **Intel CPU with a discrete GPU (AMD or NVIDIA), or Intel Meteor Lake / `xe`
  driver**: host kernel 6.16+ and QEMU 11.0+. KVM must honor the guest's
  memory attributes (`-accel kvm,honor-guest-pat=…`). Bento adds
  `honor-guest-pat=auto` to Venus VMs automatically on QEMU 11+; QEMU then
  enables it only if the kernel supports it. With an older QEMU on an Intel
  CPU, Bento shows a warning when the VM starts.
- **NVIDIA (proprietary driver)**:
  - Venus needs driver **570.86 or newer**. With older drivers, use VirGL or 2D.
  - On an Intel CPU, the point above applies too.
  - The built-in display and VirGL go through EGL/GBM, which needs NVIDIA's kernel
    modesetting: `cat /sys/module/nvidia_drm/parameters/modeset` should print
    `Y`. This is the default since driver 560; otherwise add `nvidia_drm.modeset=1`
    to the kernel command line.
  - If the built-in display stays black, switch the VM to *QEMU window (GTK)*
    in *Settings → Display*.
  - Nouveau/NVK isn't on Mesa's tested list for Venus.

Mesa notes that Venus relies on implementation-defined driver behavior, so it
isn't guaranteed to work on every driver that meets these minimums.

## Build & run

Build dependencies: CMake 3.21+, a C++20 compiler, Qt 6.7+ (Widgets, Network,
OpenGL, OpenGLWidgets, Concurrent), GLib/GIO, EGL headers and
spice-protocol 0.14+. For example, on Arch Linux:

```sh
sudo pacman -S --needed cmake ninja qt6-base glib2 libglvnd spice-protocol \
                        qemu-desktop virglrenderer edk2-ovmf
# optional: libosinfo swtpm
```

Other distributions have equivalent packages (usually the `-devel` / `-dev`
variants for building).

```sh
cmake -B build -G Ninja
cmake --build build
./build/bento-vm
```

To get a launcher entry and icon, install it:

```sh
cmake --install build --prefix ~/.local
```

Running straight from the build folder works too, but Qt then logs
`Failed to register with host portal … App info not found for 'bento-vm'`
because the desktop portal can't find `bento-vm.desktop`. It's harmless and
disappears once the desktop file is installed.

## Importing an existing VM

*New VM → Use an existing disk image* boots a disk you already have (qcow2,
raw, VMDK, VDI…), either in place or copied into the VM folder. Since Bento
doesn't run QEMU as root, the file must be readable and writable by your user
(images under `/var/lib/libvirt/images`, for example, may need
`sudo chown $USER: …`).

Imported disks default to **BIOS** firmware and the **SATA** bus, which is what
a plain `qemu-system-x86_64 -hda disk.qcow2` uses on q35, so existing installs
keep booting. You can switch to VirtIO later in *Settings → Storage* once the
guest's initramfs includes `virtio_blk`.

## Keys in the VM window

- **Ctrl+Alt+F**: toggle full screen
- **Ctrl+Alt+G**: release the keyboard back to the host
- *Keys* menu: Ctrl+Alt+Del, Ctrl+Alt+F1…F12, Print, Super

## Where things live

| | |
|---|---|
| Settings | `~/.config/bento-vm/bento-vm.conf` |
| Default VM location | `~/.local/share/bento-vm/machines/<name>/` (`vm.json`, `disk.qcow2`, `efivars.fd`) |
| Library cache (offline VMs) | `~/.local/share/bento-vm/library.json` |
| QEMU logs | `~/.local/state/bento-vm/logs/<vm-id>.log` |
| Sockets / pid files | `$XDG_RUNTIME_DIR/bento-vm/<vm-id>/` |

A VM folder is self-contained: copy it anywhere and add it back with
*File → Add Existing Bento VM Folder*.

## How it works

- `core/QemuCommand` turns a `vm.json` into a QEMU command line
  (`virtio-vga-gl,hostmem=…,blob=true,venus=true` with memfd-backed RAM).
- `core/Vm` launches QEMU detached and controls it over QMP (`core/QmpClient`).
- `display/DBusDisplay` passes QEMU one end of a socket pair via QMP
  `getfd` + `add_client` (`-display dbus,p2p=yes,gl=on`), registers a
  listener over GDBus and receives `ScanoutDMABUF`/`UpdateDMABUF`. Each update is
  acknowledged after drawing, which paces the guest.
- `display/DisplayWidget` imports the DMA-BUFs with `EGL_EXT_image_dma_buf_import`
  into a `QOpenGLWidget`, maps input (evdev → QEMU key codes) and asks the
  guest to follow the window size (`SetUIInfo`).
- `agent/VdAgent` speaks the SPICE agent protocol directly on the
  `com.redhat.spice.0` port: clipboard with direction control and file
  transfer, no SPICE server involved.

## Tests

```sh
cmake -B build -DBENTO_BUILD_TESTS=ON && cmake --build build
```

`display_test`, `ui_test` and `lifecycle_test` drive real QEMU instances. Run
them inside a headless compositor (e.g. `kwin_wayland --virtual --socket t`,
then `WAYLAND_DISPLAY=t`) with throwaway `XDG_*_HOME` directories.
`vdagent_test` talks to a real `spice-vdagentd` (e.g. in a container, with the
virtio port emulated by `socat PTY,… UNIX-LISTEN:…`).

## Known limitations

- On Wayland, compositor shortcuts (Meta, Alt+Tab) still go to the host; use
  the *Keys* menu.
- Folders can't be dropped yet (zip them first).
- In QEMU-window mode the clipboard is both-ways-or-off and file drop is unavailable.
