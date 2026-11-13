#include "VmWindow.h"
#include "OsIcon.h"
#include "Theme.h"
#include "agent/VdAgent.h"
#include "core/AppSettings.h"
#include "core/OsDb.h"
#include "core/QmpClient.h"
#include "core/UsbDevices.h"
#include "core/VmLibrary.h"
#include "core/Vm.h"
#include "display/DBusDisplay.h"
#include "display/DisplayWidget.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr uint32_t kCtrl = 0x1d, kAlt = 0x38, kDel = 0xd3, kBackspace = 0x0e, kPrint = 0xb7, kMeta = 0xdb;
constexpr uint32_t kF[] = {0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58};

QPushButton *accentButton(const QString &text, const QIcon &icon, QWidget *parent)
{
    auto *b = new QPushButton(icon, text, parent);
    b->setProperty("accent", true);
    b->setDefault(true);
    b->setMinimumHeight(38);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QLabel *centered(const QString &text, QWidget *parent, const char *objName = nullptr)
{
    auto *l = new QLabel(text, parent);
    l->setAlignment(Qt::AlignCenter);
    l->setWordWrap(true);
    if (objName)
        l->setObjectName(QLatin1String(objName));
    return l;
}
} // namespace

VmWindow::VmWindow(Vm *vm, QWidget *parent)
    : QMainWindow(parent)
    , m_vm(vm)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(vm->name());
    setWindowIcon(OsIcon::icon(vm->config().osId));
    buildUi();

    const QByteArray geo = AppSettings::instance().value(QStringLiteral("vmwindow/%1/geometry").arg(vm->id())).toByteArray();
    if (geo.isEmpty() || !restoreGeometry(geo))
        resize(1300, 860);

    connect(vm, &Vm::stateChanged, this, &VmWindow::updateState);
    connect(vm, &Vm::configChanged, this, [this] {
        setWindowTitle(m_vm->name());
        updateClipboardButton();
        updateMediaMenu();
        if (m_display)
            m_display->setAutoResize(m_vm->config().autoResize);
        updateState();
    });
    connect(vm, &Vm::usbChanged, this, &VmWindow::updateUsbButton);
    connect(vm, &Vm::qmpReady, this, [this] {
        if (m_vm->config().display == QLatin1String("embedded"))
            connectDisplay();
    });

    VdAgent *agent = vm->agent();
    connect(agent, &VdAgent::guestConnectedChanged, this, &VmWindow::updateAgentStatus);
    connect(agent, &VdAgent::notice, this, [this](const QString &t) { showToast(t, 6000); });
    connect(agent, &VdAgent::transferProgress, this, [this](qint64 sent, qint64 total, int left) {
        if (left == 0)
            return;
        const int pct = total > 0 ? int(sent * 100 / total) : 0;
        showToast(QStringLiteral("Copying %1 file%2 into the VM…").arg(left).arg(left == 1 ? "" : "s"), 0, true);
        m_toastProgress->setValue(pct);
    });
    connect(agent, &VdAgent::transferFinished, this, [this](const QString &name, bool ok, const QString &msg) {
        if (m_vm->agent()->activeTransfers() > 0 && ok)
            return;
        if (ok)
            showToast(QStringLiteral("“%1” was copied to the VM (usually into ~/Downloads).").arg(name), 5000);
        else
            showToast(QStringLiteral("Copying “%1” failed: %2").arg(name, msg), 8000);
    });

    m_thumbTimer = new QTimer(this);
    m_thumbTimer->setInterval(30000);
    connect(m_thumbTimer, &QTimer::timeout, this, &VmWindow::saveThumbnail);
    m_thumbTimer->start();

    updateState();
    updateAgentStatus();
    if (vm->state() == Vm::State::Running || vm->state() == Vm::State::Paused)
        QTimer::singleShot(0, this, &VmWindow::connectDisplay);
}

VmWindow::~VmWindow()
{
    disconnectDisplay();
}

