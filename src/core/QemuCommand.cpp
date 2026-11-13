// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "QemuCommand.h"
#include "AppSettings.h"
#include "HostInfo.h"
#include "VmConfig.h"

#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

// QEMU option values use ',' as separator; a literal comma is written as ",,".
static QString esc(QString v)
{
    return v.replace(QLatin1Char(','), QStringLiteral(",,"));
}

static QString onOff(bool b)
{
    return b ? QStringLiteral("on") : QStringLiteral("off");
}

QString QemuCommand::toShellString() const
{
    static const QRegularExpression safe(QStringLiteral(R"(^[A-Za-z0-9_@%+=:,./-]+$)"));
    auto quote = [](const QString &s) {
        if (!s.isEmpty() && safe.match(s).hasMatch())
            return s;
        QString q = s;
        q.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
        return QLatin1Char('\'') + q + QLatin1Char('\'');
    };
    QString out = quote(program);
    for (const QString &arg : args) {
        out += arg.startsWith(QLatin1Char('-')) ? QStringLiteral(" \\\n    ") : QStringLiteral(" ");
        out += quote(arg);
    }
    return out;
}

static QString audioBackend()
{
    const QString rt = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (QFileInfo::exists(rt + QStringLiteral("/pipewire-0")))
        return QStringLiteral("pipewire");
    if (QFileInfo::exists(rt + QStringLiteral("/pulse/native")))
        return QStringLiteral("pa");
    return {};
}

