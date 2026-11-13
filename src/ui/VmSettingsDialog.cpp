// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VmSettingsDialog.h"
#include "Icons.h"
#include "OsIcon.h"
#include "ResourceEditors.h"
#include "SettingsDialog.h"
#include "core/DiskOps.h"
#include "core/HostInfo.h"
#include "core/OsDb.h"
#include "core/Paths.h"
#include "core/QemuCommand.h"
#include "core/UsbDevices.h"
#include "core/Vm.h"
#include "core/VmLibrary.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

VmSettingsDialog::VmSettingsDialog(Vm *vm, QWidget *parent)
    : PageDialog(parent)
    , m_vm(vm)
    , m_cfg(vm->config())
{
    setWindowTitle(QStringLiteral("%1 — Settings").arg(vm->name()));
    setWindowIcon(OsIcon::icon(m_cfg.osId));

    if (vm->isActive()) {
        auto *banner = new QFrame(this);
        banner->setObjectName(QStringLiteral("banner"));
        banner->setFrameShape(QFrame::StyledPanel);
        auto *bl = new QHBoxLayout(banner);
        auto *icon = new QLabel(banner);
        Icons::showIn(icon, Icons::fromTheme(QStringLiteral("dialog-information")), 22);
        bl->addWidget(icon);
        bl->addWidget(new QLabel(QStringLiteral("The VM is running. Hardware changes apply the next time it starts; "
                                                "clipboard and file transfer settings apply right away."), banner), 1);
        headerArea()->addWidget(banner);
    }

    buildGeneral();
    buildResources();
    buildDisplay();
    buildStorage();
    buildNetwork();
    buildUsb();
    buildIntegration();
    buildFirmware();
    buildAdvanced();

    m_buttons->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &VmSettingsDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &VmSettingsDialog::reject);
    connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &VmSettingsDialog::apply);
    connect(m_nav, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row == m_nav->count() - 1)
            refreshCommand();
    });
}

void VmSettingsDialog::buildGeneral()
{
    QVBoxLayout *page = addPage(QStringLiteral("General"), Icons::fromTheme(QStringLiteral("documentinfo"),
                                                                           Icons::fromTheme(QStringLiteral("dialog-information"))));
    auto *form = makeForm();
    m_name = new QLineEdit(m_cfg.name, this);
    form->addRow(QStringLiteral("Name:"), m_name);
    m_os = new QComboBox(this);
    for (const OsProfile &p : OsDb::profiles())
        m_os->addItem(OsIcon::icon(p.id), p.name, p.id);
    Combos::select(m_os, m_cfg.osId);
    form->addRow(QStringLiteral("Operating system:"), m_os);
    auto *folder = new QLabel(QStringLiteral("<a href=\"%1\">%2</a>")
                                  .arg(QUrl::fromLocalFile(m_cfg.dir).toString(), m_cfg.dir.toHtmlEscaped()), this);
    folder->setOpenExternalLinks(true);
    folder->setWordWrap(true);
    form->addRow(QStringLiteral("Folder:"), folder);
    m_notes = new QPlainTextEdit(m_cfg.notes, this);
    m_notes->setPlaceholderText(QStringLiteral("Anything you want to remember about this VM…"));
    m_notes->setMaximumHeight(140);
    form->addRow(QStringLiteral("Notes:"), m_notes);
    page->addLayout(form);
}

void VmSettingsDialog::buildResources()
{
    QVBoxLayout *page = addPage(QStringLiteral("CPU & Memory"), Icons::fromTheme(QStringLiteral("cpu"),
                                                                                Icons::fromTheme(QStringLiteral("computer"))));
    const Recommendation rec = OsDb::recommend(OsDb::profile(m_cfg.osId), HostInfo::get());
    auto *cpuBox = new QGroupBox(QStringLiteral("Processor"), this);
    auto *cl = new QVBoxLayout(cpuBox);
    m_cpu = new CpuEditor(cpuBox);
    m_cpu->setRecommended(rec.cores, rec.threads);
    m_cpu->setValue(m_cfg.cores, m_cfg.threads);
    cl->addWidget(m_cpu);
    page->addWidget(cpuBox);

    auto *memBox = new QGroupBox(QStringLiteral("Memory"), this);
    auto *ml = new QVBoxLayout(memBox);
    m_mem = new MemoryEditor(memBox);
    m_mem->setRecommended(rec.memoryMiB);
    m_mem->setValueMiB(m_cfg.memoryMiB);
    ml->addWidget(m_mem);
    page->addWidget(memBox);
}