void VmWindow::buildUi()
{
    buildToolbar();

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("pageBackground"));
    auto *lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(central);
    lay->addWidget(m_stack);

    m_offPage = buildOffPage();
    m_waitPage = buildWaitPage();
    m_externalPage = buildExternalPage();
    m_display = new DisplayWidget(m_stack);
    m_display->setAutoResize(m_vm->config().autoResize);
    m_stack->addWidget(m_offPage);
    m_stack->addWidget(m_waitPage);
    m_stack->addWidget(m_externalPage);
    m_stack->addWidget(m_display);

    connect(m_display, &DisplayWidget::filesDropped, this, &VmWindow::sendFiles);
    connect(m_display, &DisplayWidget::toggleFullscreenRequested, this, &VmWindow::toggleFullscreen);
    connect(m_display, &DisplayWidget::releaseKeyboardRequested, this, [this] {
        m_toolbar->setFocus();
        showToast(QStringLiteral("Keyboard released. Click into the VM to capture it again."), 3000);
    });
    connect(m_display, &DisplayWidget::renderError, this, [this](const QString &e) { showToast(e, 10000); });

    // Toast overlay
    m_toast = new QFrame(central);
    m_toast->setObjectName(QStringLiteral("toast"));
    m_toast->setStyleSheet(QStringLiteral(
        "QFrame#toast { background: palette(window); border: 1px solid palette(mid); border-radius: 14px; }"));
    auto *tl = new QHBoxLayout(m_toast);
    tl->setContentsMargins(16, 10, 10, 10);
    m_toastLabel = new QLabel(m_toast);
    m_toastLabel->setWordWrap(true);
    m_toastProgress = new QProgressBar(m_toast);
    m_toastProgress->setRange(0, 100);
    m_toastProgress->setFixedWidth(160);
    m_toastProgress->setTextVisible(false);
    m_toastCancel = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-cancel")), QStringLiteral("Cancel"), m_toast);
    connect(m_toastCancel, &QPushButton::clicked, this, [this] {
        m_vm->agent()->cancelTransfers();
        m_toast->hide();
    });
    tl->addWidget(m_toastLabel, 1);
    tl->addWidget(m_toastProgress);
    tl->addWidget(m_toastCancel);
    m_toast->hide();
    m_toastTimer = new QTimer(this);
    m_toastTimer->setSingleShot(true);
    connect(m_toastTimer, &QTimer::timeout, m_toast, &QWidget::hide);

    setCentralWidget(central);
}