QemuCommand buildQemuCommand(const VmConfig &c, const QString &rt)
{
    const HostInfo &host = HostInfo::get();
    QemuCommand cmd;
    cmd.program = HostInfo::qemuBinary();
    QStringList &a = cmd.args;

    if (!host.kvmUsable) {
        cmd.error = host.kvmProblem;
        return cmd;
    }
    if (c.disks.isEmpty() && c.cdroms.join(QString()).isEmpty()) {
        cmd.error = QStringLiteral("This VM has neither a disk nor an installation image.");
        return cmd;
    }

    const bool uefi = c.firmware == QLatin1String("uefi");
    bool secure = uefi && c.secureBoot;
    if (uefi && host.ovmfCode.isEmpty() && host.ovmfCodeSecure.isEmpty()) {
        cmd.error = QStringLiteral("UEFI firmware (OVMF) was not found. Install the edk2-ovmf package or switch the VM to BIOS.");
        return cmd;
    }
    if (secure && host.ovmfCodeSecure.isEmpty()) {
        cmd.warnings << QStringLiteral("Secure Boot firmware not found; booting without Secure Boot.");
        secure = false;
    }
    const bool tpm = c.tpm && !host.swtpm.isEmpty();
    if (c.tpm && !tpm)
        cmd.warnings << QStringLiteral("swtpm is not installed; the VM will boot without a TPM.");

    a << QStringLiteral("-name") << QStringLiteral("guest=%1,debug-threads=on").arg(esc(c.name));
    a << QStringLiteral("-uuid") << c.id;
    a << QStringLiteral("-msg") << QStringLiteral("timestamp=on");
    a << QStringLiteral("-nodefaults") << QStringLiteral("-no-user-config");

    QString machine = QStringLiteral("q35,memory-backend=mem0,vmport=off");
    if (secure)
        machine += QStringLiteral(",smm=on");
    a << QStringLiteral("-machine") << machine;

    // Venus maps host GPU memory straight into the guest. On Intel CPUs KVM
    // ignores the guest's page attributes by default, which breaks those
    // mappings (dGPUs incl. NVIDIA, Meteor Lake/xe). QEMU 11 can turn that
    // off; "auto" only does so when the host kernel (6.16+) supports it.
    QString accel = QStringLiteral("kvm");
    if (c.graphics == QLatin1String("venus")) {
        const QVersionNumber qv = HostInfo::qemuVersion();
        if (qv.isNull() || qv >= QVersionNumber(11, 0))
            accel += QStringLiteral(",honor-guest-pat=auto");
        else if (host.intel)
            cmd.warnings << QStringLiteral("Venus on Intel CPUs needs QEMU 11.0+ and Linux 6.16+ for GPU memory "
                                           "mappings to work reliably (this QEMU is %1).").arg(qv.toString());
    }
    a << QStringLiteral("-accel") << accel;
    a << QStringLiteral("-global") << QStringLiteral("ICH9-LPC.disable_s3=1");
    a << QStringLiteral("-global") << QStringLiteral("ICH9-LPC.disable_s4=1");

    QString cpu = QStringLiteral("host");
    if (host.amd && c.threads > 1)
        cpu += QStringLiteral(",+topoext");
    if (c.hyperv)
        cpu += QStringLiteral(",hv-relaxed,hv-vapic,hv-spinlocks=0x1fff,hv-vpindex,hv-runtime,hv-time,"
                              "hv-synic,hv-stimer,hv-frequencies,hv-tlbflush,hv-ipi");
    a << QStringLiteral("-cpu") << cpu;
    a << QStringLiteral("-smp") << QStringLiteral("%1,sockets=1,cores=%2,threads=%3")
                                       .arg(c.vcpus()).arg(c.cores).arg(c.threads);

    // memfd-backed RAM is required for blob resources (udmabuf) used by Venus.
    a << QStringLiteral("-m") << QStringLiteral("%1M").arg(c.memoryMiB);
    a << QStringLiteral("-object") << QStringLiteral("memory-backend-memfd,id=mem0,size=%1M,share=on").arg(c.memoryMiB);

    a << QStringLiteral("-rtc") << (c.localtime ? QStringLiteral("base=localtime") : QStringLiteral("base=utc"));

    if (uefi) {
        const QString code = secure ? host.ovmfCodeSecure : (host.ovmfCode.isEmpty() ? host.ovmfCodeSecure : host.ovmfCode);
        if (secure)
            a << QStringLiteral("-global") << QStringLiteral("driver=cfi.pflash01,property=secure,value=on");
        a << QStringLiteral("-drive") << QStringLiteral("if=pflash,format=raw,unit=0,readonly=on,file=%1").arg(esc(code));
        a << QStringLiteral("-drive") << QStringLiteral("if=pflash,format=raw,unit=1,file=%1")
                                             .arg(esc(QemuRuntime::efiVarsFile(c.dir)));
    }

    // Graphics
    const bool gl = c.usesGl();
    if (c.graphics == QLatin1String("venus")) {
        a << QStringLiteral("-device")
          << QStringLiteral("virtio-vga-gl,hostmem=%1M,blob=true,venus=true").arg(qMax(256, c.hostmemMiB));
    } else if (c.graphics == QLatin1String("virgl")) {
        a << QStringLiteral("-device") << QStringLiteral("virtio-vga-gl");
    } else {
        a << QStringLiteral("-device") << QStringLiteral("virtio-vga");
    }

    if (c.display == QLatin1String("gtk")) {
        a << QStringLiteral("-display") << QStringLiteral("gtk,gl=%1,show-cursor=on,zoom-to-fit=on").arg(onOff(gl));
    } else if (c.display == QLatin1String("sdl")) {
        a << QStringLiteral("-display") << QStringLiteral("sdl,gl=%1").arg(onOff(gl));
    } else {
        a << QStringLiteral("-display") << QStringLiteral("dbus,p2p=yes,gl=%1").arg(onOff(gl));
    }

    // Input: absolute pointer so the host cursor maps 1:1 into the guest.
    // 8 USB2 + 8 USB3 ports leave room for hot-plugged host devices.
    a << QStringLiteral("-device") << QStringLiteral("qemu-xhci,id=xhci,p2=8,p3=8");
    a << QStringLiteral("-device") << QStringLiteral("usb-tablet,bus=xhci.0");
    // Host devices to grab whenever they're plugged in (QEMU waits for them).
    for (int i = 0; i < c.usbDevices.size(); ++i) {
        const UsbAutoConnect &u = c.usbDevices[i];
        a << QStringLiteral("-device") << QStringLiteral("usb-host,bus=xhci.0,vendorid=0x%1,productid=0x%2,id=usbauto%3")
                                              .arg(u.vendorId, u.productId).arg(i);
    }

    // Disks
    int sataPort = 0;
    for (int i = 0; i < c.disks.size(); ++i) {
        const DiskSpec &d = c.disks[i];
        const QString path = c.diskPath(i);
        if (!QFileInfo::exists(path)) {
            cmd.error = QStringLiteral("Disk image not found: %1").arg(path);
            return cmd;
        }
        const QString id = QStringLiteral("disk%1").arg(i);
        a << QStringLiteral("-drive")
          << QStringLiteral("file=%1,if=none,id=%2,format=%3,discard=unmap,detect-zeroes=unmap")
                 .arg(esc(path), id, d.format.isEmpty() ? QStringLiteral("qcow2") : d.format);
        const int boot = i + 1;
        if (d.bus == QLatin1String("sata")) {
            a << QStringLiteral("-device") << QStringLiteral("ide-hd,drive=%1,bus=ide.%2,bootindex=%3").arg(id).arg(sataPort++).arg(boot);
        } else if (d.bus == QLatin1String("nvme")) {
            a << QStringLiteral("-device") << QStringLiteral("nvme,drive=%1,serial=bento%2,bootindex=%3").arg(id).arg(i).arg(boot);
        } else {
            a << QStringLiteral("-device") << QStringLiteral("virtio-blk-pci,drive=%1,bootindex=%2").arg(id).arg(boot);
        }
    }

    // CD-ROM drives live on the last AHCI ports. One drive always exists so
    // media can be inserted at runtime.
    QStringList cds = c.cdroms;
    if (cds.isEmpty())
        cds << QString();
    for (int i = 0; i < qMin(2, int(cds.size())); ++i) {
        QString iso = cds[i];
        if (!iso.isEmpty() && !QFileInfo::exists(iso)) {
            cmd.warnings << QStringLiteral("Installation image not found, the drive will be empty: %1").arg(iso);
            iso.clear();
        }
        const QString drive = QStringLiteral("cd%1").arg(i);
        if (iso.isEmpty())
            a << QStringLiteral("-drive") << QStringLiteral("if=none,id=%1,media=cdrom,readonly=on").arg(drive);
        else
            a << QStringLiteral("-drive") << QStringLiteral("file=%1,if=none,id=%2,media=cdrom,readonly=on,format=raw").arg(esc(iso), drive);
        a << QStringLiteral("-device") << QStringLiteral("ide-cd,drive=%1,bus=ide.%2,id=%3,bootindex=%4")
                                              .arg(drive).arg(4 + i).arg(QLatin1String(QemuRuntime::cdromDeviceId(i))).arg(10 + i);
    }

    // Network (user-mode NAT, no root needed)
    if (c.network) {
        QString netdev = QStringLiteral("user,id=net0");
        for (const PortForward &f : c.portForwards) {
            netdev += QStringLiteral(",hostfwd=%1:%2:%3-:%4")
                          .arg(f.proto, f.hostAddress).arg(f.hostPort).arg(f.guestPort);
        }
        a << QStringLiteral("-netdev") << netdev;
        a << QStringLiteral("-device") << QStringLiteral("%1,netdev=net0").arg(c.nicModel);
    }

    // Audio
    if (c.audio) {
        const QString be = audioBackend();
        if (be.isEmpty()) {
            cmd.warnings << QStringLiteral("No PipeWire/PulseAudio socket found; audio disabled.");
        } else {
            a << QStringLiteral("-audiodev") << QStringLiteral("%1,id=snd0").arg(be);
            a << QStringLiteral("-device") << QStringLiteral("ich9-intel-hda");
            a << QStringLiteral("-device") << QStringLiteral("hda-output,audiodev=snd0");
        }
    }

    // Guest integration channels.
    a << QStringLiteral("-device") << QStringLiteral("virtio-serial-pci,id=vser0");
    if (c.display == QLatin1String("embedded")) {
        // Our own SPICE agent host side: clipboard (with direction control) + file transfer.
        a << QStringLiteral("-chardev") << QStringLiteral("socket,id=vdagent,path=%1,server=on,wait=off")
                                               .arg(esc(QemuRuntime::agentSocket(rt)));
        a << QStringLiteral("-device") << QStringLiteral("virtserialport,bus=vser0.0,chardev=vdagent,name=com.redhat.spice.0");
    } else if (c.clipboard != QLatin1String("off")) {
        // External QEMU window: let QEMU's built-in agent sync the clipboard (both ways).
        a << QStringLiteral("-chardev") << QStringLiteral("qemu-vdagent,id=vdagent,name=vdagent,clipboard=on");
        a << QStringLiteral("-device") << QStringLiteral("virtserialport,bus=vser0.0,chardev=vdagent,name=com.redhat.spice.0");
    }
    a << QStringLiteral("-chardev") << QStringLiteral("socket,id=qga,path=%1,server=on,wait=off")
                                           .arg(esc(QemuRuntime::guestAgentSocket(rt)));
    a << QStringLiteral("-device") << QStringLiteral("virtserialport,bus=vser0.0,chardev=qga,name=org.qemu.guest_agent.0");

    a << QStringLiteral("-device") << QStringLiteral("virtio-rng-pci");

    if (tpm) {
        a << QStringLiteral("-chardev") << QStringLiteral("socket,id=chrtpm,path=%1").arg(esc(QemuRuntime::swtpmSocket(rt)));
        a << QStringLiteral("-tpmdev") << QStringLiteral("emulator,id=tpm0,chardev=chrtpm");
        a << QStringLiteral("-device") << QStringLiteral("tpm-crb,tpmdev=tpm0");
    }

    // Control channel
    a << QStringLiteral("-qmp") << QStringLiteral("unix:%1,server=on,wait=off").arg(esc(QemuRuntime::qmpSocket(rt)));
    a << QStringLiteral("-pidfile") << QemuRuntime::pidFile(rt);

    const QString globalExtra = AppSettings::instance().extraQemuArgs().trimmed();
    if (!globalExtra.isEmpty())
        a << QProcess::splitCommand(globalExtra);
    if (!c.extraArgs.trimmed().isEmpty())
        a << QProcess::splitCommand(c.extraArgs);

    return cmd;
}
