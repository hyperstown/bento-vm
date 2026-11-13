#include "MainWindow.h"
#include "FlowLayout.h"
#include "NewVmWizard.h"
#include "SettingsDialog.h"
#include "Theme.h"
#include "VmCard.h"
#include "VmSettingsDialog.h"
#include "VmWindow.h"
#include "core/AppSettings.h"
#include "core/HostInfo.h"
#include "core/QemuCommand.h"
#include "core/Vm.h"
#include "core/VmLibrary.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Virtual Machines"));
    setAcceptDrops(true);
    buildActions();
    buildMenus();
    buildToolbar();
    buildCentral();

    const QByteArray geo = AppSettings::instance().value(QStringLiteral("ui/mainGeometry")).toByteArray();
    if (geo.isEmpty() || !restoreGeometry(geo))
        resize(1180, 760);

    VmLibrary *lib = VmLibrary::instance();
    for (Vm *vm : lib->vms())
        addCard(vm);
    connect(lib, &VmLibrary::vmAdded, this, &MainWindow::addCard);
    connect(lib, &VmLibrary::vmRemoved, this, &MainWindow::removeCard);
    connect(Theme::instance(), &Theme::changed, this, &MainWindow::applyThemeLayout);
    applyThemeLayout();
    updateEmptyState();
    updateActions();
}