void VmSettingsDialog::buildDisplay()
{
    QVBoxLayout *page = addPage(QStringLiteral("Display"), Icons::fromTheme(QStringLiteral("video-display")));
    auto *form = makeForm();
    m_graphics = Combos::graphics(this);
    Combos::select(m_graphics, m_cfg.graphics);
    form->addRow(QStringLiteral("Graphics:"), m_graphics);
    m_hostmem = Combos::hostmem(this);
    Combos::select(m_hostmem, m_cfg.hostmemMiB);
    form->addRow(QStringLiteral("Venus memory window:"), m_hostmem);
    m_display = Combos::display(this);
    Combos::select(m_display, m_cfg.display);
    form->addRow(QStringLiteral("Show the VM in:"), m_display);
    m_autoResize = new QCheckBox(QStringLiteral("Resize the guest screen to fit the window"), this);
    m_autoResize->setChecked(m_cfg.autoResize);
    form->addRow(QString(), m_autoResize);
    page->addLayout(form);
    page->addWidget(PageDialog::hint(QStringLiteral(
        "<b>Venus</b> gives the guest Vulkan (and OpenGL through Zink or VirGL) on your host GPU. The guest needs "
        "Mesa's virtio Vulkan driver (Arch: <i>vulkan-virtio</i>, Fedora/Ubuntu: <i>mesa-vulkan-drivers</i>) and a "
        "recent kernel. <b>VirGL</b> is OpenGL only. <b>Standard</b> works for every OS, including Windows."), this));
    auto sync = [this] { m_hostmem->setEnabled(m_graphics->currentData() == QLatin1String("venus")); };
    connect(m_graphics, &QComboBox::currentIndexChanged, this, sync);
    sync();
}

