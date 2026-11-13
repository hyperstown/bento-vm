// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "SettingsDialog.h"
#include "Theme.h"
#include "core/AppSettings.h"
#include "core/HostInfo.h"
#include "core/Paths.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QStorageInfo>
#include <QUrl>
#include <QVBoxLayout>

namespace Combos {

QComboBox *graphics(QWidget *parent)
{
    auto *c = new QComboBox(parent);
    c->addItem(QStringLiteral("Venus — Vulkan + OpenGL acceleration"), QStringLiteral("venus"));
    c->addItem(QStringLiteral("VirGL — OpenGL acceleration only"), QStringLiteral("virgl"));
    c->addItem(QStringLiteral("Standard — 2D, no acceleration"), QStringLiteral("2d"));
    return c;
}

QComboBox *hostmem(QWidget *parent)
{
    auto *c = new QComboBox(parent);
    for (int g : {1, 2, 4, 8, 16})
        c->addItem(QStringLiteral("%1 GiB").arg(g), g * 1024);
    c->setToolTip(QStringLiteral("Size of the host-visible memory window Venus uses to map GPU buffers into the guest.\n"
                                 "It is address space, not RAM that gets reserved up front."));
    return c;
}

QComboBox *display(QWidget *parent)
{
    auto *c = new QComboBox(parent);
    c->addItem(QStringLiteral("Built-in (recommended)"), QStringLiteral("embedded"));
    c->addItem(QStringLiteral("QEMU window (GTK)"), QStringLiteral("gtk"));
    c->addItem(QStringLiteral("QEMU window (SDL)"), QStringLiteral("sdl"));
    c->setToolTip(QStringLiteral("The built-in display supports clipboard direction control and file drag & drop.\n"
                                 "QEMU's own windows are a fallback if the built-in display has problems."));
    return c;
}

QComboBox *clipboard(QWidget *parent)
{
    auto *c = new QComboBox(parent);
    c->addItem(QStringLiteral("Both directions"), QStringLiteral("both"));
    c->addItem(QStringLiteral("Host → Guest only"), QStringLiteral("host-to-guest"));
    c->addItem(QStringLiteral("Guest → Host only"), QStringLiteral("guest-to-host"));
    c->addItem(QStringLiteral("Disabled"), QStringLiteral("off"));
    return c;
}

QComboBox *firmware(QWidget *parent)
{
    auto *c = new QComboBox(parent);
    c->addItem(QStringLiteral("UEFI"), QStringLiteral("uefi"));
    c->addItem(QStringLiteral("BIOS (legacy)"), QStringLiteral("bios"));
    return c;
}

void select(QComboBox *box, const QVariant &data)
{
    const int i = box->findData(data);
    if (i >= 0)
        box->setCurrentIndex(i);
}

} // namespace Combos

SettingsDialog::SettingsDialog(QWidget *parent)
    : PageDialog(parent)
{
    setWindowTitle(QStringLiteral("Configure BentoVM"));
    m_originalTheme = Theme::instance()->current();
    buildAppearance();
    buildStorage();
    buildDefaults();
    buildBehaviour();
    buildQemu();

    m_buttons->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] {
        save();
        m_originalTheme = Theme::instance()->current();
    });
}

void SettingsDialog::showStoragePage()
{
    setCurrentPage(m_storagePage);
}