void VmWindow::buildToolbar()
{
    m_toolbar = addToolBar(QStringLiteral("VM"));
    m_toolbar->setObjectName(QStringLiteral("vmToolbar"));
    m_toolbar->setMovable(false);
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toolbar->setIconSize(QSize(18, 18));

    m_actStart = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("media-playback-start")), QStringLiteral("Start"));
    connect(m_actStart, &QAction::triggered, this, &VmWindow::startVm);

    // Shut down with a drop-down for the harsher options.
    auto *powerMenu = new QMenu(this);
    powerMenu->addAction(QIcon::fromTheme(QStringLiteral("system-reboot")), QStringLiteral("Reboot"), this, [this] { m_vm->reboot(); });
    powerMenu->addAction(QIcon::fromTheme(QStringLiteral("process-stop")), QStringLiteral("Force Off"), this, [this] {
        if (QMessageBox::question(this, QStringLiteral("Force off?"),
                                  QStringLiteral("This is like pulling the power plug. Unsaved work in the VM will be lost."))
            == QMessageBox::Yes)
            m_vm->forceStop();
    });
    m_actShutdown = new QAction(QIcon::fromTheme(QStringLiteral("system-shutdown")), QStringLiteral("Shut Down"), this);
    m_actShutdown->setMenu(powerMenu);
    connect(m_actShutdown, &QAction::triggered, this, [this] { m_vm->shutdown(); });
    m_toolbar->addAction(m_actShutdown);
    if (auto *b = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(m_actShutdown)))
        b->setPopupMode(QToolButton::MenuButtonPopup);

    m_actPause = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("media-playback-pause")), QStringLiteral("Pause"));
    connect(m_actPause, &QAction::triggered, this, [this] {
        if (m_vm->state() == Vm::State::Paused)
            m_vm->resume();
        else
            m_vm->pause();
    });
    m_toolbar->addSeparator();

    // Send keys
    auto *keysMenu = new QMenu(this);
    keysMenu->addAction(QStringLiteral("Ctrl+Alt+Del"), this, [this] { sendCombo({kCtrl, kAlt, kDel}); });
    keysMenu->addAction(QStringLiteral("Ctrl+Alt+Backspace"), this, [this] { sendCombo({kCtrl, kAlt, kBackspace}); });
    keysMenu->addAction(QStringLiteral("Super (Meta)"), this, [this] { sendCombo({kMeta}); });
    keysMenu->addAction(QStringLiteral("Print Screen"), this, [this] { sendCombo({kPrint}); });
    keysMenu->addSeparator();
    for (int i = 0; i < 12; ++i)
        keysMenu->addAction(QStringLiteral("Ctrl+Alt+F%1").arg(i + 1), this, [this, i] { sendCombo({kCtrl, kAlt, kF[i]}); });
    m_actKeys = new QAction(QIcon::fromTheme(QStringLiteral("input-keyboard")), QStringLiteral("Keys"), this);
    m_actKeys->setMenu(keysMenu);
    m_actKeys->setToolTip(QStringLiteral("Send key combinations the host would otherwise catch.\n"
                                         "Ctrl+Alt+F toggles full screen, Ctrl+Alt+G releases the keyboard."));
    m_toolbar->addAction(m_actKeys);
    if (auto *b = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(m_actKeys)))
        b->setPopupMode(QToolButton::InstantPopup);

    // Clipboard direction
    auto *clipMenu = new QMenu(this);
    m_clipGroup = new QActionGroup(this);
    const std::pair<const char *, const char *> modes[] = {
        {"both", "Both directions"}, {"host-to-guest", "Host → Guest only"},
        {"guest-to-host", "Guest → Host only"}, {"off", "Disabled"}};
    for (const auto &[id, label] : modes) {
        QAction *a = clipMenu->addAction(QString::fromUtf8(label));
        a->setCheckable(true);
        a->setData(QString::fromLatin1(id));
        m_clipGroup->addAction(a);
    }
    connect(m_clipGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        VmConfig c = m_vm->config();
        c.clipboard = a->data().toString();
        QString err;
        if (!m_vm->updateConfig(c, &err))
            QMessageBox::warning(this, QStringLiteral("Couldn't save"), err);
        else if (c.display != QLatin1String("embedded"))
            showToast(QStringLiteral("With the QEMU window display the clipboard is either both ways or off; "
                                     "the change applies after a restart."), 7000);
    });
    m_actClipboard = new QAction(QIcon::fromTheme(QStringLiteral("edit-paste")), QStringLiteral("Clipboard"), this);
    m_actClipboard->setMenu(clipMenu);
    m_toolbar->addAction(m_actClipboard);
    m_clipButton = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(m_actClipboard));
    if (m_clipButton)
        m_clipButton->setPopupMode(QToolButton::InstantPopup);

    m_actSendFiles = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("document-send"),
                                                           QIcon::fromTheme(QStringLiteral("document-open"))),
                                          QStringLiteral("Send Files…"));
    m_actSendFiles->setToolTip(QStringLiteral("Copy files into the VM. You can also drag & drop files onto the screen."));
    connect(m_actSendFiles, &QAction::triggered, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Send files to %1").arg(m_vm->name()),
                                                                 QDir::homePath());
        if (!files.isEmpty())
            sendFiles(files);
    });

    // Removable media
    m_actMedia = new QAction(QIcon::fromTheme(QStringLiteral("media-optical")), QStringLiteral("Media"), this);
    m_actMedia->setMenu(new QMenu(this));
    m_toolbar->addAction(m_actMedia);
    m_mediaButton = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(m_actMedia));
    if (m_mediaButton)
        m_mediaButton->setPopupMode(QToolButton::InstantPopup);
    updateMediaMenu();

    // USB passthrough: the list is rebuilt every time the menu opens.
    m_actUsb = new QAction(QIcon::fromTheme(QStringLiteral("drive-removable-media-usb"),
                                            QIcon::fromTheme(QStringLiteral("drive-removable-media"))),
                           QStringLiteral("USB"), this);
    auto *usbMenu = new QMenu(this);
    usbMenu->setToolTipsVisible(true);
    connect(usbMenu, &QMenu::aboutToShow, this, &VmWindow::rebuildUsbMenu);
    m_actUsb->setMenu(usbMenu);
    m_actUsb->setToolTip(QStringLiteral("Plug host USB devices (controllers, drives…) into the VM"));
    m_toolbar->addAction(m_actUsb);
    if (auto *b = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(m_actUsb)))
        b->setPopupMode(QToolButton::InstantPopup);

    auto *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);

    m_agentStatus = new QLabel(this);
    m_agentStatus->setTextFormat(Qt::RichText);
    m_agentStatus->setContentsMargins(8, 0, 8, 0);
    m_toolbar->addWidget(m_agentStatus);

    m_actScreenshot = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("camera-photo"),
                                                            QIcon::fromTheme(QStringLiteral("image-x-generic"))),
                                           QStringLiteral("Screenshot"));
    connect(m_actScreenshot, &QAction::triggered, this, &VmWindow::takeScreenshot);
    m_actFullscreen = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("view-fullscreen")), QStringLiteral("Full Screen"));
    m_actFullscreen->setToolTip(QStringLiteral("Full screen (Ctrl+Alt+F inside the VM)"));
    connect(m_actFullscreen, &QAction::triggered, this, &VmWindow::toggleFullscreen);
    QAction *settings = m_toolbar->addAction(QIcon::fromTheme(QStringLiteral("configure")), QStringLiteral("Settings"));
    connect(settings, &QAction::triggered, this, [this] { Q_EMIT settingsRequested(m_vm); });

    // Icon-only for the less important buttons keeps the bar compact.
    for (QAction *a : {m_actScreenshot, m_actFullscreen, settings}) {
        if (auto *b = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(a)))
            b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    updateClipboardButton();
}