void VmSettingsDialog::buildStorage()
{
    QVBoxLayout *page = addPage(QStringLiteral("Storage"), Icons::fromTheme(QStringLiteral("drive-harddisk")));
    auto *diskBox = new QGroupBox(QStringLiteral("Disks"), this);
    auto *dl = new QVBoxLayout(diskBox);
    m_disks = new QTreeWidget(diskBox);
    m_disks->setHeaderLabels({QStringLiteral("Image"), QStringLiteral("Format"), QStringLiteral("Bus")});
    m_disks->setRootIsDecorated(false);
    m_disks->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_disks->setMinimumHeight(140);
    dl->addWidget(m_disks);
    auto *row = new QHBoxLayout;
    auto *addExisting = new QPushButton(Icons::fromTheme(QStringLiteral("document-open")), QStringLiteral("Add Existing…"), diskBox);
    auto *addNew = new QPushButton(Icons::fromTheme(QStringLiteral("list-add")), QStringLiteral("Create New…"), diskBox);
    auto *remove = new QPushButton(Icons::fromTheme(QStringLiteral("list-remove")), QStringLiteral("Remove"), diskBox);
    auto *bus = new QComboBox(diskBox);
    bus->addItem(QStringLiteral("VirtIO (fastest)"), QStringLiteral("virtio"));
    bus->addItem(QStringLiteral("SATA (most compatible)"), QStringLiteral("sata"));
    bus->addItem(QStringLiteral("NVMe"), QStringLiteral("nvme"));
    row->addWidget(addExisting);
    row->addWidget(addNew);
    row->addWidget(remove);
    row->addStretch();
    row->addWidget(new QLabel(QStringLiteral("Bus:"), diskBox));
    row->addWidget(bus);
    dl->addLayout(row);
    page->addWidget(diskBox);

    connect(m_disks, &QTreeWidget::currentItemChanged, this, [this, bus] {
        const int i = m_disks->indexOfTopLevelItem(m_disks->currentItem());
        bus->setEnabled(i >= 0);
        if (i >= 0) {
            QSignalBlocker b(bus);
            Combos::select(bus, m_cfg.disks[i].bus);
        }
    });
    connect(bus, &QComboBox::currentIndexChanged, this, [this, bus] {
        const int i = m_disks->indexOfTopLevelItem(m_disks->currentItem());
        if (i >= 0) {
            m_cfg.disks[i].bus = bus->currentData().toString();
            refreshDisks();
            m_disks->setCurrentItem(m_disks->topLevelItem(i));
        }
    });
    connect(addExisting, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Add disk image"), m_cfg.dir,
                                                       QStringLiteral("Disk images (*.qcow2 *.img *.raw *.vmdk *.vdi *.vhd *.vhdx);;All files (*)"));
        if (f.isEmpty())
            return;
        const DiskOps::ImageInfo info = DiskOps::imageInfo(f);
        DiskSpec d;
        d.path = f;
        d.format = info.ok ? info.format : DiskOps::formatForPath(f);
        d.bus = m_cfg.disks.isEmpty() ? OsDb::profile(m_cfg.osId).diskBus : m_cfg.disks.first().bus;
        m_cfg.disks << d;
        refreshDisks();
    });
    connect(addNew, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const int size = QInputDialog::getInt(this, QStringLiteral("New disk"), QStringLiteral("Size in GiB:"), 32, 1, 65536, 1, &ok);
        if (!ok)
            return;
        QString name = QStringLiteral("disk%1.qcow2").arg(m_cfg.disks.size() + 1);
        for (int n = 2; QFileInfo::exists(m_cfg.dir + QLatin1Char('/') + name); ++n)
            name = QStringLiteral("disk%1.qcow2").arg(m_cfg.disks.size() + n);
        QString err;
        if (!DiskOps::createDisk(m_cfg.dir + QLatin1Char('/') + name, size, &err)) {
            QMessageBox::warning(this, QStringLiteral("Couldn't create the disk"), err);
            return;
        }
        DiskSpec d;
        d.path = name;
        d.bus = m_cfg.disks.isEmpty() ? OsDb::profile(m_cfg.osId).diskBus : m_cfg.disks.first().bus;
        m_cfg.disks << d;
        refreshDisks();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int i = m_disks->indexOfTopLevelItem(m_disks->currentItem());
        if (i < 0)
            return;
        if (QMessageBox::question(this, QStringLiteral("Remove disk?"),
                                  QStringLiteral("The disk is detached from the VM. The image file itself is kept:\n%1")
                                      .arg(m_cfg.diskPath(i))) != QMessageBox::Yes)
            return;
        m_cfg.disks.removeAt(i);
        refreshDisks();
    });
    refreshDisks();

    auto *cdBox = new QGroupBox(QStringLiteral("CD/DVD drives"), this);
    auto *cf = makeForm();
    for (int i = 0; i < 2; ++i) {
        m_cd[i] = new QLineEdit(m_cfg.cdroms.value(i), cdBox);
        m_cd[i]->setPlaceholderText(QStringLiteral("Empty"));
        m_cd[i]->setClearButtonEnabled(true);
        auto *browse = new QPushButton(Icons::fromTheme(QStringLiteral("document-open")), QString(), cdBox);
        QLineEdit *edit = m_cd[i];
        connect(browse, &QPushButton::clicked, this, [this, edit] {
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Choose a disc image"), QDir::homePath(),
                                                           QStringLiteral("Disc images (*.iso *.img);;All files (*)"));
            if (!f.isEmpty())
                edit->setText(f);
        });
        auto *r = new QHBoxLayout;
        r->addWidget(m_cd[i], 1);
        r->addWidget(browse);
        cf->addRow(QStringLiteral("Drive %1:").arg(i + 1), r);
    }
    cdBox->setLayout(cf);
    page->addWidget(cdBox);
}