void MainWindow::buildActions()
{
    m_actNew = new QAction(QIcon::fromTheme(QStringLiteral("list-add")), QStringLiteral("New VM…"), this);
    m_actNew->setShortcut(QKeySequence::New);
    m_actNew->setToolTip(QStringLiteral("Create a virtual machine from an ISO or an existing disk"));
    connect(m_actNew, &QAction::triggered, this, [this] { openWizard(); });

    m_actImportFolder = new QAction(QIcon::fromTheme(QStringLiteral("document-import")), QStringLiteral("Add Existing BentoVM Folder…"), this);
    connect(m_actImportFolder, &QAction::triggered, this, &MainWindow::importFolder);

    m_actStart = new QAction(QIcon::fromTheme(QStringLiteral("media-playback-start")), QStringLiteral("Start"), this);
    connect(m_actStart, &QAction::triggered, this, [this] {
        if (Vm *vm = selectedVm())
            openVm(vm);
    });
    m_actOpen = new QAction(QIcon::fromTheme(QStringLiteral("window-new")), QStringLiteral("Open"), this);
    connect(m_actOpen, &QAction::triggered, this, [this] {
        if (Vm *vm = selectedVm())
            openVm(vm);
    });
    m_actShutdown = new QAction(QIcon::fromTheme(QStringLiteral("system-shutdown")), QStringLiteral("Shut Down"), this);
    connect(m_actShutdown, &QAction::triggered, this, [this] {
        if (Vm *vm = selectedVm())
            vm->shutdown();
    });
    m_actForceOff = new QAction(QIcon::fromTheme(QStringLiteral("process-stop")), QStringLiteral("Force Off"), this);
    connect(m_actForceOff, &QAction::triggered, this, [this] {
        Vm *vm = selectedVm();
        if (vm && QMessageBox::question(this, QStringLiteral("Force off %1?").arg(vm->name()),
                                        QStringLiteral("This is like pulling the power plug. Unsaved work in the VM will be lost."))
                      == QMessageBox::Yes)
            vm->forceStop();
    });
    m_actVmSettings = new QAction(QIcon::fromTheme(QStringLiteral("configure")), QStringLiteral("VM Settings…"), this);
    connect(m_actVmSettings, &QAction::triggered, this, [this] {
        if (Vm *vm = selectedVm())
            showVmSettings(vm);
    });
    m_actDelete = new QAction(QIcon::fromTheme(QStringLiteral("edit-delete")), QStringLiteral("Delete…"), this);
    m_actDelete->setShortcut(QKeySequence::Delete);
    connect(m_actDelete, &QAction::triggered, this, [this] {
        if (Vm *vm = selectedVm())
            deleteVm(vm);
    });
    m_actPrefs = new QAction(QIcon::fromTheme(QStringLiteral("configure")), QStringLiteral("Configure BentoVM…"), this);
    m_actPrefs->setShortcut(QKeySequence::Preferences);
    connect(m_actPrefs, &QAction::triggered, this, [this] { showSettings(); });
    m_actQuit = new QAction(QIcon::fromTheme(QStringLiteral("application-exit")), QStringLiteral("Quit"), this);
    m_actQuit->setShortcut(QKeySequence::Quit);
    connect(m_actQuit, &QAction::triggered, this, &QWidget::close);
    m_actAbout = new QAction(QIcon::fromTheme(QStringLiteral("help-about")), QStringLiteral("About BentoVM"), this);
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    file->addAction(m_actNew);
    file->addAction(m_actImportFolder);
    file->addSeparator();
    file->addAction(m_actQuit);

    m_machineMenu = menuBar()->addMenu(QStringLiteral("&Machine"));
    m_machineMenu->addAction(m_actStart);
    m_machineMenu->addAction(m_actOpen);
    m_machineMenu->addAction(m_actShutdown);
    m_machineMenu->addAction(m_actForceOff);
    m_machineMenu->addSeparator();
    m_machineMenu->addAction(m_actVmSettings);
    m_machineMenu->addAction(m_actDelete);

    QMenu *settings = menuBar()->addMenu(QStringLiteral("&Settings"));
    settings->addAction(m_actPrefs);
    settings->addAction(QIcon::fromTheme(QStringLiteral("drive-harddisk")), QStringLiteral("Storage Locations…"), this,
                        [this] { showSettings(true); });

    QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
    help->addAction(m_actAbout);
    help->addAction(QStringLiteral("About Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::buildToolbar()
{
    QToolBar *tb = addToolBar(QStringLiteral("Main"));
    tb->setObjectName(QStringLiteral("mainToolbar"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->addAction(m_actNew);
    tb->addSeparator();
    tb->addAction(m_actStart);
    tb->addAction(m_actOpen);
    tb->addAction(m_actShutdown);
    tb->addAction(m_actVmSettings);

    auto *spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    m_search = new QLineEdit(tb);
    m_search->setPlaceholderText(QStringLiteral("Search…"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(QIcon::fromTheme(QStringLiteral("search")), QLineEdit::LeadingPosition);
    m_search->setMaximumWidth(260);
    connect(m_search, &QLineEdit::textChanged, this, &MainWindow::applyFilter);
    tb->addWidget(m_search);

    // Application menu for the modern themes (where the menu bar is hidden).
    m_hamburger = new QToolButton(tb);
    m_hamburger->setObjectName(QStringLiteral("hamburger"));
    m_hamburger->setIcon(QIcon::fromTheme(QStringLiteral("application-menu"), QIcon::fromTheme(QStringLiteral("open-menu"))));
    m_hamburger->setPopupMode(QToolButton::InstantPopup);
    m_hamburger->setToolTip(QStringLiteral("Menu"));
    auto *menu = new QMenu(m_hamburger);
    menu->addAction(m_actNew);
    menu->addAction(m_actImportFolder);
    menu->addSeparator();
    menu->addAction(m_actPrefs);
    menu->addAction(QIcon::fromTheme(QStringLiteral("drive-harddisk")), QStringLiteral("Storage Locations…"), this,
                    [this] { showSettings(true); });
    menu->addSeparator();
    menu->addAction(m_actAbout);
    menu->addAction(m_actQuit);
    m_hamburger->setMenu(menu);
    m_hamburgerAction = tb->addWidget(m_hamburger);
}

void MainWindow::buildCentral()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("pageBackground"));
    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // KVM problem banner
    const HostInfo &h = HostInfo::get();
    m_kvmBanner = new QFrame(central);
    m_kvmBanner->setObjectName(QStringLiteral("banner"));
    m_kvmBanner->setFrameShape(QFrame::StyledPanel);
    auto *kb = new QHBoxLayout(m_kvmBanner);
    auto *kIcon = new QLabel(m_kvmBanner);
    kIcon->setPixmap(QIcon::fromTheme(QStringLiteral("dialog-warning")).pixmap(22, 22));
    kb->addWidget(kIcon);
    auto *kText = new QLabel(QStringLiteral("<b>Hardware virtualization isn't available.</b> %1").arg(h.kvmProblem), m_kvmBanner);
    kText->setWordWrap(true);
    kb->addWidget(kText, 1);
    m_kvmBanner->setVisible(!h.kvmUsable);
    auto *bannerWrap = new QHBoxLayout;
    bannerWrap->setContentsMargins(20, 10, 20, 0);
    bannerWrap->addWidget(m_kvmBanner);
    outer->addLayout(bannerWrap);

    m_stack = new QStackedWidget(central);
    m_stack->setObjectName(QStringLiteral("mainStack"));
    outer->addWidget(m_stack, 1);

    // Library page
    auto *library = new QWidget(m_stack);
    auto *ll = new QVBoxLayout(library);
    ll->setContentsMargins(24, 18, 24, 0);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("Virtual Machines"), library);
    title->setObjectName(QStringLiteral("pageTitle"));
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.7);
    tf.setBold(true);
    title->setFont(tf);
    m_count = new QLabel(library);
    m_count->setObjectName(QStringLiteral("mutedLabel"));
    header->addWidget(title);
    header->addSpacing(10);
    header->addWidget(m_count, 0, Qt::AlignBottom);
    header->addStretch();
    ll->addLayout(header);
    ll->addSpacing(8);

    auto *scroll = new QScrollArea(library);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_grid = new QWidget(scroll);
    m_grid->setObjectName(QStringLiteral("vmGrid"));
    m_flow = new FlowLayout(m_grid, 18);
    m_flow->setContentsMargins(0, 6, 0, 24);
    scroll->setWidget(m_grid);
    ll->addWidget(scroll, 1);
    m_stack->addWidget(library);

    // Empty state
    auto *empty = new QWidget(m_stack);
    auto *el = new QVBoxLayout(empty);
    el->addStretch(3);
    auto *icon = new QLabel(empty);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(QApplication::windowIcon().pixmap(128, 128));
    el->addWidget(icon);
    el->addSpacing(12);
    auto *et = new QLabel(QStringLiteral("No virtual machines yet"), empty);
    et->setObjectName(QStringLiteral("emptyTitle"));
    et->setAlignment(Qt::AlignCenter);
    QFont ef = et->font();
    ef.setPointSizeF(ef.pointSizeF() * 1.6);
    ef.setBold(true);
    et->setFont(ef);
    el->addWidget(et);
    auto *es = new QLabel(QStringLiteral("Create one from an installer ISO or a disk image you already have.\n"
                                         "Tip: you can drop an ISO file anywhere on this window."), empty);
    es->setObjectName(QStringLiteral("mutedLabel"));
    es->setAlignment(Qt::AlignCenter);
    el->addWidget(es);
    el->addSpacing(16);
    auto *create = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), QStringLiteral("Create a Virtual Machine"), empty);
    create->setProperty("accent", true);
    create->setDefault(true);
    create->setMinimumHeight(40);
    connect(create, &QPushButton::clicked, this, [this] { openWizard(); });
    auto *row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(create);
    row->addStretch();
    el->addLayout(row);
    el->addStretch(4);
    m_stack->addWidget(empty);

    setCentralWidget(central);
    statusBar()->setSizeGripEnabled(false);
}

void MainWindow::applyThemeLayout()
{
    const bool custom = Theme::instance()->isCustom();
    menuBar()->setVisible(!custom);
    m_hamburgerAction->setVisible(custom);
    m_hamburger->setVisible(custom);
    // A style change re-polishes the toolbar after this runs; enforce again.
    QTimer::singleShot(0, this, [this] {
        const bool c = Theme::instance()->isCustom();
        m_hamburgerAction->setVisible(c);
        m_hamburger->setVisible(c);
        menuBar()->setVisible(!c);
    });
    for (VmCard *c : std::as_const(m_cards))
        c->refresh();
}

// ---------------------------------------------------------------------------
// Cards

void MainWindow::addCard(Vm *vm)
{
    if (m_cards.contains(vm->id()))
        return;
    auto *card = new VmCard(vm, m_grid);
    m_cards.insert(vm->id(), card);
    connect(card, &VmCard::clicked, this, &MainWindow::select);
    connect(card, &VmCard::activated, this, [this](VmCard *c) {
        select(c);
        openVm(c->vm());
    });
    connect(card, &VmCard::contextMenuRequested, this, &MainWindow::showVmMenu);
    connect(card, &VmCard::filesDropped, this, [this](VmCard *c, const QStringList &files) {
        if (!m_windows.value(c->vm()->id()))
            openVm(c->vm());
        if (VmWindow *w = m_windows.value(c->vm()->id()))
            w->sendFiles(files);
    });
    connect(vm, &Vm::stateChanged, this, &MainWindow::updateActions);
    connect(vm, &Vm::availabilityChanged, this, &MainWindow::updateActions);
    connect(vm, &Vm::configChanged, this, &MainWindow::sortCards);
    connect(vm, &Vm::errorOccurred, this, [this, vm](const QString &title, const QString &details) {
        QWidget *parent = m_windows.value(vm->id()) ? static_cast<QWidget *>(m_windows.value(vm->id())) : this;
        QMessageBox box(QMessageBox::Warning, title, details, QMessageBox::Ok, parent);
        box.setDetailedText(QStringLiteral("Log file: %1").arg(vm->logFile()));
        box.exec();
    });
    sortCards();
    updateEmptyState();
}

void MainWindow::removeCard(Vm *vm)
{
    VmCard *card = m_cards.take(vm->id());
    if (!card)
        return;
    m_flow->removeWidget(card);
    card->deleteLater();
    if (m_selectedId == vm->id())
        m_selectedId.clear();
    updateEmptyState();
    updateActions();
}

void MainWindow::sortCards()
{
    QList<VmCard *> cards = m_cards.values();
    std::sort(cards.begin(), cards.end(), [](VmCard *a, VmCard *b) {
        return a->vm()->name().compare(b->vm()->name(), Qt::CaseInsensitive) < 0;
    });
    for (VmCard *c : cards)
        m_flow->removeWidget(c);
    for (VmCard *c : cards)
        m_flow->addWidget(c);
    applyFilter();
    updateEmptyState();
}

void MainWindow::applyFilter()
{
    const QString q = m_search->text().trimmed();
    for (VmCard *c : std::as_const(m_cards)) {
        const VmConfig &cfg = c->vm()->config();
        c->setVisible(q.isEmpty() || cfg.name.contains(q, Qt::CaseInsensitive) || cfg.osLabel.contains(q, Qt::CaseInsensitive)
                      || cfg.osId.contains(q, Qt::CaseInsensitive));
    }
    m_flow->invalidate();
}

void MainWindow::updateEmptyState()
{
    const int n = int(m_cards.size());
    int running = 0;
    for (VmCard *c : std::as_const(m_cards))
        running += c->vm()->isActive() ? 1 : 0;
    m_count->setText(n == 0 ? QString()
                            : QStringLiteral("%1 machine%2%3").arg(n).arg(n == 1 ? "" : "s")
                                  .arg(running ? QStringLiteral(" · %1 running").arg(running) : QString()));
    m_stack->setCurrentIndex(n == 0 ? 1 : 0);
}

void MainWindow::select(VmCard *card)
{
    m_selectedId = card ? card->vm()->id() : QString();
    for (VmCard *c : std::as_const(m_cards))
        c->setSelected(c == card);
    updateActions();
}

Vm *MainWindow::selectedVm() const
{
    VmCard *c = m_cards.value(m_selectedId);
    return c ? c->vm() : nullptr;
}

void MainWindow::updateActions()
{
    Vm *vm = selectedVm();
    const bool active = vm && vm->isActive();
    const bool running = vm && (vm->state() == Vm::State::Running || vm->state() == Vm::State::Paused);
    m_actStart->setVisible(!active);
    m_actStart->setEnabled(vm && vm->isAvailable());
    m_actOpen->setVisible(active);
    m_actOpen->setEnabled(active && vm->config().display == QLatin1String("embedded"));
    m_actShutdown->setEnabled(running);
    m_actForceOff->setEnabled(active);
    m_actVmSettings->setEnabled(vm != nullptr);
    m_actDelete->setEnabled(vm && !active);
    updateEmptyState();
}

void MainWindow::fillVmMenu(QMenu *menu, Vm *vm)
{
    const bool active = vm->isActive();
    if (!vm->isAvailable() && !active) {
        QAction *why = menu->addAction(QIcon::fromTheme(QStringLiteral("dialog-information")), QStringLiteral("Why is it unavailable?"));
        connect(why, &QAction::triggered, this, [this, vm] {
            QMessageBox::information(this, QStringLiteral("%1 is temporarily unavailable").arg(vm->name()), vm->unavailableReason());
        });
        menu->addSeparator();
        menu->addAction(QIcon::fromTheme(QStringLiteral("edit-clear-list")), QStringLiteral("Forget"), this, [this, vm] {
            if (QMessageBox::question(this, QStringLiteral("Forget %1?").arg(vm->name()),
                                      QStringLiteral("BentoVM will stop listing this VM. No files are deleted; you can add the "
                                                     "folder again later with File → Add Existing BentoVM Folder."))
                == QMessageBox::Yes) {
                QString err;
                VmLibrary::instance()->remove(vm, false, &err);
            }
        });
        return;
    }
    if (active) {
        if (vm->config().display == QLatin1String("embedded"))
            menu->addAction(QIcon::fromTheme(QStringLiteral("window-new")), QStringLiteral("Open"), this, [this, vm] { openVm(vm); });
        menu->addAction(QIcon::fromTheme(QStringLiteral("system-shutdown")), QStringLiteral("Shut Down"), vm, &Vm::shutdown);
        menu->addAction(QIcon::fromTheme(QStringLiteral("system-reboot")), QStringLiteral("Reboot"), vm, &Vm::reboot);
        if (vm->state() == Vm::State::Paused)
            menu->addAction(QIcon::fromTheme(QStringLiteral("media-playback-start")), QStringLiteral("Resume"), vm, &Vm::resume);
        else
            menu->addAction(QIcon::fromTheme(QStringLiteral("media-playback-pause")), QStringLiteral("Pause"), vm, &Vm::pause);
        menu->addAction(QIcon::fromTheme(QStringLiteral("process-stop")), QStringLiteral("Force Off"), this, [this, vm] {
            if (QMessageBox::question(this, QStringLiteral("Force off %1?").arg(vm->name()),
                                      QStringLiteral("Unsaved work in the VM will be lost.")) == QMessageBox::Yes)
                vm->forceStop();
        });
    } else {
        menu->addAction(QIcon::fromTheme(QStringLiteral("media-playback-start")), QStringLiteral("Start"), this, [this, vm] { openVm(vm); });
    }
    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(QStringLiteral("configure")), QStringLiteral("Settings…"), this, [this, vm] { showVmSettings(vm); });
    menu->addAction(QIcon::fromTheme(QStringLiteral("document-open-folder")), QStringLiteral("Show in File Manager"), this,
                    [vm] { QDesktopServices::openUrl(QUrl::fromLocalFile(vm->config().dir)); });
    menu->addAction(QIcon::fromTheme(QStringLiteral("text-x-log"), QIcon::fromTheme(QStringLiteral("document-preview"))),
                    QStringLiteral("Open Log"), this, [this, vm] {
                        if (QFileInfo::exists(vm->logFile()))
                            QDesktopServices::openUrl(QUrl::fromLocalFile(vm->logFile()));
                        else
                            QMessageBox::information(this, QStringLiteral("No log yet"), QStringLiteral("The VM hasn't been started yet."));
                    });
    menu->addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), QStringLiteral("Copy QEMU Command"), this, [vm] {
        QApplication::clipboard()->setText(buildQemuCommand(vm->config(), vm->runtimeDir()).toShellString());
    });
    menu->addSeparator();
    QAction *del = menu->addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), QStringLiteral("Delete…"), this,
                                   [this, vm] { deleteVm(vm); });
    del->setEnabled(!active);
}