QWidget *VmWindow::buildOffPage()
{
    auto *page = new QFrame(this);
    page->setObjectName(QStringLiteral("infoPage"));
    auto *lay = new QVBoxLayout(page);
    lay->addStretch(3);
    m_offBadge = new QLabel(page);
    m_offBadge->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_offBadge);
    lay->addSpacing(10);
    m_offTitle = centered(QString(), page, "emptyTitle");
    QFont f = m_offTitle->font();
    f.setPointSizeF(f.pointSizeF() * 1.6);
    f.setBold(true);
    m_offTitle->setFont(f);
    lay->addWidget(m_offTitle);
    m_offSpecs = centered(QString(), page, "mutedLabel");
    lay->addWidget(m_offSpecs);
    lay->addSpacing(16);
    auto *start = accentButton(QStringLiteral("Start"), QIcon::fromTheme(QStringLiteral("media-playback-start")), page);
    start->setMinimumWidth(180);
    connect(start, &QPushButton::clicked, this, &VmWindow::startVm);
    auto *settings = new QPushButton(QIcon::fromTheme(QStringLiteral("configure")), QStringLiteral("Settings"), page);
    connect(settings, &QPushButton::clicked, this, [this] { Q_EMIT settingsRequested(m_vm); });
    auto *row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(start);
    row->addWidget(settings);
    row->addStretch();
    lay->addLayout(row);
    lay->addStretch(4);
    return page;
}

QWidget *VmWindow::buildWaitPage()
{
    auto *page = new QFrame(this);
    page->setObjectName(QStringLiteral("infoPage"));
    auto *lay = new QVBoxLayout(page);
    lay->addStretch();
    m_waitTitle = centered(QStringLiteral("Starting…"), page, "emptyTitle");
    QFont f = m_waitTitle->font();
    f.setPointSizeF(f.pointSizeF() * 1.4);
    f.setBold(true);
    m_waitTitle->setFont(f);
    m_waitDetail = centered(QString(), page, "mutedLabel");
    m_waitDetail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_retryButton = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), QStringLiteral("Try Again"), page);
    connect(m_retryButton, &QPushButton::clicked, this, [this] {
        m_displayRetries = 0;
        connectDisplay();
    });
    lay->addWidget(m_waitTitle);
    lay->addWidget(m_waitDetail);
    lay->addSpacing(12);
    lay->addWidget(m_retryButton, 0, Qt::AlignHCenter);
    lay->addStretch();
    return page;
}

QWidget *VmWindow::buildExternalPage()
{
    auto *page = new QFrame(this);
    page->setObjectName(QStringLiteral("infoPage"));
    auto *lay = new QVBoxLayout(page);
    lay->addStretch();
    auto *title = centered(QStringLiteral("Running in a QEMU window"), page, "emptyTitle");
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 1.4);
    f.setBold(true);
    title->setFont(f);
    lay->addWidget(title);
    lay->addWidget(centered(QStringLiteral("This VM uses QEMU's own display window (set in Settings → Display). "
                                           "Clipboard direction control and file drag & drop need the built-in display."),
                            page, "mutedLabel"));
    lay->addStretch();
    return page;
}

void VmWindow::updateState()
{
    if (!m_vm)
        return;
    const Vm::State s = m_vm->state();
    const bool embedded = m_vm->config().display == QLatin1String("embedded");
    const bool active = m_vm->isActive();

    m_actStart->setVisible(!active);
    m_actStart->setEnabled(m_vm->isAvailable());
    m_actShutdown->setVisible(active);
    m_actShutdown->setEnabled(s == Vm::State::Running || s == Vm::State::Paused);
    m_actPause->setVisible(active);
    m_actPause->setEnabled(s == Vm::State::Running || s == Vm::State::Paused);
    m_actPause->setText(s == Vm::State::Paused ? QStringLiteral("Resume") : QStringLiteral("Pause"));
    m_actPause->setIcon(QIcon::fromTheme(s == Vm::State::Paused ? QStringLiteral("media-playback-start")
                                                                : QStringLiteral("media-playback-pause")));
    const bool live = embedded && s == Vm::State::Running;
    m_actKeys->setEnabled(live);
    m_actSendFiles->setEnabled(live && m_vm->config().fileTransfer);
    m_actScreenshot->setEnabled(live);
    m_actUsb->setEnabled(s == Vm::State::Running || s == Vm::State::Paused);
    updateUsbButton();

    if (!active) {
        disconnectDisplay();
        const VmConfig &c = m_vm->config();
        m_offBadge->setPixmap(OsIcon::badge(c.osId, 96, devicePixelRatioF()));
        m_offTitle->setText(c.name);
        if (!m_vm->isAvailable()) {
            m_offSpecs->setText(m_vm->unavailableReason());
        } else {
            m_offSpecs->setText(QStringLiteral("%1 · %2 vCPU · %3 GiB RAM · %4\nPowered off")
                                    .arg(c.osLabel.isEmpty() ? OsDb::profile(c.osId).name : c.osLabel)
                                    .arg(c.vcpus())
                                    .arg(c.memoryMiB / 1024.0, 0, 'f', c.memoryMiB % 1024 ? 1 : 0)
                                    .arg(graphicsModeLabel(c.graphics)));
        }
        m_stack->setCurrentWidget(m_offPage);
        if (isFullScreen())
            toggleFullscreen();
        return;
    }
    if (!embedded) {
        m_stack->setCurrentWidget(m_externalPage);
        return;
    }
    if (m_dbus && m_dbus->isConnected()) {
        m_stack->setCurrentWidget(m_display);
        return;
    }
    if (m_stack->currentWidget() != m_waitPage || m_retryButton->isHidden()) {
        m_waitTitle->setText(s == Vm::State::Starting ? QStringLiteral("Starting %1…").arg(m_vm->name())
                                                      : QStringLiteral("Connecting to the display…"));
        m_waitDetail->setText(QString());
        m_retryButton->hide();
        m_stack->setCurrentWidget(m_waitPage);
    }
}