void VmSettingsDialog::refreshDisks()
{
    const int cur = m_disks->indexOfTopLevelItem(m_disks->currentItem());
    m_disks->clear();
    for (int i = 0; i < m_cfg.disks.size(); ++i) {
        const DiskSpec &d = m_cfg.disks[i];
        const QString path = m_cfg.diskPath(i);
        auto *item = new QTreeWidgetItem(m_disks, {path, d.format, d.bus});
        item->setIcon(0, Icons::fromTheme(QFileInfo::exists(path) ? QStringLiteral("drive-harddisk")
                                                                  : QStringLiteral("dialog-warning")));
        if (!QFileInfo::exists(path))
            item->setToolTip(0, QStringLiteral("File not found"));
    }
    if (cur >= 0 && cur < m_disks->topLevelItemCount())
        m_disks->setCurrentItem(m_disks->topLevelItem(cur));
}

void VmSettingsDialog::buildNetwork()
{
    QVBoxLayout *page = addPage(QStringLiteral("Network"), Icons::fromTheme(QStringLiteral("network-wired")),
                                QStringLiteral("The VM gets NAT networking through QEMU (no root or bridge setup needed). "
                                               "Port forwards make guest services reachable from the host."));
    auto *form = makeForm();
    m_network = new QCheckBox(QStringLiteral("Connect the VM to the network"), this);
    m_network->setChecked(m_cfg.network);
    form->addRow(QString(), m_network);
    m_nic = new QComboBox(this);
    m_nic->addItem(QStringLiteral("VirtIO (fastest, needs drivers on Windows)"), QStringLiteral("virtio-net-pci"));
    m_nic->addItem(QStringLiteral("Intel e1000e"), QStringLiteral("e1000e"));
    m_nic->addItem(QStringLiteral("Intel e1000 (older guests)"), QStringLiteral("e1000"));
    m_nic->addItem(QStringLiteral("Realtek RTL8139"), QStringLiteral("rtl8139"));
    Combos::select(m_nic, m_cfg.nicModel);
    form->addRow(QStringLiteral("Network card:"), m_nic);
    page->addLayout(form);

    auto *box = new QGroupBox(QStringLiteral("Port forwarding"), this);
    auto *bl = new QVBoxLayout(box);
    m_forwards = new QTableWidget(0, 4, box);
    m_forwards->setHorizontalHeaderLabels({QStringLiteral("Protocol"), QStringLiteral("Host address"),
                                           QStringLiteral("Host port"), QStringLiteral("Guest port")});
    m_forwards->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_forwards->verticalHeader()->hide();
    m_forwards->setMinimumHeight(150);
    bl->addWidget(m_forwards);
    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(Icons::fromTheme(QStringLiteral("list-add")), QStringLiteral("Add"), box);
    auto *ssh = new QPushButton(Icons::fromTheme(QStringLiteral("utilities-terminal")), QStringLiteral("Add SSH"), box);
    auto *remove = new QPushButton(Icons::fromTheme(QStringLiteral("list-remove")), QStringLiteral("Remove"), box);
    row->addWidget(add);
    row->addWidget(ssh);
    row->addWidget(remove);
    row->addStretch();
    bl->addLayout(row);
    bl->addWidget(PageDialog::hint(QStringLiteral("Use 127.0.0.1 to keep a port private to this computer, or 0.0.0.0 to "
                                                  "expose it on your network. Example: <code>ssh -p 2222 user@localhost</code>"),
                                   box));
    page->addWidget(box);

    auto addRow = [this](const PortForward &f) {
        const int r = m_forwards->rowCount();
        m_forwards->insertRow(r);
        auto *proto = new QComboBox(m_forwards);
        proto->addItems({QStringLiteral("tcp"), QStringLiteral("udp")});
        proto->setCurrentText(f.proto);
        m_forwards->setCellWidget(r, 0, proto);
        m_forwards->setItem(r, 1, new QTableWidgetItem(f.hostAddress));
        m_forwards->setItem(r, 2, new QTableWidgetItem(QString::number(f.hostPort)));
        m_forwards->setItem(r, 3, new QTableWidgetItem(QString::number(f.guestPort)));
    };
    for (const PortForward &f : std::as_const(m_cfg.portForwards))
        addRow(f);
    connect(add, &QPushButton::clicked, this, [addRow] {
        PortForward f;
        f.hostPort = 8080;
        f.guestPort = 80;
        addRow(f);
    });
    connect(ssh, &QPushButton::clicked, this, [addRow] {
        PortForward f;
        f.hostPort = 2222;
        f.guestPort = 22;
        addRow(f);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (m_forwards->currentRow() >= 0)
            m_forwards->removeRow(m_forwards->currentRow());
    });
}

