// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

// Clicks through the New VM wizard and its Create step on a real drive, like a
// user would:
//  - the window keeps responding and shows progress while the disk is created
//  - clicking Create again doesn't create a second VM
//  - Cancel stops, removes what was created and closes the wizard
//  - otherwise the VM is created; its folder is printed (delete it afterwards)
// Run inside a headless compositor with throwaway XDG_* directories.
//   create_ui_test <out-dir> <installer.iso> <storage-location> <disk GiB>
#include "core/AppSettings.h"
#include "core/DiskOps.h"
#include "core/Vm.h"
#include "core/VmLibrary.h"
#include "ui/NewVmWizard.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

#include <cstdio>
#include <functional>

namespace {

int g_failures = 0;
QString g_out;
qint64 g_maxGap = 0;
QElapsedTimer g_sinceTick;

void check(bool ok, const QString &what)
{
    std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    std::fflush(stdout);
    if (!ok)
        ++g_failures;
}

void wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

bool waitUntil(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!done()) {
        if (t.elapsed() > timeoutMs)
            return false;
        wait(100);
    }
    return true;
}

QPushButton *button(QWidget *root, const QString &text)
{
    for (QPushButton *b : root->findChildren<QPushButton *>())
        if (b->text() == text && b->isVisible())
            return b;
    return nullptr;
}

void click(QWidget *root, const QString &text)
{
    if (QPushButton *b = button(root, text))
        b->click();
    else
        check(false, QStringLiteral("button “%1” exists").arg(text));
}

QStringList vmFolders(const QString &location)
{
    return QDir(location).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
}

// Opens the wizard and goes to its last page, with the VM not started afterwards.
QPointer<NewVmWizard> openWizard(const QString &iso, int diskGiB)
{
    QPointer<NewVmWizard> wiz = new NewVmWizard;
    wiz->setAttribute(Qt::WA_DeleteOnClose);
    wiz->setSourcePath(iso);
    wiz->show();
    wait(1500);
    click(wiz, QStringLiteral("Next"));
    wait(6000);   // OS detection
    click(wiz, QStringLiteral("Next"));
    wait(800);
    for (QSpinBox *s : wiz->findChildren<QSpinBox *>())
        if (s->suffix() == QLatin1String(" GiB"))
            s->setValue(diskGiB);
    click(wiz, QStringLiteral("Next"));
    wait(800);
    click(wiz, QStringLiteral("Next"));
    wait(800);
    for (QCheckBox *c : wiz->findChildren<QCheckBox *>())
        if (c->text() == QLatin1String("Start the VM right away"))
            c->setChecked(false);
    return wiz;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("bento-vm"));
    QApplication::setApplicationDisplayName(QStringLiteral("BentoVM"));
    if (argc < 5) {
        std::fprintf(stderr, "usage: create_ui_test <out-dir> <iso> <storage-location> <disk GiB>\n");
        return 2;
    }
    g_out = QString::fromLocal8Bit(argv[1]);
    const QString iso = QString::fromLocal8Bit(argv[2]);
    const QString location = QDir::cleanPath(QString::fromLocal8Bit(argv[3]));
    const int diskGiB = QString::fromLocal8Bit(argv[4]).toInt();
    QDir().mkpath(g_out);

    AppSettings::instance().setStorageLocations({location});
    AppSettings::instance().setDefaultLocation(location);
    Theme::instance()->init();
    Theme::instance()->apply(QStringLiteral("auto"));
    VmLibrary::instance()->refresh();

    // Measures how long the event loop goes without running a 20 ms timer.
    QTimer ticker;
    ticker.setInterval(20);
    QObject::connect(&ticker, &QTimer::timeout, [] { g_maxGap = std::max(g_maxGap, g_sinceTick.restart()); });
    g_sinceTick.start();
    ticker.start();
    const QStringList before = vmFolders(location);

    std::printf("Cancel while the disk is being created\n");
    {
        QPointer<NewVmWizard> wiz = openWizard(iso, diskGiB);
        g_maxGap = 0;
        g_sinceTick.restart();
        click(wiz, QStringLiteral("Create"));
        wait(300);
        // Impatient clicks: the button must not start a second creation.
        QPushButton *busy = button(wiz, QStringLiteral("Creating…"));
        check(busy && !busy->isEnabled(), QStringLiteral("Create turns into a disabled “Creating…”"));
        if (busy)
            for (int i = 0; i < 3; ++i)
                busy->click();
        wait(8000);
        wiz->grab().save(g_out + QStringLiteral("/progress.png"));
        click(wiz, QStringLiteral("Cancel"));
        check(waitUntil([&] { return wiz.isNull() || !wiz->isVisible(); }, 60000), QStringLiteral("Cancel closes the wizard"));
        check(vmFolders(location) == before, QStringLiteral("nothing is left in %1").arg(location));
        check(g_maxGap < 300, QStringLiteral("window kept responding (longest stall %1 ms)").arg(g_maxGap));
    }

    std::printf("Create a %d GiB VM\n", diskGiB);
    {
        QPointer<NewVmWizard> wiz = openWizard(iso, diskGiB);
        QString createdId;
        QObject::connect(wiz, &NewVmWizard::vmCreated, [&](Vm *vm, bool) { createdId = vm->id(); });
        g_maxGap = 0;
        g_sinceTick.restart();
        QElapsedTimer t;
        t.start();
        click(wiz, QStringLiteral("Create"));
        wait(30000);
        if (wiz)
            wiz->grab().save(g_out + QStringLiteral("/progress-30s.png"));
        const bool closed = waitUntil([&] { return wiz.isNull() || !wiz->isVisible(); }, 20 * 60 * 1000);
        std::printf("        took %.1f s\n", t.elapsed() / 1000.0);
        check(closed && !createdId.isEmpty(), QStringLiteral("VM created and wizard closed"));
        check(g_maxGap < 300, QStringLiteral("window kept responding (longest stall %1 ms)").arg(g_maxGap));
        QStringList added = vmFolders(location);
        for (const QString &b : before)
            added.removeAll(b);
        check(added.size() == 1, QStringLiteral("exactly one new VM folder: %1").arg(added.join(QStringLiteral(", "))));
        if (Vm *vm = VmLibrary::instance()->find(createdId)) {
            const VmConfig &c = vm->config();
            check(!c.dir.contains(QLatin1String("//")), QStringLiteral("clean folder path: %1").arg(c.dir));
            const DiskOps::ImageInfo info = DiskOps::imageInfo(c.diskPath(0));
            check(info.ok && info.virtualSize == qint64(diskGiB) << 30,
                  QStringLiteral("disk is %1 GiB: %2").arg(diskGiB).arg(info.ok ? QString::number(info.virtualSize >> 30) : info.error));
            std::printf("CREATED %s\n", qPrintable(c.dir));
        }
    }

    std::printf(g_failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