void VmWindow::updateAgentStatus()
{
    const bool on = m_vm && m_vm->agent()->isGuestConnected();
    const QColor c = Theme::stateColor(on ? QStringLiteral("running") : QStringLiteral("stopped"));
    m_agentStatus->setText(QStringLiteral("<span style='color:%1'>●</span> Guest tools").arg(c.name()));
    m_agentStatus->setToolTip(on ? QStringLiteral("spice-vdagent is running in the guest: clipboard sharing and file drop are available.")
                                 : QStringLiteral("No guest agent detected. Install spice-vdagent in the VM (Windows: virtio-win "
                                                  "guest tools) for clipboard sharing and file drag & drop."));
    m_agentStatus->setVisible(m_vm && m_vm->config().display == QLatin1String("embedded"));
}

void VmWindow::updateClipboardButton()
{
    if (!m_vm)
        return;
    const QString mode = m_vm->config().clipboard;
    for (QAction *a : m_clipGroup->actions())
        a->setChecked(a->data().toString() == mode);
    const QString label = mode == QLatin1String("both")            ? QStringLiteral("Clipboard: ⇄")
                          : mode == QLatin1String("host-to-guest") ? QStringLiteral("Clipboard: → VM")
                          : mode == QLatin1String("guest-to-host") ? QStringLiteral("Clipboard: → Host")
                                                                   : QStringLiteral("Clipboard: Off");
    m_actClipboard->setText(label);
    m_actClipboard->setToolTip(QStringLiteral("Clipboard sharing: %1").arg(clipboardModeLabel(mode)));
}

void VmWindow::updateMediaMenu()
{
    QMenu *menu = m_actMedia->menu();
    menu->clear();
    const QStringList cds = m_vm->config().cdroms;
    const QString current = cds.value(0);
    QAction *info = menu->addAction(current.isEmpty() ? QStringLiteral("No disc inserted")
                                                      : QStringLiteral("Inserted: %1").arg(QFileInfo(current).fileName()));
    info->setEnabled(false);
    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(QStringLiteral("document-open")), QStringLiteral("Insert ISO Image…"), this, [this] {
        const QString iso = QFileDialog::getOpenFileName(this, QStringLiteral("Choose a disc image"), QDir::homePath(),
                                                         QStringLiteral("Disc images (*.iso *.img);;All files (*)"));
        if (!iso.isEmpty())
            m_vm->changeCdrom(0, iso);
    });
    QAction *eject = menu->addAction(QIcon::fromTheme(QStringLiteral("media-eject")), QStringLiteral("Eject"), this,
                                     [this] { m_vm->changeCdrom(0, QString()); });
    eject->setEnabled(!current.isEmpty());
}

// ---------------------------------------------------------------------------

void VmWindow::startVm()
{
    QString err;
    if (!m_vm->start(&err)) {
        QMessageBox::warning(this, QStringLiteral("Can't start %1").arg(m_vm->name()), err);
        return;
    }
    for (const QString &w : m_vm->lastWarnings())
        showToast(w, 6000);
    updateState();
}

void VmWindow::connectDisplay()
{
    if (!m_vm || m_vm->config().display != QLatin1String("embedded"))
        return;
    if (!m_vm->qmp() || !m_vm->qmp()->isReady())
        return;
    if (m_dbus && m_dbus->isConnected())
        return;
    disconnectDisplay();
    m_dbus = new DBusDisplay(m_vm->qmp(), this);
    m_display->attach(m_dbus);
    m_display->setAcceptFileDrops(m_vm->config().fileTransfer);
    connect(m_dbus, &DBusDisplay::connected, this, [this] {
        m_displayRetries = 0;
        m_stack->setCurrentWidget(m_display);
        m_display->setFocus();
        updateState();
    });
    connect(m_dbus, &DBusDisplay::failed, this, &VmWindow::onDisplayFailed);
    connect(m_dbus, &DBusDisplay::disconnected, this, [this] {
        if (m_vm && m_vm->isActive())
            QTimer::singleShot(1000, this, &VmWindow::connectDisplay);
        updateState();
    });
    m_dbus->start();
    updateState();
}

void VmWindow::disconnectDisplay()
{
    if (!m_dbus)
        return;
    m_display->detach();
    m_dbus->stop();
    m_dbus->deleteLater();
    m_dbus = nullptr;
}