void MainWindow::showVmMenu(VmCard *card, const QPoint &pos)
{
    QMenu menu(this);
    fillVmMenu(&menu, card->vm());
    menu.exec(pos);
}

// ---------------------------------------------------------------------------
// VM operations

bool MainWindow::startVm(Vm *vm)
{
    QString err;
    if (!vm->start(&err)) {
        QMessageBox::warning(this, QStringLiteral("Can't start %1").arg(vm->name()), err);
        return false;
    }
    if (!vm->lastWarnings().isEmpty())
        statusBar()->showMessage(vm->lastWarnings().join(QStringLiteral("  ·  ")), 10000);
    return true;
}

void MainWindow::openVm(Vm *vm)
{
    if (!vm->isActive() && !vm->isAvailable()) {
        QMessageBox::information(this, QStringLiteral("%1 is temporarily unavailable").arg(vm->name()), vm->unavailableReason());
        return;
    }
    if (vm->config().display != QLatin1String("embedded")) {
        if (!vm->isActive())
            startVm(vm);
        else
            statusBar()->showMessage(QStringLiteral("%1 is shown in its own QEMU window.").arg(vm->name()), 5000);
        return;
    }
    VmWindow *w = m_windows.value(vm->id());
    if (!w) {
        w = new VmWindow(vm);
        connect(w, &VmWindow::settingsRequested, this, &MainWindow::showVmSettings);
        m_windows.insert(vm->id(), w);
    }
    if (!vm->isActive() && !startVm(vm))
        return;
    w->show();
    w->raise();
    w->activateWindow();
}

