// End-to-end UI smoke test. Run it inside a headless compositor with
// throwaway XDG_* directories; it drives the real widgets and saves
// screenshots:
//
//   ui_test <out-dir> <installer.iso>
//
// Scenario: library with an "unplugged drive" VM → wizard (detects the ISO)
// → create + boot the VM with Venus → type into the guest → themes →
// dialogs → force off.
#include "core/AppSettings.h"
#include "core/DiskOps.h"
#include "core/Paths.h"
#include "core/Vm.h"
#include "core/VmLibrary.h"
#include "display/DisplayWidget.h"
#include "display/GlibIntegration.h"
#include "ui/MainWindow.h"
#include "ui/NewVmWizard.h"
#include "ui/ResourceEditors.h"
#include "ui/SettingsDialog.h"
#include "ui/Theme.h"
#include "ui/VmSettingsDialog.h"
#include "ui/VmWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QKeyEvent>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QUuid>

#include <cstdio>
#include <functional>

static QString g_out;
static QList<std::pair<int, std::function<void()>>> g_steps;

static void shot(QWidget *w, const QString &name)
{
    if (!w) {
        std::printf("shot %s: no widget\n", qPrintable(name));
        return;
    }
    const QPixmap pm = w->grab();
    pm.save(g_out + QLatin1Char('/') + name + QStringLiteral(".png"));
    std::printf("shot %-24s %dx%d\n", qPrintable(name), pm.width(), pm.height());
    std::fflush(stdout);
}

static void step(int delayMs, std::function<void()> fn)
{
    g_steps.append({delayMs, std::move(fn)});
}

static void runSteps(int i = 0)
{
    if (i >= g_steps.size())
        return;
    QTimer::singleShot(g_steps[i].first, [i] {
        g_steps[i].second();
        runSteps(i + 1);
    });
}

static QPushButton *button(QWidget *root, const QString &text)
{
    for (QPushButton *b : root->findChildren<QPushButton *>())
        if (b->text() == text && b->isVisible())
            return b;
    return nullptr;
}

template<typename T>
static T *topLevel()
{
    for (QWidget *w : QApplication::topLevelWidgets())
        if (auto *t = qobject_cast<T *>(w); t && t->isVisible())
            return t;
    return nullptr;
}

// US layout evdev codes
static int evdevFor(QChar c, bool *shift)
{
    static const char *rows[] = {"1234567890-=", "qwertyuiop[]", "asdfghjkl;'", "zxcvbnm,./"};
    static const int starts[] = {2, 16, 30, 44};
    static const char *shifted = "!@#$%^&*()_+QWERTYUIOP{}ASDFGHJKL:\"ZXCVBNM<>?|";
    static const char *base = "1234567890-=qwertyuiop[]asdfghjkl;'zxcvbnm,./\\";
    *shift = false;
    char ch = char(c.toLatin1());
    if (ch == ' ')
        return 57;
    if (ch == '\n')
        return 28;
    if (ch == '\\')
        return 43;
    if (const char *p = std::strchr(shifted, ch); p && ch) {
        *shift = true;
        ch = base[p - shifted];
        if (ch == '\\')
            return 43;
    }
    for (int r = 0; r < 4; ++r)
        if (const char *p = std::strchr(rows[r], ch); p && ch)
            return starts[r] + int(p - rows[r]);
    return 0;
}