void VmWindow::onDisplayFailed(const QString &error)
{
    if (!m_vm || !m_vm->isActive())
        return;
    if (++m_displayRetries <= 3) {
        QTimer::singleShot(800 * m_displayRetries, this, &VmWindow::connectDisplay);
        return;
    }
    m_waitTitle->setText(QStringLiteral("Couldn't connect to the display"));
    m_waitDetail->setText(error + QStringLiteral("\n\nIf this keeps happening, switch the VM to “QEMU window” "
                                                 "in Settings → Display."));
    m_retryButton->show();
    m_stack->setCurrentWidget(m_waitPage);
}

void VmWindow::sendFiles(const QStringList &files)
{
    if (!m_vm)
        return;
    if (!m_vm->config().fileTransfer) {
        showToast(QStringLiteral("File transfer is disabled for this VM (Settings → Integration)."));
        return;
    }
    m_vm->agent()->sendFiles(files);
}

void VmWindow::sendCombo(const QList<uint32_t> &qnums)
{
    m_display->sendKeyCombo(qnums);
    m_display->setFocus();
}

void VmWindow::takeScreenshot()
{
    const QImage img = m_display->grabGuestFrame();
    if (img.isNull())
        return;
    QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (QDir(dir + QStringLiteral("/Screenshots")).exists())
        dir += QStringLiteral("/Screenshots");
    const QString file = QStringLiteral("%1/%2 %3.png")
                             .arg(dir, m_vm->name(), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh-mm-ss")));
    if (img.save(file))
        showToast(QStringLiteral("Screenshot saved to %1").arg(file));
    else
        showToast(QStringLiteral("Couldn't save the screenshot to %1").arg(dir));
}

void VmWindow::saveThumbnail()
{
    if (!m_vm || m_stack->currentWidget() != m_display || !m_display->hasFrame() || !isVisible())
        return;
    const QImage img = m_display->grabGuestFrame();
    if (!img.isNull())
        m_vm->setThumbnail(img);
}

void VmWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        showNormal();
        m_toolbar->show();
    } else {
        m_toolbar->hide();
        showFullScreen();
        showToast(QStringLiteral("Press Ctrl+Alt+F to leave full screen"), 3000);
    }
    m_display->setFocus();
}

void VmWindow::showToast(const QString &text, int ms, bool progress)
{
    m_toastLabel->setText(text);
    m_toastProgress->setVisible(progress);
    m_toastCancel->setVisible(progress);
    m_toast->setMaximumWidth(qMin(720, width() - 40));
    m_toast->adjustSize();
    positionToast();
    m_toast->show();
    m_toast->raise();
    if (ms > 0)
        m_toastTimer->start(ms);
    else
        m_toastTimer->stop();
}

void VmWindow::positionToast()
{
    if (!m_toast || !centralWidget())
        return;
    const QSize s = m_toast->sizeHint().boundedTo(QSize(qMin(720, width() - 40), 400));
    m_toast->resize(s);
    m_toast->move((centralWidget()->width() - s.width()) / 2, centralWidget()->height() - s.height() - 24);
}

void VmWindow::resizeEvent(QResizeEvent *e)
{
    QMainWindow::resizeEvent(e);
    positionToast();
}

void VmWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange && !isFullScreen())
        m_toolbar->show();
}

void VmWindow::closeEvent(QCloseEvent *e)
{
    if (m_vm && !isFullScreen())
        AppSettings::instance().setValue(QStringLiteral("vmwindow/%1/geometry").arg(m_vm->id()), saveGeometry());
    saveThumbnail();

    if (m_vm && m_vm->isActive() && !m_closingForGood) {
        QString action = AppSettings::instance().closeAction();
        if (action == QLatin1String("ask")) {
            QMessageBox box(QMessageBox::Question, QStringLiteral("%1 is still running").arg(m_vm->name()),
                            QStringLiteral("Do you want to keep the VM running in the background or shut it down?"),
                            QMessageBox::NoButton, this);
            QPushButton *keep = box.addButton(QStringLiteral("Keep Running"), QMessageBox::AcceptRole);
            QPushButton *shut = box.addButton(QStringLiteral("Shut Down"), QMessageBox::DestructiveRole);
            box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(keep);
            auto *remember = new QCheckBox(QStringLiteral("Remember my choice"), &box);
            box.setCheckBox(remember);
            box.exec();
            if (box.clickedButton() == keep)
                action = QStringLiteral("keep");
            else if (box.clickedButton() == shut)
                action = QStringLiteral("shutdown");
            else {
                e->ignore();
                return;
            }
            if (remember->isChecked())
                AppSettings::instance().setCloseAction(action);
        }
        if (action == QLatin1String("shutdown"))
            m_vm->shutdown();
    }
    disconnectDisplay();
    e->accept();
}

// ---------------------------------------------------------------------------
// USB passthrough

void VmWindow::updateUsbButton()
{
    if (!m_vm || !m_actUsb)
        return;
    const int n = m_vm->isActive() ? int(m_vm->usbAttachments().size()) : 0;
    m_actUsb->setText(n ? QStringLiteral("USB (%1)").arg(n) : QStringLiteral("USB"));
}