void MainWindow::showVmSettings(Vm *vm, const QString &page)
{
    QWidget *parent = m_windows.value(vm->id()) ? static_cast<QWidget *>(m_windows.value(vm->id())) : this;
    VmSettingsDialog dlg(vm, parent);
    dlg.showPage(page);
    dlg.exec();
}

void MainWindow::deleteVm(Vm *vm)
{
    if (vm->isActive())
        return;
    const VmConfig &c = vm->config();
    QStringList outside;
    for (int i = 0; i < c.disks.size(); ++i)
        if (!c.diskPath(i).startsWith(c.dir + QLatin1Char('/')))
            outside << c.diskPath(i);

    QMessageBox box(QMessageBox::Warning, QStringLiteral("Delete %1?").arg(c.name),
                    QStringLiteral("This permanently deletes the VM folder and everything in it:\n%1").arg(c.dir),
                    QMessageBox::NoButton, this);
    if (!outside.isEmpty())
        box.setInformativeText(QStringLiteral("Disk images outside the folder are kept:\n%1").arg(outside.join(QLatin1Char('\n'))));
    QPushButton *del = box.addButton(QStringLiteral("Delete"), QMessageBox::DestructiveRole);
    del->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
    box.addButton(QMessageBox::Cancel);
    auto *keep = new QCheckBox(QStringLiteral("Keep the files, only remove it from BentoVM"), &box);
    box.setCheckBox(keep);
    box.exec();
    if (box.clickedButton() != del)
        return;
    QString err;
    if (!VmLibrary::instance()->remove(vm, !keep->isChecked(), &err))
        QMessageBox::warning(this, QStringLiteral("Couldn't delete"), err);
}