void VmSettingsDialog::showPage(const QString &name)
{
    if (name == QLatin1String("usb") && m_usbPage >= 0)
        setCurrentPage(m_usbPage);
}

void VmSettingsDialog::buildUsb()
{
    m_usbPage = m_nav->count();
    QVBoxLayout *page = addPage(QStringLiteral("USB"), Icons::fromTheme(QStringLiteral("drive-removable-media-usb"),
                                                                       Icons::fromTheme(QStringLiteral("drive-removable-media"))),
                                QStringLiteral("Devices listed here are handed to the VM automatically whenever it starts, and as "
                                               "soon as they're plugged in while it runs. To connect a device just for now, use "
                                               "the USB menu in the VM window."));
    m_usbList = new QListWidget(this);
    m_usbList->setIconSize(QSize(28, 28));
    m_usbList->setMinimumHeight(200);
    page->addWidget(m_usbList);

    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(Icons::fromTheme(QStringLiteral("list-add")), QStringLiteral("Add Device"), this);
    auto *addMenu = new QMenu(add);
    add->setMenu(addMenu);
    auto *remove = new QPushButton(Icons::fromTheme(QStringLiteral("list-remove")), QStringLiteral("Remove"), this);
    m_usbAllow = new QPushButton(Icons::fromTheme(QStringLiteral("dialog-password")), QStringLiteral("Allow Access…"), this);
    m_usbAllow->setToolTip(QStringLiteral("Installs a udev rule so your user may use this device (asks for the administrator password)."));
    row->addWidget(add);
    row->addWidget(remove);
    row->addStretch();
    row->addWidget(m_usbAllow);
    page->addLayout(row);
    page->addWidget(PageDialog::hint(QStringLiteral("USB devices are only accessible to root by default. “Allow Access” adds a "
                                                    "rule for that one device to %1.").arg(Usb::rulesFile()), this));

    connect(addMenu, &QMenu::aboutToShow, this, [this, addMenu] {
        addMenu->clear();
        for (const UsbDevice &d : Usb::enumerate()) {
            bool listed = false;
            for (const UsbAutoConnect &u : std::as_const(m_cfg.usbDevices))
                listed |= u.vendorId == d.vendorId && u.productId == d.productId;
            QAction *a = addMenu->addAction(d.icon(), QStringLiteral("%1  (%2)").arg(d.displayName(), d.usbId()));
            a->setEnabled(!listed);
            connect(a, &QAction::triggered, this, [this, d] {
                if (d.isKeyboardOrMouse()
                    && QMessageBox::question(this, QStringLiteral("Hand over %1 at every start?").arg(d.displayName()),
                                             QStringLiteral("While the VM runs, the host won't receive input from this device."))
                           != QMessageBox::Yes)
                    return;
                m_cfg.usbDevices << UsbAutoConnect{d.vendorId, d.productId, d.displayName()};
                refreshUsbList();
                m_usbList->setCurrentRow(m_usbList->count() - 1);
            });
        }
        if (addMenu->isEmpty())
            addMenu->addAction(QStringLiteral("No USB devices plugged in"))->setEnabled(false);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = m_usbList->currentRow();
        if (row >= 0 && row < m_cfg.usbDevices.size()) {
            m_cfg.usbDevices.removeAt(row);
            refreshUsbList();
        }
    });
    connect(m_usbAllow, &QPushButton::clicked, this, [this] {
        const int row = m_usbList->currentRow();
        if (row < 0 || row >= m_cfg.usbDevices.size())
            return;
        const UsbAutoConnect u = m_cfg.usbDevices[row];
        m_usbAllow->setEnabled(false);
        Usb::grantAccess(u.vendorId, u.productId, this, [this](bool ok, const QString &err) {
            if (!ok)
                QMessageBox::warning(this, QStringLiteral("Couldn't allow access"), err);
            QTimer::singleShot(500, this, &VmSettingsDialog::refreshUsbList);
        });
    });
    connect(m_usbList, &QListWidget::currentRowChanged, this, &VmSettingsDialog::refreshUsbList);
    refreshUsbList();
}

