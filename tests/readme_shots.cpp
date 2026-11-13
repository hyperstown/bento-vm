// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

// Stages the README screenshots in a throwaway library and saves them with
// rounded corners + a soft shadow (transparent PNG, fine on light/dark pages).
// Run inside a headless compositor with throwaway XDG_* dirs:
//
//   readme_shots <out-dir> <linux-live.iso> <windows.iso> <arch.iso>
//   readme_shots --library <out-dir> <name> <linux-live.iso> <windows.iso>
// The second form only captures the library in the current desktop style
// (e.g. with a Breeze Dark kdeglobals), reusing thumbnails from a full run.
#include "core/AppSettings.h"
#include "core/DiskOps.h"
#include "core/Paths.h"
#include "core/Vm.h"
#include "core/VmLibrary.h"
#include "display/DisplayWidget.h"
#include "display/GlibIntegration.h"
#include "ui/MainWindow.h"
#include "ui/NewVmWizard.h"
#include "ui/SettingsDialog.h"
#include "ui/Theme.h"
#include "ui/VmCard.h"
#include "ui/VmWindow.h"

#include <QApplication>
#include <QDir>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QToolBar>
#include <QUuid>

#include <cstdio>
#include <functional>

static QString g_out;
static QList<std::pair<int, std::function<void()>>> g_steps;

static void step(int ms, std::function<void()> fn) { g_steps.append({ms, std::move(fn)}); }
static void run(int i = 0)
{
    if (i < g_steps.size())
        QTimer::singleShot(g_steps[i].first, [i] {
            g_steps[i].second();
            run(i + 1);
        });
}