void MainWindow::openWizard(const QString &source)
{
    auto *wizard = new NewVmWizard(this);
    wizard->setAttribute(Qt::WA_DeleteOnClose);
    if (!source.isEmpty())
        wizard->setSourcePath(source);
    connect(wizard, &NewVmWizard::vmCreated, this, [this](Vm *vm, bool startNow) {
        if (VmCard *c = m_cards.value(vm->id()))
            select(c);
        if (startNow)
            QTimer::singleShot(0, this, [this, vm] { openVm(vm); });
    });
    wizard->open();
}

void MainWindow::showSettings(bool storagePage)
{
    SettingsDialog dlg(this);
    if (storagePage)
        dlg.showStoragePage();
    dlg.exec();
    VmLibrary::instance()->refresh();
}

void MainWindow::importFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose a BentoVM folder (contains vm.json)"),
                                                          AppSettings::instance().defaultLocation());
    if (dir.isEmpty())
        return;
    QString err;
    if (!VmLibrary::instance()->importFolder(dir, &err))
        QMessageBox::warning(this, QStringLiteral("Couldn't add the VM"), err);
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, QStringLiteral("About BentoVM"),
                       QStringLiteral("<h3>BentoVM %1</h3>"
                                      "<p>A friendly QEMU/KVM front end with first-class support for "
                                      "<b>Venus</b> (Vulkan) and VirGL GPU acceleration.</p>"
                                      "<p>VMs run as your user, no root or libvirt needed.</p>")
                           .arg(QApplication::applicationVersion()));
}