void VmSettingsDialog::refreshUsbList()
{
    QSignalBlocker block(m_usbList);
    const int keep = m_usbList->currentRow();
    const int selected = m_cfg.usbDevices.isEmpty() ? -1 : qBound(0, keep, int(m_cfg.usbDevices.size()) - 1);
    m_usbList->clear();
    const QList<UsbDevice> host = Usb::enumerate();
    bool selectedNeedsAccess = false;
    for (int i = 0; i < m_cfg.usbDevices.size(); ++i) {
        const UsbAutoConnect &u = m_cfg.usbDevices[i];
        const UsbDevice *present = nullptr;
        for (const UsbDevice &d : host)
            if (d.vendorId == u.vendorId && d.productId == u.productId)
                present = &d;
        QString status;
        QIcon icon = present ? present->icon() : Usb::iconFor(UsbDevice::Kind::Other);
        if (!present) {
            status = QStringLiteral("not plugged in");
        } else if (!present->accessible()) {
            status = QStringLiteral("needs permission");
            icon = Icons::fromTheme(QStringLiteral("dialog-warning"));
            if (i == selected)
                selectedNeedsAccess = true;
        } else {
            status = QStringLiteral("ready");
        }
        new QListWidgetItem(icon, QStringLiteral("%1  (%2:%3)\n%4")
                                      .arg(u.name.isEmpty() ? QStringLiteral("USB device") : u.name, u.vendorId, u.productId, status),
                            m_usbList);
    }
    if (selected >= 0)
        m_usbList->setCurrentRow(selected);
    m_usbAllow->setEnabled(selectedNeedsAccess);
}

void VmSettingsDialog::buildIntegration()
{
    QVBoxLayout *page = addPage(QStringLiteral("Integration"), Icons::fromTheme(QStringLiteral("edit-paste")),
                                QStringLiteral("Clipboard sharing and file drag & drop need the SPICE guest agent in the VM "
                                               "(Linux: spice-vdagent, Windows: virtio-win guest tools)."));
    auto *form = makeForm();
    m_clipboard = Combos::clipboard(this);
    Combos::select(m_clipboard, m_cfg.clipboard);
    form->addRow(QStringLiteral("Clipboard sharing:"), m_clipboard);
    m_fileTransfer = new QCheckBox(QStringLiteral("Allow dragging files from the host into the VM"), this);
    m_fileTransfer->setChecked(m_cfg.fileTransfer);
    form->addRow(QString(), m_fileTransfer);
    m_audio = new QCheckBox(QStringLiteral("Sound output (through PipeWire)"), this);
    m_audio->setChecked(m_cfg.audio);
    form->addRow(QString(), m_audio);
    page->addLayout(form);
}