// Synthetic key events, paced like a fast typist so the guest's PS/2 queue
// (16 scancodes) never overflows.
static void typeText(QWidget *target, const QString &text)
{
    QList<std::pair<QEvent::Type, int>> events;
    for (QChar c : text) {
        bool shift = false;
        const int code = evdevFor(c, &shift);
        if (!code)
            continue;
        if (shift)
            events.append({QEvent::KeyPress, 42});
        events.append({QEvent::KeyPress, code});
        events.append({QEvent::KeyRelease, code});
        if (shift)
            events.append({QEvent::KeyRelease, 42});
    }
    QPointer<QWidget> guard(target);
    for (int i = 0; i < events.size(); ++i) {
        QTimer::singleShot(i * 12, [guard, ev = events[i]] {
            if (!guard)
                return;
            QKeyEvent ke(ev.first, Qt::Key_unknown, Qt::NoModifier, quint32(ev.second + 8), 0, 0);
            QApplication::sendEvent(guard, &ke);
        });
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("bento-vm"));
    QApplication::setApplicationDisplayName(QStringLiteral("Bento"));
    QApplication::setDesktopFileName(QStringLiteral("bento-vm"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/bento-vm.svg")));
    if (argc < 3) {
        std::fprintf(stderr, "usage: ui_test <out-dir> <iso>\n");
        return 2;
    }
    g_out = QString::fromLocal8Bit(argv[1]);
    const QString iso = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(g_out);

    GlibIntegration::ensure();
    Theme::instance()->init();
    Theme::instance()->apply(QStringLiteral("system"));
    Paths::ensureDir(Paths::defaultStorageLocation());

    // A VM on an "external drive" that then gets unplugged.
    const QString ext = g_out + QStringLiteral("/ext-drive");
    const QString extVms = ext + QStringLiteral("/VMs");
    {
        VmConfig c;
        c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        c.name = QStringLiteral("Fedora Workstation");
        c.osId = QStringLiteral("fedora");
        c.osLabel = QStringLiteral("Fedora 41");
        c.cores = 4;
        c.threads = 2;
        c.memoryMiB = 8192;
        c.dir = extVms + QStringLiteral("/Fedora Workstation");
        QDir().mkpath(c.dir);
        QString err;
        DiskOps::createDisk(c.dir + QStringLiteral("/disk.qcow2"), 1, &err);
        c.disks << DiskSpec{QStringLiteral("disk.qcow2"), QStringLiteral("qcow2"), QStringLiteral("virtio")};
        c.save();
        AppSettings::instance().setStorageLocations({Paths::defaultStorageLocation(), extVms});
        VmLibrary::instance()->refresh();
        QDir(ext).removeRecursively();   // "unplug"
        VmLibrary::instance()->refresh();
    }

    MainWindow w;
    w.resize(1280, 800);
    w.show();

    step(1500, [&] { shot(&w, QStringLiteral("01-main-system")); });
    step(100, [&] { w.openWizard(iso); });
    step(1200, [&] { shot(topLevel<NewVmWizard>() ? static_cast<QWidget *>(topLevel<NewVmWizard>()) : w.findChild<NewVmWizard *>(), QStringLiteral("02-wizard-source")); });
    step(100, [&] {
        auto *wiz = w.findChild<NewVmWizard *>();
        if (QPushButton *b = button(wiz, QStringLiteral("Next")))
            b->click();
    });
    step(6000, [&] { shot(w.findChild<NewVmWizard *>(), QStringLiteral("03-wizard-os")); });
    step(100, [&] { button(w.findChild<NewVmWizard *>(), QStringLiteral("Next"))->click(); });
    step(800, [&] {
        auto *wiz = w.findChild<NewVmWizard *>();
        shot(wiz, QStringLiteral("04-wizard-resources"));
        wiz->findChild<MemoryEditor *>()->setValueMiB(2048);
        for (QCheckBox *c : wiz->findChildren<QCheckBox *>())
            if (c->text() == QLatin1String("Sound"))
                c->setChecked(false);
    });
    step(100, [&] { button(w.findChild<NewVmWizard *>(), QStringLiteral("Next"))->click(); });
    step(800, [&] { shot(w.findChild<NewVmWizard *>(), QStringLiteral("05-wizard-features")); });
    step(100, [&] { button(w.findChild<NewVmWizard *>(), QStringLiteral("Next"))->click(); });
    step(800, [&] { shot(w.findChild<NewVmWizard *>(), QStringLiteral("06-wizard-summary")); });
    step(100, [&] { button(w.findChild<NewVmWizard *>(), QStringLiteral("Create"))->click(); });
    step(4000, [&] {
        if (VmWindow *vw = topLevel<VmWindow>())
            vw->resize(1280, 860);
        shot(&w, QStringLiteral("07-main-running"));
    });
    step(10000, [&] { shot(topLevel<VmWindow>(), QStringLiteral("08-vm-early")); });
    step(70000, [&] { shot(topLevel<VmWindow>(), QStringLiteral("09-vm-booted")); });
    step(200, [&] {
        if (VmWindow *vw = topLevel<VmWindow>())
            typeText(vw->findChild<DisplayWidget *>(), QStringLiteral("clear; dmesg | grep -iE 'virtio.gpu|virgl' | tail; ls /dev/dri\n"));
    });
    step(6000, [&] { shot(topLevel<VmWindow>(), QStringLiteral("10-vm-typed")); });
    step(100, [&] { Theme::instance()->apply(QStringLiteral("modern-dark")); });
    step(1500, [&] {
        shot(&w, QStringLiteral("11-main-modern-dark"));
        shot(topLevel<VmWindow>(), QStringLiteral("12-vm-modern-dark"));
    });
    step(100, [&] {
        auto *d = new SettingsDialog(&w);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
    });
    step(1200, [&] {
        auto *d = w.findChild<SettingsDialog *>();
        shot(d, QStringLiteral("13-settings-appearance"));
        d->showStoragePage();
    });
    step(600, [&] {
        auto *d = w.findChild<SettingsDialog *>();
        shot(d, QStringLiteral("14-settings-storage"));
        d->close();
    });
    step(300, [&] {
        Vm *vm = VmLibrary::instance()->findByName(QStringLiteral("Arch Linux"));
        if (!vm)
            return;
        auto *d = new VmSettingsDialog(vm, &w);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
    });
    step(1200, [&] {
        auto *d = w.findChild<VmSettingsDialog *>();
        if (!d)
            return;
        shot(d, QStringLiteral("15-vmsettings-general"));
        d->setCurrentPage(2);
    });
    step(600, [&] {
        if (auto *d = w.findChild<VmSettingsDialog *>()) {
            shot(d, QStringLiteral("16-vmsettings-display"));
            d->setCurrentPage(8);
        }
    });
    step(800, [&] {
        if (auto *d = w.findChild<VmSettingsDialog *>()) {
            shot(d, QStringLiteral("17-vmsettings-advanced"));
            d->close();
        }
    });
    step(100, [&] { Theme::instance()->apply(QStringLiteral("modern-light")); });
    step(1500, [&] { shot(&w, QStringLiteral("18-main-modern-light")); });
    step(100, [&] {
        if (Vm *vm = VmLibrary::instance()->findByName(QStringLiteral("Arch Linux")))
            vm->forceStop();
    });
    step(3000, [&] {
        Theme::instance()->apply(QStringLiteral("system"));
        shot(&w, QStringLiteral("19-main-stopped"));
    });
    step(500, [&] {
        for (Vm *vm : VmLibrary::instance()->vms())
            std::printf("vm %-22s state=%s available=%d\n", qPrintable(vm->name()),
                        qPrintable(Vm::stateLabel(vm->state())), vm->isAvailable());
        app.quit();
    });

    runSteps();
    return app.exec();
}