// ---------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent *e)
{
    AppSettings::instance().setValue(QStringLiteral("ui/mainGeometry"), saveGeometry());

    bool vmWindowsOpen = false;
    for (const auto &w : std::as_const(m_windows))
        vmWindowsOpen |= w && w->isVisible();
    QStringList running;
    for (Vm *vm : VmLibrary::instance()->vms())
        if (vm->isActive())
            running << vm->name();

    if (!vmWindowsOpen && !running.isEmpty()
        && AppSettings::instance().value(QStringLiteral("ui/quitAction"), QStringLiteral("ask")).toString() == QLatin1String("ask")) {
        QMessageBox box(QMessageBox::Question, QStringLiteral("VMs are still running"),
                        QStringLiteral("%1 will keep running in the background. Open BentoVM again to get back to %2.")
                            .arg(running.join(QStringLiteral(", ")), running.size() == 1 ? QStringLiteral("it") : QStringLiteral("them")),
                        QMessageBox::NoButton, this);
        QPushButton *keep = box.addButton(QStringLiteral("Keep Running"), QMessageBox::AcceptRole);
        QPushButton *shut = box.addButton(QStringLiteral("Shut Them Down"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        auto *remember = new QCheckBox(QStringLiteral("Don't ask again"), &box);
        box.setCheckBox(remember);
        box.exec();
        if (box.clickedButton() == shut) {
            for (Vm *vm : VmLibrary::instance()->vms())
                if (vm->isActive())
                    vm->shutdown();
        } else if (box.clickedButton() != keep) {
            e->ignore();
            return;
        }
        if (remember->isChecked())
            AppSettings::instance().setValue(QStringLiteral("ui/quitAction"), QStringLiteral("keep"));
    }
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    for (const QUrl &u : e->mimeData()->urls()) {
        if (u.isLocalFile()) {
            openWizard(u.toLocalFile());
            return;
        }
    }
}
