// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

// Checks that creating a VM never blocks the event loop and that every way it
// can fail comes with a reason: VmCreator against a fake qemu-img that is
// slow, silent, crashes, complains or can't be started, plus folders and
// disk images that can't be used. Prints each error message for review.
//   create_test <scratch-dir>
#include "core/DiskOps.h"
#include "core/VmCreator.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include <cstdio>

namespace {

QString g_dir;
int g_failures = 0;

void check(bool ok, const QString &what)
{
    std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok)
        ++g_failures;
}

void setMode(const QString &mode)
{
    QFile f(g_dir + QStringLiteral("/bin/mode"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(mode.toUtf8());
}

struct Run
{
    VmCreator::Result result;
    QList<int> percents;
    qint64 maxGapMs = 0;   // longest the event loop went without running a timer
};

// Runs a creation to the end while a 20 ms timer measures event-loop stalls.
Run create(const VmCreator::Request &r, int cancelAfterMs = -1)
{
    Run run;
    VmCreator creator;
    QEventLoop loop;
    QElapsedTimer sinceTick;
    QTimer ticker;
    ticker.setInterval(20);
    QObject::connect(&ticker, &QTimer::timeout, [&] {
        run.maxGapMs = std::max(run.maxGapMs, sinceTick.restart());
    });
    QObject::connect(&creator, &VmCreator::progress, [&](const QString &, int percent) {
        if (percent >= 0 && (run.percents.isEmpty() || run.percents.last() != percent))
            run.percents << percent;
    });
    QObject::connect(&creator, &VmCreator::finished, [&](const VmCreator::Result &res) {
        run.result = res;
        loop.quit();
    });
    if (cancelAfterMs >= 0)
        QTimer::singleShot(cancelAfterMs, &creator, &VmCreator::cancel);
    sinceTick.start();
    ticker.start();
    creator.start(r);
    loop.exec();
    return run;
}

VmCreator::Request request(const QString &name, int diskGiB = 8)
{
    VmCreator::Request r;
    r.config.id = name;
    r.config.name = name;
    r.config.osId = QStringLiteral("linux");
    r.location = g_dir + QStringLiteral("/vms");
    r.newDiskGiB = diskGiB;
    r.newDiskBus = QStringLiteral("virtio");
    return r;
}

void expectFailure(const char *label, const Run &run, const QString &mustContain)
{
    std::printf("%s\n", label);
    const VmCreator::Result &r = run.result;
    check(!r.ok && !r.canceled, QStringLiteral("fails"));
    check(!r.title.isEmpty() && !r.error.isEmpty(), QStringLiteral("says what and why"));
    check(r.error.contains(mustContain), QStringLiteral("reason mentions “%1”").arg(mustContain));
    check(QDir(g_dir + QStringLiteral("/vms")).isEmpty(), QStringLiteral("leaves nothing behind"));
    std::printf("        %s: %s\n", qPrintable(r.title), qPrintable(QString(r.error).replace(QLatin1Char('\n'), QStringLiteral("\n        "))));
    std::printf("        details: %s\n", qPrintable(QString(r.details).replace(QLatin1Char('\n'), QStringLiteral(" | "))));
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: create_test <scratch-dir>\n");
        return 2;
    }
    g_dir = QDir(QString::fromLocal8Bit(argv[1])).absolutePath();
    QDir(g_dir).removeRecursively();
    QDir().mkpath(g_dir + QStringLiteral("/bin"));
    QDir().mkpath(g_dir + QStringLiteral("/vms"));
    for (const char *xdg : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"})
        qputenv(xdg, (g_dir + QStringLiteral("/xdg/") + QString::fromLatin1(xdg)).toLocal8Bit());

    // The fake qemu-img. "measure" and "info" behave like the real one.
    const QString fake = g_dir + QStringLiteral("/bin/qemu-img");
    {
        QFile f(fake);
        if (!f.open(QIODevice::WriteOnly))
            return 2;
        f.write(R"SH(#!/bin/bash
real=$(PATH=${PATH#*:} command -v qemu-img)
mode=$(cat "$(dirname "$0")/mode")
[ "$1-$mode" = measure-slow ] && { echo '{"required": 8388608, "fully-allocated": 8589934592}'; exit; }
case "$1" in measure|info) exec "$real" "$@";; esac
path="${@: -2:1}"
case "$mode" in
    real) exec "$real" "$@";;
    slow) for i in $(seq 32); do   # 8 MiB in 3.2 s
              dd if=/dev/zero bs=256K count=1 oflag=append conv=notrunc,fsync of="$path" status=none; sleep 0.1
          done;;
    silent) exit 3;;
    crash) kill -SEGV $$;;
    message) echo "qemu-img: $path: Could not create '$path': No space left on device" >&2; exit 1;;
esac
)SH");
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    qputenv("PATH", (g_dir + QStringLiteral("/bin:") + qEnvironmentVariable("PATH")).toLocal8Bit());

    QCoreApplication app(argc, argv);

    std::printf("slow drive (fake qemu-img writes 8 MiB of metadata in 3 s)\n");
    {
        setMode(QStringLiteral("slow"));
        const Run run = create(request(QStringLiteral("Slow")));
        check(run.result.ok, QStringLiteral("succeeds: %1").arg(run.result.error));
        check(run.maxGapMs < 250, QStringLiteral("event loop never stalls (longest gap %1 ms)").arg(run.maxGapMs));
        int between = 0;
        for (int p : run.percents)
            between += p > 0 && p < 100;
        check(between >= 5 && !run.percents.isEmpty() && run.percents.last() == 100,
              QStringLiteral("reports progress (%1 steps between 0 and 100)").arg(between));
        check(QFileInfo::exists(run.result.config.configFile()), QStringLiteral("saves vm.json"));
        QDir(run.result.config.dir).removeRecursively();
    }

    std::printf("cancel while creating\n");
    {
        setMode(QStringLiteral("slow"));
        const Run run = create(request(QStringLiteral("Canceled")), 1000);
        check(run.result.canceled && !run.result.ok, QStringLiteral("reports cancellation"));
        check(QDir(g_dir + QStringLiteral("/vms")).isEmpty(), QStringLiteral("removes the new folder"));
    }

    setMode(QStringLiteral("silent"));
    expectFailure("qemu-img fails without a message", create(request(QStringLiteral("Silent"))), QStringLiteral("exit code 3"));
    setMode(QStringLiteral("crash"));
    expectFailure("qemu-img crashes", create(request(QStringLiteral("Crash"))), QStringLiteral("stopped unexpectedly"));
    setMode(QStringLiteral("message"));
    expectFailure("qemu-img reports an error", create(request(QStringLiteral("Full"))), QStringLiteral("No space left on device"));

    QFile::setPermissions(fake, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    expectFailure("qemu-img can't be started", create(request(QStringLiteral("NoQemuImg"))), QStringLiteral("couldn't be started"));
    QFile::setPermissions(fake, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    std::printf("storage location isn't writable\n");
    {
        setMode(QStringLiteral("real"));
        const QString ro = g_dir + QStringLiteral("/readonly");
        QDir().mkpath(ro);
        QFile::setPermissions(ro, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        VmCreator::Request r = request(QStringLiteral("ReadOnly"));
        r.location = ro;
        const Run run = create(r);
        check(!run.result.ok && run.result.error.contains(QStringLiteral("Permission denied")),
              QStringLiteral("explains it: %1").arg(run.result.error));
        QFile::setPermissions(ro, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

    VmCreator::Request copy = request(QStringLiteral("Copy"), 0);
    copy.existingDisk = g_dir + QStringLiteral("/missing.qcow2");
    copy.copyExistingDisk = true;
    copy.existingDiskBus = QStringLiteral("sata");
    expectFailure("disk image to copy is missing", create(copy), QStringLiteral("No such file or directory"));

    std::printf("real qemu-img, small disk\n");
    {
        setMode(QStringLiteral("real"));
        const Run run = create(request(QStringLiteral("Real"), 2));
        check(run.result.ok, QStringLiteral("succeeds: %1").arg(run.result.error));
        const DiskOps::ImageInfo info = DiskOps::imageInfo(run.result.config.dir + QStringLiteral("/disk.qcow2"));
        check(info.ok && info.format == QLatin1String("qcow2") && info.virtualSize == 2LL << 30,
              QStringLiteral("disk is a 2 GiB qcow2"));
        QDir(run.result.config.dir).removeRecursively();
        const DiskOps::ImageInfo missing = DiskOps::imageInfo(g_dir + QStringLiteral("/missing.qcow2"));
        check(!missing.ok && !missing.error.isEmpty(), QStringLiteral("imageInfo explains a missing file: %1").arg(missing.error));
    }

    std::printf(g_failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