// Window capture → rounded corners, hairline border and a soft drop shadow.
static void framed(QWidget *w, const QString &name, int maxWidth = 1400)
{
    if (!w) {
        std::printf("missing widget for %s\n", qPrintable(name));
        return;
    }
    QImage shot = w->grab().toImage();
    if (shot.width() > maxWidth)
        shot = shot.scaledToWidth(maxWidth, Qt::SmoothTransformation);
    shot.setDevicePixelRatio(1.0);
    const int m = 36, r = 12;
    QImage out(shot.width() + 2 * m, shot.height() + 2 * m, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF rect(m, m, shot.width(), shot.height());
    for (int i = 0; i < 24; ++i) {   // layered shadow
        QPainterPath s;
        const qreal grow = 24 - i;
        s.addRoundedRect(rect.adjusted(-grow, -grow + 10, grow, grow + 10), r + grow, r + grow);
        p.fillPath(s, QColor(0, 0, 0, 4));
    }
    QPainterPath clip;
    clip.addRoundedRect(rect, r, r);
    p.save();
    p.setClipPath(clip);
    p.drawImage(rect.topLeft(), shot);
    p.restore();
    p.setPen(QPen(QColor(128, 128, 128, 90), 1));
    p.drawPath(clip);
    p.end();
    out.save(g_out + QLatin1Char('/') + name + QStringLiteral(".png"));
    std::printf("saved %-20s %dx%d\n", qPrintable(name), out.width(), out.height());
    std::fflush(stdout);
}

template<typename T>
static T *visible()
{
    for (QWidget *w : QApplication::topLevelWidgets())
        if (auto *t = qobject_cast<T *>(w); t && t->isVisible())
            return t;
    return nullptr;
}

static QPushButton *button(QWidget *root, const QString &text)
{
    for (QPushButton *b : root->findChildren<QPushButton *>())
        if (b->text() == text && b->isVisible())
            return b;
    return nullptr;
}

static Vm *makeVm(const QString &location, const QString &name, const QString &osId, const QString &label, int cores,
                  int mem, const QString &graphics, const QString &firmware, const QString &iso, const QString &bus)
{
    VmConfig c;
    c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.name = name;
    c.osId = osId;
    c.osLabel = label;
    c.cores = cores;
    c.threads = 2;
    c.memoryMiB = mem;
    c.graphics = graphics;
    c.firmware = firmware;
    c.audio = false;
    c.dir = VmLibrary::uniqueFolder(location, name);
    QDir().mkpath(c.dir);
    QString err;
    DiskOps::createDisk(c.dir + QStringLiteral("/disk.qcow2"), 64, &err);
    c.disks << DiskSpec{QStringLiteral("disk.qcow2"), QStringLiteral("qcow2"), bus};
    if (!iso.isEmpty())
        c.cdroms << iso;
    c.created = QDateTime::currentDateTime();
    c.save();
    return VmLibrary::instance()->add(c);
}

static void pressEnter(VmWindow *w)
{
    if (auto *d = w ? w->findChild<DisplayWidget *>() : nullptr)
        d->sendKeyCombo({0x1c});
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("bento-vm"));
    QApplication::setApplicationDisplayName(QStringLiteral("Bento"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/bento-vm.svg")));
    if (argc < 5)
        return 2;
    const bool libraryOnly = QByteArray(argv[1]) == "--library";
    if (libraryOnly && argc < 6)
        return 2;
    const int a = libraryOnly ? 2 : 1;
    g_out = QString::fromLocal8Bit(argv[a]);
    const QString shotName = libraryOnly ? QString::fromLocal8Bit(argv[a + 1]) : QString();
    const QString linuxIso = QString::fromLocal8Bit(argv[libraryOnly ? a + 2 : a + 1]);
    const QString winIso = QString::fromLocal8Bit(argv[libraryOnly ? a + 3 : a + 2]);
    const QString archIso = libraryOnly ? QString() : QString::fromLocal8Bit(argv[a + 3]);
    QDir().mkpath(g_out);

    GlibIntegration::ensure();
    Theme::instance()->init();
    Theme::instance()->apply(libraryOnly ? QStringLiteral("system") : QStringLiteral("modern-dark"));
    AppSettings::instance().setCloseAction(QStringLiteral("keep"));
    // Neutral-looking paths: they are visible in the wizard and on cards.
    const QString lib = QStringLiteral("/var/tmp/Virtual Machines");
    Paths::ensureDir(lib);

    // A VM on an external drive that is then "unplugged".
    const QString ext = QDir::cleanPath(g_out + QStringLiteral("/../External SSD"));
    QDir().mkpath(ext + QStringLiteral("/VMs"));
    AppSettings::instance().setStorageLocations({lib, QDir::cleanPath(ext + QStringLiteral("/VMs"))});
    makeVm(QDir::cleanPath(ext + QStringLiteral("/VMs")), QStringLiteral("Fedora Workstation"), QStringLiteral("fedora"),
           QStringLiteral("Fedora 41"), 4, 8192, QStringLiteral("venus"), QStringLiteral("uefi"), QString(), QStringLiteral("virtio"));
    Vm *debian = makeVm(lib, QStringLiteral("Debian 12"), QStringLiteral("debian"), QStringLiteral("Debian 12 Bookworm"), 2,
                        4096, QStringLiteral("virgl"), QStringLiteral("uefi"), QString(), QStringLiteral("virtio"));
    Vm *win = makeVm(lib, QStringLiteral("Windows 10"), QStringLiteral("windows"), QStringLiteral("Windows 10 / 11"), 2, 4096,
                     QStringLiteral("2d"), QStringLiteral("bios"), winIso, QStringLiteral("sata"));
    Vm *mint = makeVm(lib, QStringLiteral("Linux Mint"), QStringLiteral("linuxmint"), QStringLiteral("Linux Mint 21.1 Cinnamon"),
                      2, 4096, QStringLiteral("virgl"), QStringLiteral("uefi"), linuxIso, QStringLiteral("virtio"));
    Q_UNUSED(debian);
    QDir(ext).removeRecursively();
    VmLibrary::instance()->refresh();

    MainWindow mw;
    mw.resize(1290, 720);
    mw.show();

    if (libraryOnly) {
        win->setThumbnail(QImage(g_out + QStringLiteral("/raw-Windows 10.png")));
        mint->setThumbnail(QImage(g_out + QStringLiteral("/raw-Linux Mint.png")));
        step(500, [&] {
            QString err;
            mint->start(&err);   // only for the "Running" state on the card
        });
        step(3000, [&] {
            for (VmCard *c : mw.findChildren<VmCard *>())
                if (c->vm() == mint)
                    Q_EMIT c->clicked(c);
        });
        step(1000, [&] { framed(&mw, shotName); });
        step(100, [&] { mint->forceStop(); });
        step(2000, [&] { app.quit(); });
        run();
        return app.exec();
    }

    auto grabThumb = [](Vm *vm) {
        VmWindow *w = nullptr;
        for (QWidget *t : QApplication::topLevelWidgets())
            if (auto *vw = qobject_cast<VmWindow *>(t); vw && vw->vm() == vm)
                w = vw;
        if (auto *d = w ? w->findChild<DisplayWidget *>() : nullptr) {
            const QImage f = d->grabGuestFrame();
            f.save(g_out + QStringLiteral("/raw-") + vm->name() + QStringLiteral(".png"));
            vm->setThumbnail(f);
        }
        return w;
    };

    // 1. Windows installer screen as a thumbnail.
    step(1000, [&] { mw.openVm(win); });
    step(5000, [&] { if (VmWindow *w = visible<VmWindow>()) w->resize(1280, 860); });
    step(85000, [&] {
        if (VmWindow *w = grabThumb(win))
            w->close();
        win->forceStop();
    });
    // 2. Linux Mint live desktop: the hero shot.
    step(4000, [&] { mw.openVm(mint); });
    step(3000, [&] { if (VmWindow *w = visible<VmWindow>()) w->resize(1440, 900); });
    step(12000, [&] { pressEnter(visible<VmWindow>()); });   // boot menu, in case it doesn't time out
    step(140000, [&] {
        VmWindow *w = visible<VmWindow>();
        grabThumb(mint);
        framed(w, QStringLiteral("vm-window"), 1440);
    });
    // 3. Library in both looks.
    step(1500, [&] {
        mw.raise();
        for (VmCard *c : mw.findChildren<VmCard *>())
            if (c->vm() == mint)
                Q_EMIT c->clicked(c);
    });
    step(800, [&] { framed(&mw, QStringLiteral("library-modern-dark")); });
    step(100, [&] { Theme::instance()->apply(QStringLiteral("system")); });
    step(1500, [&] { framed(&mw, QStringLiteral("library-system")); });
    step(100, [&] { Theme::instance()->apply(QStringLiteral("modern-light")); });
    step(1500, [&] { framed(&mw, QStringLiteral("library-modern-light")); });
    step(100, [&] { Theme::instance()->apply(QStringLiteral("modern-dark")); });
    // 4. Wizard: OS detection + graphics page.
    step(800, [&] { mw.openWizard(archIso); });
    step(1000, [&] { button(mw.findChild<NewVmWizard *>(), QStringLiteral("Next"))->click(); });
    step(6000, [&] { framed(mw.findChild<NewVmWizard *>(), QStringLiteral("wizard-os")); });
    step(100, [&] { button(mw.findChild<NewVmWizard *>(), QStringLiteral("Next"))->click(); });
    step(800, [&] { framed(mw.findChild<NewVmWizard *>(), QStringLiteral("wizard-resources")); });
    step(100, [&] { button(mw.findChild<NewVmWizard *>(), QStringLiteral("Next"))->click(); });
    step(800, [&] {
        framed(mw.findChild<NewVmWizard *>(), QStringLiteral("wizard-graphics"));
        mw.findChild<NewVmWizard *>()->reject();
    });
    // 5. Settings → Appearance.
    step(500, [&] {
        auto *d = new SettingsDialog(&mw);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
    });
    step(1200, [&] {
        auto *d = mw.findChild<SettingsDialog *>();
        framed(d, QStringLiteral("settings-appearance"));
        d->close();
    });
    step(500, [&] {
        mint->forceStop();
        win->forceStop();
    });
    step(3000, [&] { app.quit(); });

    run();
    QTimer::singleShot(420000, &app, [&] {
        mint->forceStop();
        win->forceStop();
        app.exit(1);
    });
    return app.exec();
}