void VmWindow::rebuildUsbMenu()
{
    QMenu *menu = m_actUsb->menu();
    menu->clear();
    if (!m_vm)
        return;
    menu->addSection(QStringLiteral("Plug into %1").arg(m_vm->name()));

    const QList<UsbDevice> devices = Usb::enumerate();
    for (const UsbDevice &d : devices) {
        QAction *a = menu->addAction(d.icon(), QStringLiteral("%1  (%2)").arg(d.displayName(), d.usbId()));
        a->setCheckable(true);
        const UsbAttachment *att = m_vm->usbAttachmentFor(d);
        a->setChecked(att != nullptr);

        Vm *owner = nullptr;
        for (Vm *other : VmLibrary::instance()->vms())
            if (other != m_vm && other->isActive() && other->usbAttachmentFor(d))
                owner = other;
        if (owner && !att) {
            a->setEnabled(false);
            a->setText(a->text() + QStringLiteral(" — used by %1").arg(owner->name()));
        } else if (!att && !d.accessible()) {
            a->setToolTip(Usb::aclHelperPath().isEmpty()
                              ? QStringLiteral("Asks for your administrator password to allow access.")
                              : QStringLiteral("Access is granted only while it's connected to the VM."));
        }
        if (att) {
            const QString id = att->qdevId;
            const bool isAuto = att->autoConnect();
            const QString name = d.displayName();
            connect(a, &QAction::triggered, this, [this, id, name, isAuto] { disconnectUsb(id, name, isAuto); });
        } else {
            connect(a, &QAction::triggered, this, [this, d] { connectUsb(d); });
        }
    }

    // Attached to the VM but not (or no longer) plugged into the host.
    for (const UsbAttachment &att : m_vm->usbAttachments()) {
        bool present = false;
        for (const UsbDevice &d : devices)
            present |= att.matches(d);
        if (present)
            continue;
        QString name = att.autoConnect() ? QStringLiteral("%1:%2").arg(att.vendorId, att.productId)
                                         : QStringLiteral("Device on port %1-%2").arg(att.hostBus).arg(att.hostPort);
        for (const UsbAutoConnect &u : m_vm->config().usbDevices)
            if (att.autoConnect() && u.vendorId == att.vendorId && u.productId == att.productId && !u.name.isEmpty())
                name = u.name;
        QAction *a = menu->addAction(Usb::iconFor(UsbDevice::Kind::Other), name + QStringLiteral(" — waiting to be plugged in"));
        a->setCheckable(true);
        a->setChecked(true);
        const QString id = att.qdevId;
        const bool isAuto = att.autoConnect();
        connect(a, &QAction::triggered, this, [this, id, name, isAuto] { disconnectUsb(id, name, isAuto); });
    }

    if (devices.isEmpty() && m_vm->usbAttachments().isEmpty())
        menu->addAction(QStringLiteral("No USB devices found"))->setEnabled(false);
    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(QStringLiteral("configure")), QStringLiteral("Connect Automatically at Startup…"), this,
                    [this] { Q_EMIT settingsRequested(m_vm, QStringLiteral("usb")); });
}