void SettingsDialog::buildAppearance()
{
    QVBoxLayout *page = addPage(QStringLiteral("Appearance"), QIcon::fromTheme(QStringLiteral("preferences-desktop-theme")),
                                QStringLiteral("BentoVM looks like a regular KDE application by default. The Modern themes give "
                                               "it a self-contained look with rounded cards and a green accent."));
    m_themeGroup = new QButtonGroup(this);
    for (const Theme::Info &t : Theme::available()) {
        auto *card = new QFrame(this);
        card->setObjectName(QStringLiteral("card"));
        card->setFrameShape(QFrame::StyledPanel);
        auto *cl = new QVBoxLayout(card);
        auto *rb = new QRadioButton(t.name, card);
        QFont f = rb->font();
        f.setBold(true);
        rb->setFont(f);
        rb->setProperty("themeId", t.id);
        m_themeGroup->addButton(rb);
        cl->addWidget(rb);
        auto *desc = PageDialog::hint(t.description, card);
        desc->setContentsMargins(24, 0, 0, 0);
        cl->addWidget(desc);
        if (t.id == Theme::instance()->current())
            rb->setChecked(true);
        page->addWidget(card);
    }
    connect(m_themeGroup, &QButtonGroup::buttonToggled, this, [](QAbstractButton *b, bool on) {
        if (on)
            Theme::instance()->apply(b->property("themeId").toString());   // live preview
    });
}

void SettingsDialog::buildStorage()
{
    m_storagePage = m_nav->count();
    QVBoxLayout *page = addPage(QStringLiteral("Storage"), QIcon::fromTheme(QStringLiteral("drive-harddisk")),
                                QStringLiteral("Folders where BentoVM keeps virtual machines. New VMs go to the default "
                                               "location. Locations on external drives are fine: their VMs show as "
                                               "“temporarily unavailable” while the drive is disconnected."));
    const AppSettings &s = AppSettings::instance();
    m_locations = s.storageLocations();
    m_defaultLocation = s.defaultLocation();

    m_locationList = new QListWidget(this);
    m_locationList->setIconSize(QSize(32, 32));
    m_locationList->setMinimumHeight(220);
    m_locationList->setTextElideMode(Qt::ElideMiddle);
    m_locationList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    page->addWidget(m_locationList);

    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), QStringLiteral("Add Location…"), this);
    auto *remove = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), QStringLiteral("Remove"), this);
    auto *makeDefault = new QPushButton(QIcon::fromTheme(QStringLiteral("starred-symbolic"), QIcon::fromTheme(QStringLiteral("rating"))),
                                        QStringLiteral("Make Default"), this);
    auto *open = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open-folder")), QStringLiteral("Open"), this);
    row->addWidget(add);
    row->addWidget(remove);
    row->addWidget(makeDefault);
    row->addStretch();
    row->addWidget(open);
    page->addLayout(row);
    page->addWidget(PageDialog::hint(QStringLiteral("Removing a location only stops BentoVM from looking there; no files are deleted."), this));

    connect(add, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose a folder for virtual machines"),
                                                              QDir::homePath());
        if (dir.isEmpty())
            return;
        const QString clean = QDir::cleanPath(dir);
        if (!m_locations.contains(clean))
            m_locations << clean;
        refreshLocations();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = m_locationList->currentRow();
        if (row < 0 || m_locations.size() <= 1)
            return;
        const QString loc = m_locations.takeAt(row);
        if (m_defaultLocation == loc)
            m_defaultLocation = m_locations.first();
        refreshLocations();
    });
    connect(makeDefault, &QPushButton::clicked, this, [this] {
        const int row = m_locationList->currentRow();
        if (row >= 0)
            m_defaultLocation = m_locations[row];
        refreshLocations();
    });
    connect(open, &QPushButton::clicked, this, [this] {
        const int row = m_locationList->currentRow();
        if (row >= 0)
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_locations[row]));
    });
    refreshLocations();
}

void SettingsDialog::refreshLocations()
{
    const int keep = m_locationList->currentRow();
    m_locationList->clear();
    for (const QString &loc : std::as_const(m_locations)) {
        const bool exists = QFileInfo::exists(loc);
        QString status;
        if (exists) {
            QStorageInfo si(loc);
            status = QStringLiteral("%1 free of %2").arg(Paths::humanSize(si.bytesAvailable()), Paths::humanSize(si.bytesTotal()));
        } else if (loc == Paths::defaultStorageLocation()) {
            status = QStringLiteral("Will be created when needed");
        } else {
            status = QStringLiteral("Not available right now (drive disconnected?)");
        }
        auto *item = new QListWidgetItem(
            QIcon::fromTheme(exists ? QStringLiteral("folder") : QStringLiteral("folder-important"),
                             QIcon::fromTheme(QStringLiteral("folder"))),
            QStringLiteral("%1%2\n%3").arg(loc, loc == m_defaultLocation ? QStringLiteral("   (default)") : QString(), status),
            m_locationList);
        if (loc == m_defaultLocation) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
    }
    m_locationList->setCurrentRow(qBound(0, keep, int(m_locations.size()) - 1));
}