void VmSettingsDialog::buildFirmware()
{
    QVBoxLayout *page = addPage(QStringLiteral("Firmware"), Icons::fromTheme(QStringLiteral("preferences-system"),
                                                                            Icons::fromTheme(QStringLiteral("configure"))));
    const HostInfo &h = HostInfo::get();
    auto *form = makeForm();
    m_firmware = Combos::firmware(this);
    Combos::select(m_firmware, m_cfg.firmware);
    form->addRow(QStringLiteral("Firmware:"), m_firmware);
    m_secureBoot = new QCheckBox(QStringLiteral("Secure Boot capable firmware"), this);
    m_secureBoot->setChecked(m_cfg.secureBoot);
    m_secureBoot->setEnabled(!h.ovmfCodeSecure.isEmpty());
    form->addRow(QString(), m_secureBoot);
    m_tpm = new QCheckBox(QStringLiteral("TPM 2.0 (emulated)"), this);
    m_tpm->setChecked(m_cfg.tpm);
    if (h.swtpm.isEmpty()) {
        m_tpm->setEnabled(false);
        m_tpm->setToolTip(QStringLiteral("Install swtpm to enable an emulated TPM."));
        m_tpm->setText(QStringLiteral("TPM 2.0 (install swtpm to enable)"));
    }
    form->addRow(QString(), m_tpm);
    m_localtime = new QCheckBox(QStringLiteral("Hardware clock uses local time (Windows expects this)"), this);
    m_localtime->setChecked(m_cfg.localtime);
    form->addRow(QString(), m_localtime);
    m_hyperv = new QCheckBox(QStringLiteral("Hyper-V enlightenments (faster Windows guests)"), this);
    m_hyperv->setChecked(m_cfg.hyperv);
    form->addRow(QString(), m_hyperv);
    page->addLayout(form);
    page->addWidget(PageDialog::hint(QStringLiteral("Switching between BIOS and UEFI after installation usually makes "
                                                    "the installed system unbootable."), this));
    auto sync = [this, &h] {
        const bool uefi = m_firmware->currentData() == QLatin1String("uefi");
        m_secureBoot->setEnabled(uefi && !h.ovmfCodeSecure.isEmpty());
    };
    connect(m_firmware, &QComboBox::currentIndexChanged, this, sync);
    sync();
}

void VmSettingsDialog::buildAdvanced()
{
    QVBoxLayout *page = addPage(QStringLiteral("Advanced"), Icons::fromTheme(QStringLiteral("code-context"),
                                                                            Icons::fromTheme(QStringLiteral("utilities-terminal"))));
    auto *form = makeForm();
    m_extraArgs = new QLineEdit(m_cfg.extraArgs, this);
    m_extraArgs->setPlaceholderText(QStringLiteral("e.g. -device usb-host,vendorid=0x046d,productid=0xc52b"));
    form->addRow(QStringLiteral("Extra QEMU arguments:"), m_extraArgs);
    page->addLayout(form);

    auto *box = new QGroupBox(QStringLiteral("Command line"), this);
    auto *bl = new QVBoxLayout(box);
    m_command = new QPlainTextEdit(box);
    m_command->setReadOnly(true);
    m_command->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_command->setMinimumHeight(260);
    m_command->setLineWrapMode(QPlainTextEdit::NoWrap);
    bl->addWidget(m_command);
    auto *row = new QHBoxLayout;
    auto *copy = new QPushButton(Icons::fromTheme(QStringLiteral("edit-copy")), QStringLiteral("Copy"), box);
    auto *log = new QPushButton(Icons::fromTheme(QStringLiteral("text-x-log"), Icons::fromTheme(QStringLiteral("document-open"))),
                                QStringLiteral("Open Log"), box);
    row->addWidget(copy);
    row->addWidget(log);
    row->addStretch();
    bl->addLayout(row);
    page->addWidget(box);
    connect(m_extraArgs, &QLineEdit::textChanged, this, &VmSettingsDialog::refreshCommand);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_command->toPlainText()); });
    connect(log, &QPushButton::clicked, this, [this] {
        if (QFileInfo::exists(m_vm->logFile()))
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_vm->logFile()));
        else
            QMessageBox::information(this, QStringLiteral("No log yet"), QStringLiteral("The VM hasn't been started yet."));
    });
}