void VmWindow::connectUsb(const UsbDevice &d)
{
    if (!m_vm || !m_vm->isActive())
        return;
    const QString name = d.displayName();
    if (d.isKeyboardOrMouse()) {
        if (QMessageBox::question(this, QStringLiteral("Hand over %1?").arg(name),
                                  QStringLiteral("While it's plugged into the VM, the host won't receive any input from "
                                                 "this %1. Make sure you have another one to unplug it again from this menu.")
                                      .arg(d.kind() == UsbDevice::Kind::Keyboard ? QStringLiteral("keyboard")
                                                                                 : QStringLiteral("mouse")))
            != QMessageBox::Yes)
            return;
    }
    const QStringList mounts = d.mountPoints();
    if (!mounts.isEmpty()) {
        QMessageBox box(QMessageBox::Warning, QStringLiteral("%1 is in use on the host").arg(name),
                        QStringLiteral("It's mounted at %1. Eject it on the host first: handing a mounted drive to the VM "
                                       "can corrupt the files on it.").arg(mounts.join(QStringLiteral(", "))),
                        QMessageBox::Cancel, this);
        QPushButton *anyway = box.addButton(QStringLiteral("Connect Anyway"), QMessageBox::DestructiveRole);
        box.exec();
        if (box.clickedButton() != anyway)
            return;
    }
    if (d.accessible()) {
        attachUsbNow(d);
        return;
    }

    // USB device nodes are root-only by default and QEMU runs as us.
    // Preferred: one-time access through spice-gtk's ACL helper (no password
    // on the active session, revoked when the device is handed back).
    if (!Usb::aclHelperPath().isEmpty()) {
        requestOneTimeUsbAccess(d);
        return;
    }

    QMessageBox ask(QMessageBox::Question, QStringLiteral("Allow access to %1?").arg(name),
                    QStringLiteral("Bento runs VMs as your user, and USB devices are only accessible to root by default. "
                                   "Allowing access asks for your administrator password."),
                    QMessageBox::Cancel, this);
    ask.setInformativeText(QStringLiteral("<b>This time</b>: until the device is unplugged or the computer restarts.<br>"
                                          "<b>Always</b>: adds a rule for this device (%1) to %2, so you won't be asked again.")
                               .arg(d.usbId(), Usb::rulesFile()));
    QPushButton *once = ask.addButton(QStringLiteral("Allow This Time"), QMessageBox::AcceptRole);
    QPushButton *always = ask.addButton(QStringLiteral("Always Allow"), QMessageBox::AcceptRole);
    once->setIcon(QIcon::fromTheme(QStringLiteral("dialog-password")));
    ask.setDefaultButton(once);
    ask.exec();
    if (ask.clickedButton() == once) {
        requestOneTimeUsbAccess(d);
        return;
    }
    if (ask.clickedButton() != always)
        return;

    showToast(QStringLiteral("Waiting for authorization…"), 0);
    Usb::grantAccess(d.vendorId, d.productId, this, [this, d](bool ok, const QString &err) {
        if (!ok) {
            m_toast->hide();
            QMessageBox::warning(this, QStringLiteral("Couldn't allow access"), err);
            return;
        }
        // udev applies the ACL asynchronously; give it a moment.
        auto *tries = new int(0);
        auto *timer = new QTimer(this);
        timer->setInterval(200);
        connect(timer, &QTimer::timeout, this, [this, d, timer, tries] {
            UsbDevice current = d;
            for (const UsbDevice &x : Usb::enumerate())
                if (x.sysName == d.sysName)
                    current = x;
            if (current.accessible() || ++*tries > 15) {
                timer->stop();
                timer->deleteLater();
                delete tries;
                if (current.accessible()) {
                    attachUsbNow(current);
                } else {
                    m_toast->hide();
                    QMessageBox::warning(this, QStringLiteral("Still no access"),
                                         QStringLiteral("The rule was added, but the device isn't accessible yet. "
                                                        "Unplug it, plug it back in and try again."));
                }
            }
        });
        timer->start();
    });
}

void VmWindow::requestOneTimeUsbAccess(const UsbDevice &d)
{
    Usb::requestTemporaryAccess(d, this, [this, d](bool ok, const QString &err, QProcess *holder) {
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("Couldn't access %1").arg(d.displayName()), err);
            return;
        }
        if (!m_vm || !m_vm->isActive()) {
            Usb::releaseTemporaryAccess(holder);
            return;
        }
        attachUsbNow(d, holder);
    });
}

void VmWindow::attachUsbNow(const UsbDevice &d, QProcess *accessHolder)
{
    const QString name = d.displayName();
    m_vm->attachUsb(d, [this, name, d](const QString &err) {
        if (!err.isEmpty()) {
            showToast(QStringLiteral("Couldn't connect %1: %2").arg(name, err), 8000);
            return;
        }
        // QEMU claims the port even when it can't open the device; it then
        // keeps the generic "USB Host Device" name. Check after a moment.
        QTimer::singleShot(1200, this, [this, name, d] {
            const UsbAttachment *att = m_vm ? m_vm->usbAttachmentFor(d) : nullptr;
            if (!att || !m_vm->qmp()->isReady())
                return;
            const QString id = att->qdevId;
            m_vm->qmp()->execute(QStringLiteral("human-monitor-command"),
                                 {{QStringLiteral("command-line"), QStringLiteral("info usb")}},
                                 [this, name, id](const QJsonValue &r, const QString &) {
                                     for (const QString &line : r.toString().split(QLatin1Char('\n'))) {
                                         if (line.contains(QStringLiteral("ID: ") + id) && line.contains(QLatin1String("Product USB Host Device"))) {
                                             showToast(QStringLiteral("QEMU couldn't open %1. Another program may be using it; "
                                                                      "see the VM log for details.").arg(name), 9000);
                                             return;
                                         }
                                     }
                                     showToast(QStringLiteral("%1 is now connected to the VM.").arg(name), 4000);
                                 });
        });
    }, accessHolder);
}

void VmWindow::disconnectUsb(const QString &qdevId, const QString &name, bool autoConnect)
{
    m_vm->detachUsb(qdevId, [this, name, autoConnect](const QString &err) {
        if (!err.isEmpty())
            showToast(QStringLiteral("Couldn't disconnect %1: %2").arg(name, err), 8000);
        else if (autoConnect)
            showToast(QStringLiteral("%1 is back on the host. It will reconnect the next time the VM starts; "
                                     "change that in Settings → USB.").arg(name), 7000);
        else
            showToast(QStringLiteral("%1 is back on the host.").arg(name), 4000);
    });
}