void SettingsDialog::buildDefaults()
{
    QVBoxLayout *page = addPage(QStringLiteral("New VMs"), QIcon::fromTheme(QStringLiteral("document-new")),
                                QStringLiteral("Defaults the setup wizard starts from. Operating systems that need different "
                                               "settings (e.g. Windows) override them."));
    const AppSettings &s = AppSettings::instance();
    auto *form = makeForm();
    m_graphics = Combos::graphics(this);
    Combos::select(m_graphics, s.defaultGraphics());
    m_hostmem = Combos::hostmem(this);
    Combos::select(m_hostmem, s.defaultHostmemMiB());
    m_display = Combos::display(this);
    Combos::select(m_display, s.defaultDisplay());
    m_clipboard = Combos::clipboard(this);
    Combos::select(m_clipboard, s.defaultClipboard());
    m_fileTransfer = new QCheckBox(QStringLiteral("Allow dragging files into the VM"), this);
    m_fileTransfer->setChecked(s.defaultFileTransfer());
    m_firmware = Combos::firmware(this);
    Combos::select(m_firmware, s.defaultFirmware());
    form->addRow(QStringLiteral("Graphics:"), m_graphics);
    form->addRow(QStringLiteral("Venus memory window:"), m_hostmem);
    form->addRow(QStringLiteral("Display:"), m_display);
    form->addRow(QStringLiteral("Clipboard sharing:"), m_clipboard);
    form->addRow(QString(), m_fileTransfer);
    form->addRow(QStringLiteral("Firmware:"), m_firmware);
    page->addLayout(form);
}

void SettingsDialog::buildBehaviour()
{
    QVBoxLayout *page = addPage(QStringLiteral("Behavior"), QIcon::fromTheme(QStringLiteral("preferences-system-windows")));
    auto *form = makeForm();
    m_closeAction = new QComboBox(this);
    m_closeAction->addItem(QStringLiteral("Ask every time"), QStringLiteral("ask"));
    m_closeAction->addItem(QStringLiteral("Keep the VM running in the background"), QStringLiteral("keep"));
    m_closeAction->addItem(QStringLiteral("Shut the VM down"), QStringLiteral("shutdown"));
    Combos::select(m_closeAction, AppSettings::instance().closeAction());
    form->addRow(QStringLiteral("When closing a running VM's window:"), m_closeAction);
    page->addLayout(form);
    page->addWidget(PageDialog::hint(QStringLiteral(
        "VMs run as independent QEMU processes, so they keep running if BentoVM is closed or crashes. "
        "BentoVM reconnects to them the next time it starts."), this));
}