void VmSettingsDialog::refreshCommand()
{
    if (!m_command || !m_vm)
        return;
    const VmConfig c = collect();
    const QemuCommand cmd = buildQemuCommand(c, m_vm->runtimeDir());
    QString text = cmd.toShellString();
    if (!cmd.error.isEmpty())
        text = QStringLiteral("# Error: %1\n\n").arg(cmd.error) + text;
    for (const QString &w : cmd.warnings)
        text = QStringLiteral("# Warning: %1\n").arg(w) + text;
    m_command->setPlainText(text);
}

VmConfig VmSettingsDialog::collect() const
{
    VmConfig c = m_cfg;
    c.name = m_name->text().trimmed();
    c.osId = m_os->currentData().toString();
    if (c.osId != m_vm->config().osId)
        c.osLabel.clear();
    c.notes = m_notes->toPlainText();
    c.cores = m_cpu->cores();
    c.threads = m_cpu->threads();
    c.memoryMiB = m_mem->valueMiB();
    c.graphics = m_graphics->currentData().toString();
    c.hostmemMiB = m_hostmem->currentData().toInt();
    c.display = m_display->currentData().toString();
    c.autoResize = m_autoResize->isChecked();
    c.cdroms.clear();
    for (QLineEdit *cd : m_cd)
        c.cdroms << cd->text().trimmed();
    while (!c.cdroms.isEmpty() && c.cdroms.last().isEmpty())
        c.cdroms.removeLast();
    c.network = m_network->isChecked();
    c.nicModel = m_nic->currentData().toString();
    c.portForwards.clear();
    for (int r = 0; r < m_forwards->rowCount(); ++r) {
        PortForward f;
        if (auto *proto = qobject_cast<QComboBox *>(m_forwards->cellWidget(r, 0)))
            f.proto = proto->currentText();
        f.hostAddress = m_forwards->item(r, 1) ? m_forwards->item(r, 1)->text().trimmed() : QStringLiteral("127.0.0.1");
        f.hostPort = m_forwards->item(r, 2) ? m_forwards->item(r, 2)->text().toInt() : 0;
        f.guestPort = m_forwards->item(r, 3) ? m_forwards->item(r, 3)->text().toInt() : 0;
        if (f.hostPort > 0 && f.hostPort < 65536 && f.guestPort > 0 && f.guestPort < 65536)
            c.portForwards << f;
    }
    c.clipboard = m_clipboard->currentData().toString();
    c.fileTransfer = m_fileTransfer->isChecked();
    c.audio = m_audio->isChecked();
    c.firmware = m_firmware->currentData().toString();
    c.secureBoot = m_secureBoot->isChecked() && c.firmware == QLatin1String("uefi");
    c.tpm = m_tpm->isChecked();
    c.localtime = m_localtime->isChecked();
    c.hyperv = m_hyperv->isChecked();
    c.extraArgs = m_extraArgs->text().trimmed();
    return c;
}

bool VmSettingsDialog::apply()
{
    if (!m_vm)
        return false;
    const VmConfig c = collect();
    if (c.name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Name missing"), QStringLiteral("Please give the VM a name."));
        return false;
    }
    if (Vm *other = VmLibrary::instance()->findByName(c.name); other && other != m_vm) {
        QMessageBox::warning(this, QStringLiteral("Name in use"), QStringLiteral("Another VM is already called “%1”.").arg(c.name));
        return false;
    }
    if (c.firmware != m_vm->config().firmware && !c.disks.isEmpty()) {
        if (QMessageBox::question(this, QStringLiteral("Change firmware?"),
                                  QStringLiteral("An operating system installed with %1 usually won't boot with %2. Change it anyway?")
                                      .arg(m_vm->config().firmware.toUpper(), c.firmware.toUpper()))
            != QMessageBox::Yes)
            return false;
    }
    QString err;
    if (!m_vm->updateConfig(c, &err)) {
        QMessageBox::warning(this, QStringLiteral("Couldn't save"), err);
        return false;
    }
    m_cfg = m_vm->config();
    setWindowTitle(QStringLiteral("%1 — Settings").arg(m_cfg.name));
    return true;
}

void VmSettingsDialog::accept()
{
    if (apply())
        QDialog::accept();
}
