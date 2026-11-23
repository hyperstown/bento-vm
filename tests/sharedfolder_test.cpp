// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

// Shared folders (virtio-fs). Needs KVM and virtiofsd; runs headless and uses
// its own throwaway XDG directories.
//
//   sharedfolder_test                          settings, command line, virtiofsd lifecycle,
//                                              sandbox fallback, older virtiofsd
//   sharedfolder_test <vmlinuz> <initrd.img>   …plus a guest that uses the folder
//                                              (build it with tests/virtiofs-guest.sh)
#include "core/HostInfo.h"
#include "core/Paths.h"
#include "core/QemuCommand.h"
#include "core/SharedFolder.h"
#include "core/Vm.h"
#include "core/VmConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>

#include <cstdio>
#include <functional>
#include <unistd.h>

static int g_failures = 0;

static void check(bool ok, const char *what, const QString &detail = {})
{
    std::printf("%s %s%s\n", ok ? "ok  " : "FAIL", what, ok || detail.isEmpty() ? "" : qPrintable(QStringLiteral(": ") + detail));
    std::fflush(stdout);
    if (!ok)
        ++g_failures;
}

static bool waitFor(const std::function<bool()> &cond, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (t.elapsed() > ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
    return true;
}

static QString readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

static bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

static qint64 virtiofsdPid(const Vm &vm)
{
    return readAll(QemuRuntime::virtiofsdPidFile(vm.runtimeDir())).trimmed().toLongLong();
}

static bool processRunning(qint64 pid)
{
    const QString stat = readAll(QStringLiteral("/proc/%1/stat").arg(pid));
    return pid > 0 && !stat.isEmpty() && stat.mid(stat.lastIndexOf(QLatin1Char(')')) + 2, 1) != QLatin1String("Z");
}

// A VM with a tiny raw disk (nothing bootable) and the shared folder `share`.
static VmConfig makeConfig(const QString &base, const QString &name, const QString &share)
{
    VmConfig c;
    c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.name = name;
    c.dir = base + QStringLiteral("/vms/") + name;
    QDir().mkpath(c.dir);
    QFile disk(c.dir + QStringLiteral("/disk.img"));
    if (disk.open(QIODevice::WriteOnly))
        disk.resize(1 << 20);
    c.disks << DiskSpec{QStringLiteral("disk.img"), QStringLiteral("raw"), QStringLiteral("virtio")};
    c.firmware = QStringLiteral("bios");
    c.graphics = QStringLiteral("2d");
    c.memoryMiB = 512;
    c.cores = 1;
    c.network = false;
    c.audio = false;
    c.sharedFolder = share;
    QString err;
    c.save(&err);
    return c;
}

static bool stopVm(Vm &vm)
{
    if (vm.isActive())
        vm.forceStop();
    return waitFor([&] { return vm.state() == Vm::State::Stopped; }, 15000);
}

// --- child modes: run with a fake virtiofsd first on PATH ------------------

// The fake refuses --sandbox namespace, like a system that restricts user namespaces.
static int fallbackChild(const QString &base)
{
    const QString share = base + QStringLiteral("/share-fallback");
    QDir().mkpath(share);
    Vm vm(makeConfig(base, QStringLiteral("fallback"), share));
    QString err;
    check(vm.start(&err), "fallback: VM starts when virtiofsd can't create its sandbox", err);
    check(waitFor([&] { return vm.state() == Vm::State::Running; }, 30000), "fallback: VM is running");
    check(readAll(vm.logFile()).contains(QLatin1String("note: virtiofsd couldn't set up its sandbox")),
          "fallback: the VM log says it runs without the sandbox");
    check(readAll(Paths::virtiofsdLogFile(vm.id())).contains(QLatin1String("--sandbox none")),
          "fallback: second attempt used --sandbox none");
    check(stopVm(vm), "fallback: VM stops");
    return g_failures;
}

// The fake hides --readonly and --translate-uid, like virtiofsd 1.10.
static int oldChild(const QString &base)
{
    check(SharedFolder::support().installed && !SharedFolder::support().readOnly
              && !SharedFolder::support().translateIds,
          "old virtiofsd: features detected from --help");
    const QString share = base + QStringLiteral("/share-old");
    QDir().mkpath(share);
    VmConfig c = makeConfig(base, QStringLiteral("old"), share);
    c.sharedFolderReadOnly = true;
    const QemuCommand cmd = buildQemuCommand(c, Paths::vmRuntimeDir(c.id));
    check(cmd.sharedFolder.isEmpty() && !cmd.args.join(QLatin1Char(' ')).contains(QLatin1String("vhost-user-fs")),
          "old virtiofsd: a read-only share is left out rather than shared writable");
    check(cmd.warnings.join(QLatin1Char(' ')).contains(QLatin1String("can't share folders read-only")),
          "old virtiofsd: warning explains why", cmd.warnings.join(QLatin1Char(' ')));
    c.sharedFolderReadOnly = false;
    check(!buildQemuCommand(c, Paths::vmRuntimeDir(c.id)).sharedFolder.isEmpty(),
          "old virtiofsd: a writable share still works");
    return g_failures;
}

static int runChild(const QString &self, const QString &mode, const QString &base)
{
    const QString fakeDir = base + QStringLiteral("/fake-") + mode;
    QDir().mkpath(fakeDir);
    const QString real = HostInfo::get().virtiofsd;
    const QByteArray script = mode == QLatin1String("fallback")
        ? QByteArray("#!/bin/sh\nfor a in \"$@\"; do [ \"$a\" = namespace ] && "
                     "{ echo 'Error entering sandbox: Operation not permitted (test)' >&2; exit 1; }; done\n"
                     "exec \"$REAL_VIRTIOFSD\" \"$@\"\n")
        : QByteArray("#!/bin/sh\nif [ \"$1\" = --help ]; then \"$REAL_VIRTIOFSD\" --help | "
                     "grep -v -e --readonly -e --translate-; exit 0; fi\nexec \"$REAL_VIRTIOFSD\" \"$@\"\n");
    writeFile(fakeDir + QStringLiteral("/virtiofsd"), script);
    QFile::setPermissions(fakeDir + QStringLiteral("/virtiofsd"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PATH"), fakeDir + QLatin1Char(':') + env.value(QStringLiteral("PATH")));
    env.insert(QStringLiteral("REAL_VIRTIOFSD"), real);
    p.setProcessEnvironment(env);
    p.setProcessChannelMode(QProcess::ForwardedChannels);
    p.start(self, {QStringLiteral("--child"), mode, base});
    if (!p.waitForFinished(120000)) {
        p.kill();
        p.waitForFinished();
        check(false, "child test finished in time", mode);
        return 1;
    }
    return p.exitCode();
}

// --- main ------------------------------------------------------------------

int main(int argc, char **argv)
{
    // Headless, and never the user's real settings, VMs or runtime sockets.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    const bool child = argc >= 4 && qstrcmp(argv[1], "--child") == 0;
    // Short names: socket paths in the runtime folder must stay under 108 bytes.
    QTemporaryDir tmp(QDir::tempPath() + QStringLiteral("/bento-XXXXXX"));
    const QString base = child ? QString::fromLocal8Bit(argv[3]) : tmp.path();
    if (!child) {
        const std::pair<const char *, const char *> xdg[] = {{"XDG_CONFIG_HOME", "config"}, {"XDG_DATA_HOME", "data"},
                                                            {"XDG_STATE_HOME", "state"}, {"XDG_CACHE_HOME", "cache"},
                                                            {"XDG_RUNTIME_DIR", "run"}};
        for (const auto &[v, name] : xdg) {
            const QString dir = base + QLatin1Char('/') + QString::fromLatin1(name);
            QDir().mkpath(dir);
            QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
            qputenv(v, QFile::encodeName(dir));
        }
    }
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("bento-vm"));

    if (child) {
        const QString mode = QString::fromLocal8Bit(argv[2]);
        return mode == QLatin1String("fallback") ? fallbackChild(base) : oldChild(base);
    }

    if (HostInfo::get().virtiofsd.isEmpty() || !HostInfo::get().kvmUsable) {
        std::printf("SKIP: needs virtiofsd and KVM\n");
        return 0;
    }
    std::printf("virtiofsd: %s (read-only: %s, id mapping: %s)\n", qPrintable(HostInfo::get().virtiofsd),
                SharedFolder::support().readOnly ? "yes" : "no", SharedFolder::support().translateIds ? "yes" : "no");

    // Settings round trip; older vm.json files have no shared folder.
    {
        VmConfig c;
        c.id = QStringLiteral("x");
        c.sharedFolder = QStringLiteral("/srv/share");
        c.sharedFolderReadOnly = true;
        const VmConfig back = VmConfig::fromJson(c.toJson());
        check(back.sharedFolder == c.sharedFolder && back.sharedFolderReadOnly, "settings round trip");
        QJsonObject old = c.toJson();
        old.remove(QStringLiteral("sharedFolder"));
        check(VmConfig::fromJson(old).sharedFolder.isEmpty(), "vm.json without a shared folder");
    }

    // Folders that would hand the guest the home folder.
    check(SharedFolder::exposesHome(QDir::homePath()) && SharedFolder::exposesHome(QStringLiteral("/"))
              && !SharedFolder::exposesHome(QDir::homePath() + QStringLiteral("/Shared"))
              && !SharedFolder::exposesHome(QString()),
          "home folder detection");

    // Command line.
    const QString share = base + QStringLiteral("/share");
    QDir().mkpath(share);
    {
        VmConfig c = makeConfig(base, QStringLiteral("cmd"), share);
        QemuCommand cmd = buildQemuCommand(c, Paths::vmRuntimeDir(c.id));
        const QString args = cmd.args.join(QLatin1Char(' '));
        check(cmd.error.isEmpty() && cmd.sharedFolder == share, "command shares the folder", cmd.error);
        check(args.contains(QStringLiteral("vhost-user-fs-pci,id=sharedfolder,chardev=virtiofs0,tag=shared"))
                  && args.contains(QemuRuntime::virtiofsSocket(Paths::vmRuntimeDir(c.id))),
              "command has the virtio-fs device", args);
        c.sharedFolder = base + QStringLiteral("/missing");
        cmd = buildQemuCommand(c, Paths::vmRuntimeDir(c.id));
        check(cmd.sharedFolder.isEmpty() && !cmd.args.join(QLatin1Char(' ')).contains(QLatin1String("vhost-user-fs"))
                  && cmd.warnings.join(QLatin1Char(' ')).contains(QLatin1String("wasn't found")),
              "missing folder: warning, VM boots without it");
        c.sharedFolder = QStringLiteral("share");   // relative paths are inside the VM folder
        check(buildQemuCommand(c, Paths::vmRuntimeDir(c.id)).warnings.join(QLatin1Char(' ')).contains(c.dir + QStringLiteral("/share")),
              "relative folder is looked up in the VM folder");
    }

    // virtiofsd starts with the VM and is gone once it stops.
    {
        Vm vm(makeConfig(base, QStringLiteral("lifecycle"), share));
        QString err;
        check(vm.start(&err), "VM with a shared folder starts", err);
        const qint64 pid = virtiofsdPid(vm);
        check(processRunning(pid), "virtiofsd is running");
        check(waitFor([&] { return vm.state() == Vm::State::Running; }, 30000), "VM is running");
        check(waitFor([&] { return readAll(Paths::virtiofsdLogFile(vm.id())).contains(QLatin1String("Client connected")); }, 5000),
              "QEMU connected to virtiofsd");
        check(readAll(vm.logFile()).contains(QLatin1String("note: shared folder: ") + share), "VM log names the folder");
        check(stopVm(vm), "VM stops");
        check(waitFor([&] { return !processRunning(pid); }, 5000), "virtiofsd exited with QEMU");
        check(!QFileInfo::exists(vm.runtimeDir()), "runtime folder removed");
    }

    // QEMU fails to start: virtiofsd must not be left behind.
    {
        VmConfig c = makeConfig(base, QStringLiteral("badqemu"), share);
        c.extraArgs = QStringLiteral("-this-option-does-not-exist");
        Vm vm(c);
        QString err;
        check(vm.start(&err), "VM with a bad QEMU option is launched", err);
        const qint64 pid = virtiofsdPid(vm);
        check(waitFor([&] { return vm.state() == Vm::State::Stopped; }, 15000), "QEMU exits");
        check(waitFor([&] { return !processRunning(pid); }, 5000), "virtiofsd stopped after QEMU failed");
    }

    g_failures += runChild(QCoreApplication::applicationFilePath(), QStringLiteral("fallback"), base);
    g_failures += runChild(QCoreApplication::applicationFilePath(), QStringLiteral("old"), base);

    // A guest that mounts the folder, reads a file from the host and writes one back as user 1000.
    if (argc >= 3) {
        const QString kernel = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
        const QString initrd = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
        const QByteArray token = QUuid::createUuid().toByteArray();
        for (const bool readOnly : {false, true}) {
            const char *label = readOnly ? "read-only guest" : "guest";
            if (readOnly && !SharedFolder::support().readOnly) {
                std::printf("skip %s: this virtiofsd has no --readonly\n", label);
                continue;
            }
            QFile::remove(share + QStringLiteral("/from-guest.txt"));
            writeFile(share + QStringLiteral("/from-host.txt"), token);
            const QString serial = base + QStringLiteral("/serial-%1.log").arg(readOnly);
            VmConfig c = makeConfig(base, QString::fromLatin1(label).replace(QLatin1Char(' '), QLatin1Char('-')), share);
            c.sharedFolderReadOnly = readOnly;
            c.extraArgs = QStringLiteral(R"(-kernel "%1" -initrd "%2" -append "console=ttyS0 panic=-1 quiet" -serial "file:%3")")
                              .arg(kernel, initrd, serial);
            Vm vm(c);
            QString err;
            const bool started = vm.start(&err);
            check(started, label, err);
            if (!started)
                continue;
            const bool off = waitFor([&] { return vm.state() == Vm::State::Stopped; }, 90000);
            check(off, "guest powered off by itself");
            check(!readAll(vm.logFile()).contains(QLatin1String("qemu-system-x86_64: ")), "QEMU reported no error",
                  readAll(vm.logFile()).right(400));
            if (!off)
                stopVm(vm);
            const QString out = readAll(serial);
            check(out.contains(QLatin1String("bento-test: mounted")), "guest mounted the shared folder", out.right(600));
            check(out.contains(QLatin1String("bento-test: owner 1000:1000")), "guest sees the host's files as user 1000");
            if (!readOnly) {
                check(out.contains(QLatin1String("bento-test: write ok")), "guest user wrote a file", out.right(800));
                const QFileInfo written(share + QStringLiteral("/from-guest.txt"));
                check(readAll(written.filePath()).toUtf8() == token, "host sees what the guest wrote");
                check(written.ownerId() == ::getuid(), "the guest's file belongs to the host user");
            } else {
                check(out.contains(QLatin1String("bento-test: write failed")), "guest can't write to a read-only share");
                check(!QFileInfo::exists(share + QStringLiteral("/from-guest.txt")), "nothing was written");
            }
        }
    }

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