void SettingsDialog::buildQemu()
{
    QVBoxLayout *page = addPage(QStringLiteral("QEMU"), QIcon::fromTheme(QStringLiteral("utilities-terminal")),
                                QStringLiteral("What BentoVM found on this system."));
    const HostInfo &h = HostInfo::get();
    auto *form = makeForm();
    m_qemuBinary = new QLineEdit(AppSettings::instance().qemuBinary(), this);
    m_qemuBinary->setPlaceholderText(HostInfo::qemuBinary());
    auto *browse = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")), QString(), this);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("QEMU binary"), QStringLiteral("/usr/bin"));
        if (!f.isEmpty())
            m_qemuBinary->setText(f);
    });
    auto *binRow = new QHBoxLayout;
    binRow->addWidget(m_qemuBinary, 1);
    binRow->addWidget(browse);
    form->addRow(QStringLiteral("QEMU binary:"), binRow);
    m_extraArgs = new QLineEdit(AppSettings::instance().extraQemuArgs(), this);
    m_extraArgs->setPlaceholderText(QStringLiteral("Extra arguments added to every VM"));
    form->addRow(QStringLiteral("Extra arguments:"), m_extraArgs);
    page->addLayout(form);

    auto *box = new QGroupBox(QStringLiteral("System"), this);
    auto *bf = makeForm();
    auto status = [](bool ok, const QString &text) {
        return QStringLiteral("<span style='color:%1'>●</span> %2")
            .arg(ok ? QStringLiteral("#22c55e") : QStringLiteral("#ef4444"), text.toHtmlEscaped());
    };
    QProcess p;
    p.setProcessEnvironment(Paths::hostEnvironment());
    p.start(HostInfo::qemuBinary(), {QStringLiteral("--version")});
    p.waitForFinished(3000);
    const QString version = QString::fromLocal8Bit(p.readAllStandardOutput()).section(QLatin1Char('\n'), 0, 0);
    auto addRow = [&](const QString &label, bool ok, const QString &text) {
        auto *l = new QLabel(status(ok, text), box);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setWordWrap(true);
        bf->addRow(label, l);
    };
    addRow(QStringLiteral("QEMU:"), !version.isEmpty(), version.isEmpty() ? QStringLiteral("not found") : version);
    addRow(QStringLiteral("KVM:"), h.kvmUsable, h.kvmUsable ? QStringLiteral("/dev/kvm is usable") : h.kvmProblem);
    addRow(QStringLiteral("CPU:"), true, QStringLiteral("%1 — %2 cores / %3 threads").arg(h.cpuModel).arg(h.physicalCores).arg(h.logicalCpus));
    addRow(QStringLiteral("UEFI firmware:"), !h.ovmfCode.isEmpty(), h.ovmfCode.isEmpty() ? QStringLiteral("not found (install edk2-ovmf)") : h.ovmfCode);
    addRow(QStringLiteral("Secure Boot firmware:"), !h.ovmfCodeSecure.isEmpty(), h.ovmfCodeSecure.isEmpty() ? QStringLiteral("not found") : h.ovmfCodeSecure);
    addRow(QStringLiteral("TPM emulator:"), !h.swtpm.isEmpty(), h.swtpm.isEmpty() ? QStringLiteral("swtpm not installed (needed for Windows 11)") : h.swtpm);
    addRow(QStringLiteral("qemu-img:"), !h.qemuImg.isEmpty(), h.qemuImg.isEmpty() ? QStringLiteral("not found") : h.qemuImg);
    box->setLayout(bf);
    page->addWidget(box);
}

void SettingsDialog::save()
{
    AppSettings &s = AppSettings::instance();
    if (QAbstractButton *b = m_themeGroup->checkedButton())
        s.setTheme(b->property("themeId").toString());
    s.setStorageLocations(m_locations);
    s.setDefaultLocation(m_defaultLocation);
    s.setValue(QStringLiteral("defaults/graphics"), m_graphics->currentData());
    s.setValue(QStringLiteral("defaults/hostmemMiB"), m_hostmem->currentData());
    s.setValue(QStringLiteral("defaults/display"), m_display->currentData());
    s.setValue(QStringLiteral("defaults/clipboard"), m_clipboard->currentData());
    s.setValue(QStringLiteral("defaults/fileTransfer"), m_fileTransfer->isChecked());
    s.setValue(QStringLiteral("defaults/firmware"), m_firmware->currentData());
    s.setCloseAction(m_closeAction->currentData().toString());
    s.setValue(QStringLiteral("advanced/qemuBinary"), m_qemuBinary->text().trimmed());
    s.setValue(QStringLiteral("advanced/extraArgs"), m_extraArgs->text().trimmed());
}

void SettingsDialog::accept()
{
    save();
    QDialog::accept();
}

void SettingsDialog::reject()
{
    if (Theme::instance()->current() != m_originalTheme)
        Theme::instance()->apply(m_originalTheme);
    QDialog::reject();
}
