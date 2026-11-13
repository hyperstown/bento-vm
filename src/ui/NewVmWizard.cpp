#include "NewVmWizard.h"
#include "OsIcon.h"
#include "PageDialog.h"
#include "ResourceEditors.h"
#include "SettingsDialog.h"
#include "core/AppSettings.h"
#include "core/DiskOps.h"
#include "core/HostInfo.h"
#include "core/Paths.h"
#include "core/Vm.h"
#include "core/VmLibrary.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStyle>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

const char *kPageTitles[] = {"Choose the installation source", "Operating system", "Processor, memory and storage",
                             "Graphics and integration", "Ready to create"};

bool isIsoPath(const QString &p)
{
    const QString ext = QFileInfo(p).suffix().toLower();
    return ext == QLatin1String("iso");
}

bool loopbackPortFree(int port)
{
    const int s = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s < 0)
        return true;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(quint16(port));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool ok = ::bind(s, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0;
    ::close(s);
    return ok;
}

int suggestSshPort()
{
    QSet<int> used;
    for (Vm *vm : VmLibrary::instance()->vms())
        for (const PortForward &f : vm->config().portForwards)
            used.insert(f.hostPort);
    for (int p = 2222; p < 2300; ++p)
        if (!used.contains(p) && loopbackPortFree(p))
            return p;
    return 2222;
}

struct FoundIso
{
    QString path;
    QString profileId;
    QString label;
    qint64 size = 0;
    bool drivers = false;
};

QList<FoundIso> findIsos(const QStringList &extra)
{
    QStringList dirs;
    const QString home = QDir::homePath();
    dirs << QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    for (const QFileInfo &fi : QDir(home).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (fi.fileName().contains(QLatin1String("iso"), Qt::CaseInsensitive))
            dirs << fi.absoluteFilePath();
    QStringList files;
    for (const QString &d : std::as_const(dirs))
        for (const QFileInfo &fi : QDir(d).entryInfoList({QStringLiteral("*.iso"), QStringLiteral("*.ISO")}, QDir::Files))
            files << fi.absoluteFilePath();
    for (const QString &e : extra)
        if (QFileInfo::exists(e))
            files << QFileInfo(e).absoluteFilePath();
    files.removeDuplicates();

    QList<FoundIso> out;
    for (const QString &f : std::as_const(files)) {
        FoundIso iso;
        iso.path = f;
        iso.size = QFileInfo(f).size();
        const OsDb::IsoInfo info = OsDb::readIsoInfo(f);
        const QString base = QFileInfo(f).completeBaseName();
        iso.drivers = base.contains(QLatin1String("virtio-win"), Qt::CaseInsensitive)
                      || info.volumeId.startsWith(QLatin1String("virtio-win"), Qt::CaseInsensitive);
        iso.profileId = OsDb::guessFromText(info.volumeId);
        if (iso.profileId.isEmpty())
            iso.profileId = OsDb::guessFromText(base);
        if (iso.profileId.isEmpty())
            iso.profileId = QStringLiteral("other");
        iso.label = iso.drivers ? QStringLiteral("Windows VirtIO drivers") : OsDb::profile(iso.profileId).name;
        if (iso.profileId == QLatin1String("other") && !iso.drivers)
            iso.label = base;
        out << iso;
    }
    std::sort(out.begin(), out.end(), [](const FoundIso &a, const FoundIso &b) {
        return QFileInfo(a.path).lastModified() > QFileInfo(b.path).lastModified();
    });
    return out;
}

QLabel *bigLabel(const QString &text, QWidget *parent, qreal scale = 1.5)
{
    auto *l = new QLabel(text, parent);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * scale);
    f.setBold(true);
    l->setFont(f);
    l->setWordWrap(true);
    return l;
}

// Clicking anywhere on an option card selects its radio button.
class CardClickFilter : public QObject
{
public:
    CardClickFilter(QRadioButton *radio, QObject *parent)
        : QObject(parent)
        , m_radio(radio)
    {
    }
    bool eventFilter(QObject *, QEvent *e) override
    {
        if (e->type() == QEvent::MouseButtonPress)
            m_radio->setChecked(true);
        return false;
    }

private:
    QRadioButton *m_radio;
};

} // namespace

NewVmWizard::NewVmWizard(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("New Virtual Machine"));
    setAcceptDrops(true);
    resize(900, 700);

    m_stepTitle = bigLabel(QString(), this, 1.6);
    m_stepTitle->setObjectName(QStringLiteral("pageTitle"));
    m_stepCounter = new QLabel(this);
    m_stepCounter->setObjectName(QStringLiteral("mutedLabel"));
    auto *header = new QHBoxLayout;
    header->addWidget(m_stepTitle, 1);
    header->addWidget(m_stepCounter, 0, Qt::AlignBottom);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildSourcePage());
    m_pages->addWidget(buildOsPage());
    m_pages->addWidget(buildResourcesPage());
    m_pages->addWidget(buildFeaturesPage());
    m_pages->addWidget(buildSummaryPage());

    m_cancel = new QPushButton(QStringLiteral("Cancel"), this);
    m_back = new QPushButton(QIcon::fromTheme(QStringLiteral("go-previous")), QStringLiteral("Back"), this);
    m_next = new QPushButton(QIcon::fromTheme(QStringLiteral("go-next")), QStringLiteral("Next"), this);
    m_next->setDefault(true);
    m_next->setProperty("accent", true);
    m_next->setMinimumWidth(120);
    auto *footer = new QHBoxLayout;
    footer->addWidget(m_cancel);
    footer->addStretch();
    footer->addWidget(m_back);
    footer->addWidget(m_next);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(22, 18, 22, 16);
    lay->addLayout(header);
    lay->addSpacing(8);
    lay->addWidget(m_pages, 1);
    lay->addSpacing(6);
    lay->addLayout(footer);

    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_back, &QPushButton::clicked, this, &NewVmWizard::back);
    connect(m_next, &QPushButton::clicked, this, &NewVmWizard::next);
    connect(&m_detectWatcher, &QFutureWatcher<OsDb::Detection>::finished, this, &NewVmWizard::onDetected);

    goTo(SourcePage);
    scanForIsos();
}

// ---------------------------------------------------------------------------
// Pages

QFrame *NewVmWizard::optionCard(QRadioButton *radio, const QString &description, QWidget *extra)
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("optionCard"));
    card->setFrameShape(QFrame::StyledPanel);
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(16, 14, 16, 14);
    QFont f = radio->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.1);
    radio->setFont(f);
    lay->addWidget(radio);
    auto *desc = PageDialog::hint(description, card);
    desc->setContentsMargins(24, 0, 0, 4);
    lay->addWidget(desc);
    if (extra) {
        extra->setContentsMargins(24, 0, 0, 0);
        lay->addWidget(extra);
    }
    card->installEventFilter(new CardClickFilter(radio, card));
    return card;
}

QWidget *NewVmWizard::buildSourcePage()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(12);

    m_isoRadio = new QRadioButton(QStringLiteral("Install from an image file (ISO)"), page);
    m_diskRadio = new QRadioButton(QStringLiteral("Use an existing disk image"), page);
    auto *group = new QButtonGroup(page);
    group->addButton(m_isoRadio);
    group->addButton(m_diskRadio);
    m_isoRadio->setChecked(true);

    // ISO
    auto *isoExtra = new QWidget(page);
    auto *il = new QVBoxLayout(isoExtra);
    m_isoPath = new QLineEdit(isoExtra);
    m_isoPath->setPlaceholderText(QStringLiteral("Choose or drop an .iso file…"));
    m_isoPath->setClearButtonEnabled(true);
    auto *isoBrowse = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")), QStringLiteral("Browse…"), isoExtra);
    auto *ir = new QHBoxLayout;
    ir->addWidget(m_isoPath, 1);
    ir->addWidget(isoBrowse);
    il->addLayout(ir);
    auto *foundLabel = new QLabel(QStringLiteral("Found on this computer:"), isoExtra);
    foundLabel->setObjectName(QStringLiteral("mutedLabel"));
    il->addWidget(foundLabel);
    m_isoList = new QListWidget(isoExtra);
    m_isoList->setIconSize(QSize(32, 32));
    m_isoList->setMinimumHeight(170);
    il->addWidget(m_isoList);
    m_isoCard = optionCard(m_isoRadio, QStringLiteral("Install a fresh system from an installer image you downloaded, "
                                                       "for example Arch, Fedora, Ubuntu or Windows."), isoExtra);
    lay->addWidget(m_isoCard, 1);

    // Disk
    auto *diskExtra = new QWidget(page);
    auto *dl = new QVBoxLayout(diskExtra);
    m_diskPath = new QLineEdit(diskExtra);
    m_diskPath->setPlaceholderText(QStringLiteral("Choose a .qcow2, .img, .raw, .vmdk or .vdi file…"));
    m_diskPath->setClearButtonEnabled(true);
    auto *diskBrowse = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")), QStringLiteral("Browse…"), diskExtra);
    auto *dr = new QHBoxLayout;
    dr->addWidget(m_diskPath, 1);
    dr->addWidget(diskBrowse);
    dl->addLayout(dr);
    m_diskInPlace = new QRadioButton(QStringLiteral("Use the file where it is"), diskExtra);
    m_diskCopy = new QRadioButton(QStringLiteral("Copy it into the VM's folder"), diskExtra);
    m_diskInPlace->setChecked(true);
    auto *modeRow = new QHBoxLayout;
    modeRow->addWidget(m_diskInPlace);
    modeRow->addWidget(m_diskCopy);
    modeRow->addStretch();
    dl->addLayout(modeRow);
    m_diskInfo = new QLabel(diskExtra);
    m_diskInfo->setWordWrap(true);
    m_diskInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dl->addWidget(m_diskInfo);
    m_diskCard = optionCard(m_diskRadio, QStringLiteral("Boot a disk you already have, e.g. from virt-manager, GNOME Boxes "
                                                         "or a QEMU command line."), diskExtra);
    lay->addWidget(m_diskCard);

    auto sync = [this, isoExtra, diskExtra] {
        isoExtra->setEnabled(m_isoRadio->isChecked());
        diskExtra->setEnabled(m_diskRadio->isChecked());
        m_isoCard->setProperty("checked", m_isoRadio->isChecked());
        m_diskCard->setProperty("checked", m_diskRadio->isChecked());
        for (QFrame *c : {m_isoCard, m_diskCard}) {
            c->style()->unpolish(c);
            c->style()->polish(c);
        }
        onSourceChanged();
    };
    connect(m_isoRadio, &QRadioButton::toggled, this, sync);
    sync();

    connect(isoBrowse, &QPushButton::clicked, this, [this] {
        const QString start = m_isoPath->text().isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                                                          : QFileInfo(m_isoPath->text()).absolutePath();
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Choose an installation image"), start,
                                                       QStringLiteral("Disc images (*.iso *.img);;All files (*)"));
        if (!f.isEmpty())
            m_isoPath->setText(f);
    });
    connect(diskBrowse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Choose a disk image"), QDir::homePath(),
                                                       QStringLiteral("Disk images (*.qcow2 *.img *.raw *.vmdk *.vdi *.vhd *.vhdx);;All files (*)"));
        if (!f.isEmpty())
            m_diskPath->setText(f);
    });
    connect(m_isoPath, &QLineEdit::textChanged, this, &NewVmWizard::onSourceChanged);
    connect(m_diskPath, &QLineEdit::textChanged, this, [this] {
        updateDiskInfo();
        onSourceChanged();
    });
    connect(m_diskInPlace, &QRadioButton::toggled, this, &NewVmWizard::updateDiskInfo);
    connect(m_isoList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item && !item->data(Qt::UserRole).toString().isEmpty())
            m_isoPath->setText(item->data(Qt::UserRole).toString());
    });
    connect(m_isoList, &QListWidget::itemDoubleClicked, this, [this] { next(); });
    return page;
}

QWidget *NewVmWizard::buildOsPage()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);

    auto *banner = new QFrame(page);
    banner->setObjectName(QStringLiteral("card"));
    banner->setFrameShape(QFrame::StyledPanel);
    auto *bl = new QHBoxLayout(banner);
    bl->setContentsMargins(16, 16, 16, 16);
    m_detectIcon = new QLabel(banner);
    m_detectIcon->setFixedSize(64, 64);
    bl->addWidget(m_detectIcon);
    auto *bt = new QVBoxLayout;
    m_detectTitle = bigLabel(QString(), banner, 1.25);
    m_detectDetail = new QLabel(banner);
    m_detectDetail->setObjectName(QStringLiteral("mutedLabel"));
    m_detectDetail->setWordWrap(true);
    bt->addWidget(m_detectTitle);
    bt->addWidget(m_detectDetail);
    bl->addLayout(bt, 1);
    lay->addWidget(banner);
    lay->addSpacing(12);

    auto *form = PageDialog::makeForm();
    m_os = new QComboBox(page);
    m_os->setIconSize(QSize(20, 20));
    for (const OsProfile &p : OsDb::profiles())
        m_os->addItem(OsIcon::icon(p.id), p.name, p.id);
    form->addRow(QStringLiteral("Operating system:"), m_os);
    m_name = new QLineEdit(page);
    form->addRow(QStringLiteral("Name:"), m_name);
    lay->addLayout(form);
    lay->addSpacing(8);
    m_osHint = static_cast<QLabel *>(PageDialog::hint(QString(), page));
    lay->addWidget(m_osHint);
    lay->addStretch();

    connect(m_os, &QComboBox::currentIndexChanged, this, [this] {
        const QString osId = m_os->currentData().toString();
    const OsProfile &p = OsDb::profile(osId);
        m_osHint->setText(p.hint);
        m_detectIcon->setPixmap(OsIcon::badge(p.id, 64, devicePixelRatioF()));
        if (!m_nameEdited)
            m_name->setText(uniqueName(p.family == OsFamily::Windows ? QStringLiteral("Windows") : p.name));
    });
    connect(m_name, &QLineEdit::textEdited, this, [this] { m_nameEdited = true; });
    return page;
}

QWidget *NewVmWizard::buildResourcesPage()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);

    auto *cpuBox = new QGroupBox(QStringLiteral("Processor"), page);
    auto *cl = new QVBoxLayout(cpuBox);
    m_cpu = new CpuEditor(cpuBox);
    cl->addWidget(m_cpu);
    lay->addWidget(cpuBox);

    auto *memBox = new QGroupBox(QStringLiteral("Memory"), page);
    auto *ml = new QVBoxLayout(memBox);
    m_mem = new MemoryEditor(memBox);
    ml->addWidget(m_mem);
    lay->addWidget(memBox);

    auto *storeBox = new QGroupBox(QStringLiteral("Storage"), page);
    auto *sf = PageDialog::makeForm();
    m_diskSizeRow = new QWidget(storeBox);
    auto *dsr = new QHBoxLayout(m_diskSizeRow);
    dsr->setContentsMargins(0, 0, 0, 0);
    m_diskSize = new QSpinBox(m_diskSizeRow);
    m_diskSize->setRange(4, 65536);
    m_diskSize->setSuffix(QStringLiteral(" GiB"));
    dsr->addWidget(m_diskSize);
    dsr->addWidget(PageDialog::hint(QStringLiteral("Space is only used as the VM writes data."), m_diskSizeRow), 1);
    sf->addRow(QStringLiteral("Disk size:"), m_diskSizeRow);
    m_location = new QComboBox(storeBox);
    sf->addRow(QStringLiteral("Save in:"), m_location);
    m_locationInfo = new QLabel(storeBox);
    m_locationInfo->setObjectName(QStringLiteral("mutedLabel"));
    m_locationInfo->setWordWrap(true);
    sf->addRow(QString(), m_locationInfo);
    storeBox->setLayout(sf);
    lay->addWidget(storeBox);
    lay->addStretch();

    const AppSettings &s = AppSettings::instance();
    for (const QString &loc : s.storageLocations())
        m_location->addItem(QIcon::fromTheme(QStringLiteral("folder")), loc, loc);
    m_location->addItem(QIcon::fromTheme(QStringLiteral("document-open-folder")), QStringLiteral("Other folder…"), QString());
    Combos::select(m_location, s.defaultLocation());

    connect(m_location, &QComboBox::activated, this, [this](int i) {
        if (!m_location->itemData(i).toString().isEmpty()) {
            updateLocationInfo();
            return;
        }
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Where should the VM be stored?"), QDir::homePath());
        if (dir.isEmpty()) {
            Combos::select(m_location, AppSettings::instance().defaultLocation());
        } else {
            m_location->insertItem(i, QIcon::fromTheme(QStringLiteral("folder")), dir, dir);
            m_location->setCurrentIndex(i);
        }
        updateLocationInfo();
    });
    connect(m_diskSize, &QSpinBox::valueChanged, this, &NewVmWizard::updateLocationInfo);
    return page;
}

QWidget *NewVmWizard::buildFeaturesPage()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    const HostInfo &h = HostInfo::get();

    auto *gfx = new QGroupBox(QStringLiteral("Graphics"), page);
    auto *gf = PageDialog::makeForm();
    m_graphics = Combos::graphics(gfx);
    m_hostmem = Combos::hostmem(gfx);
    m_display = Combos::display(gfx);
    gf->addRow(QStringLiteral("Acceleration:"), m_graphics);
    gf->addRow(QStringLiteral("Venus memory window:"), m_hostmem);
    gf->addRow(QStringLiteral("Show the VM in:"), m_display);
    m_featureHint = static_cast<QLabel *>(PageDialog::hint(QString(), gfx));
    gf->addRow(QString(), m_featureHint);
    gfx->setLayout(gf);
    lay->addWidget(gfx);

    auto *fw = new QGroupBox(QStringLiteral("Firmware"), page);
    auto *ff = PageDialog::makeForm();
    m_firmware = Combos::firmware(fw);
    ff->addRow(QStringLiteral("Boot firmware:"), m_firmware);
    m_secureBoot = new QCheckBox(QStringLiteral("Secure Boot capable"), fw);
    m_tpm = new QCheckBox(h.swtpm.isEmpty() ? QStringLiteral("TPM 2.0 (install swtpm to enable)") : QStringLiteral("TPM 2.0"), fw);
    m_tpm->setEnabled(!h.swtpm.isEmpty());
    auto *fwRow = new QHBoxLayout;
    fwRow->addWidget(m_secureBoot);
    fwRow->addWidget(m_tpm);
    fwRow->addStretch();
    ff->addRow(QString(), fwRow);
    fw->setLayout(ff);
    lay->addWidget(fw);

    auto *integ = new QGroupBox(QStringLiteral("Integration"), page);
    auto *inf = PageDialog::makeForm();
    m_clipboard = Combos::clipboard(integ);
    inf->addRow(QStringLiteral("Clipboard sharing:"), m_clipboard);
    m_fileTransfer = new QCheckBox(QStringLiteral("Drag && drop files into the VM"), integ);
    m_audio = new QCheckBox(QStringLiteral("Sound"), integ);
    auto *iRow = new QHBoxLayout;
    iRow->addWidget(m_fileTransfer);
    iRow->addWidget(m_audio);
    iRow->addStretch();
    inf->addRow(QString(), iRow);
    m_ssh = new QCheckBox(QStringLiteral("Forward SSH from host port"), integ);
    m_sshPort = new QSpinBox(integ);
    m_sshPort->setRange(1024, 65535);
    auto *sRow = new QHBoxLayout;
    sRow->addWidget(m_ssh);
    sRow->addWidget(m_sshPort);
    sRow->addWidget(new QLabel(QStringLiteral("→ guest port 22"), integ));
    sRow->addStretch();
    inf->addRow(QStringLiteral("Network:"), sRow);
    integ->setLayout(inf);
    lay->addWidget(integ);
    lay->addStretch();

    connect(m_graphics, &QComboBox::currentIndexChanged, this, &NewVmWizard::updateFeatureHints);
    connect(m_display, &QComboBox::currentIndexChanged, this, &NewVmWizard::updateFeatureHints);
    connect(m_firmware, &QComboBox::currentIndexChanged, this, &NewVmWizard::updateFeatureHints);
    connect(m_ssh, &QCheckBox::toggled, m_sshPort, &QWidget::setEnabled);
    return page;
}

QWidget *NewVmWizard::buildSummaryPage()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    auto *card = new QFrame(page);
    card->setObjectName(QStringLiteral("card"));
    card->setFrameShape(QFrame::StyledPanel);
    auto *cl = new QVBoxLayout(card);
    cl->setContentsMargins(18, 16, 18, 16);
    m_summary = new QLabel(card);
    m_summary->setTextFormat(Qt::RichText);
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cl->addWidget(m_summary);
    lay->addWidget(card);
    lay->addSpacing(10);
    m_startNow = new QCheckBox(QStringLiteral("Start the VM right away"), page);
    m_startNow->setChecked(true);
    lay->addWidget(m_startNow);
    lay->addStretch();
    return page;
}

// ---------------------------------------------------------------------------
// Navigation

void NewVmWizard::goTo(int page)
{
    if (page == OsPage && m_detectedFor != sourcePath())
        startDetection();
    if (page == ResourcesPage || page == FeaturesPage) {
        if (m_appliedProfileFor != m_os->currentData().toString() + sourcePath())
            applyProfile();
        updateLocationInfo();
        updateFeatureHints();
    }
    if (page == SummaryPage)
        updateSummary();
    m_pages->setCurrentIndex(page);
    updateStepLabel();
    updateButtons();
}

void NewVmWizard::next()
{
    const int page = m_pages->currentIndex();
    if (!validatePage(page))
        return;
    if (page == SummaryPage) {
        create();
        return;
    }
    goTo(page + 1);
}

void NewVmWizard::back()
{
    if (m_pages->currentIndex() > 0)
        goTo(m_pages->currentIndex() - 1);
}

void NewVmWizard::updateStepLabel()
{
    const int i = m_pages->currentIndex();
    m_stepTitle->setText(QString::fromUtf8(kPageTitles[i]));
    m_stepCounter->setText(QStringLiteral("Step %1 of %2").arg(i + 1).arg(int(PageCount)));
}

void NewVmWizard::updateButtons()
{
    const int i = m_pages->currentIndex();
    m_back->setEnabled(i > 0);
    m_next->setText(i == SummaryPage ? QStringLiteral("Create") : QStringLiteral("Next"));
    m_next->setIcon(QIcon::fromTheme(i == SummaryPage ? QStringLiteral("dialog-ok-apply") : QStringLiteral("go-next")));
    if (i == SourcePage)
        m_next->setEnabled(!sourcePath().isEmpty());
    else
        m_next->setEnabled(true);
}

bool NewVmWizard::validatePage(int page)
{
    auto fail = [this](const QString &t, const QString &m) {
        QMessageBox::warning(this, t, m);
        return false;
    };
    switch (page) {
    case SourcePage: {
        const QString p = sourcePath();
        const QFileInfo fi(p);
        if (!fi.isFile())
            return fail(QStringLiteral("File not found"), QStringLiteral("“%1” doesn't exist.").arg(p));
        if (::access(QFile::encodeName(p).constData(), R_OK) != 0)
            return fail(QStringLiteral("No permission"),
                        QStringLiteral("You can't read “%1”. Bento runs VMs as your user, without root.\n\n"
                                       "Fix it with:\n  sudo chown $USER: '%1'").arg(p));
        if (!isoMode() && m_diskInPlace->isChecked() && ::access(QFile::encodeName(p).constData(), W_OK) != 0)
            return fail(QStringLiteral("Disk is read-only for you"),
                        QStringLiteral("You can't write to “%1”, so the VM couldn't boot from it in place.\n\n"
                                       "Choose “Copy it into the VM's folder”, or fix the permissions:\n  sudo chown $USER: '%1'").arg(p));
        return true;
    }
    case OsPage: {
        const QString name = m_name->text().trimmed();
        if (name.isEmpty())
            return fail(QStringLiteral("Name missing"), QStringLiteral("Please give the VM a name."));
        if (VmLibrary::instance()->nameInUse(name))
            return fail(QStringLiteral("Name in use"), QStringLiteral("There already is a VM called “%1”.").arg(name));
        return true;
    }
    case ResourcesPage: {
        const QString loc = m_location->currentData().toString();
        if (loc.isEmpty() || !Paths::ensureDir(loc))
            return fail(QStringLiteral("Can't use this folder"),
                        QStringLiteral("“%1” isn't available or can't be created.").arg(loc));
        if (::access(QFile::encodeName(loc).constData(), W_OK) != 0)
            return fail(QStringLiteral("Can't use this folder"), QStringLiteral("You can't write to “%1”.").arg(loc));
        return true;
    }
    case FeaturesPage: {
        const HostInfo &h = HostInfo::get();
        if (m_firmware->currentData() == QLatin1String("uefi") && h.ovmfCode.isEmpty() && h.ovmfCodeSecure.isEmpty())
            return fail(QStringLiteral("UEFI firmware missing"),
                        QStringLiteral("OVMF wasn't found. Install the edk2-ovmf package or choose BIOS."));
        return true;
    }
    default:
        return true;
    }
}

// ---------------------------------------------------------------------------
// Source & detection

bool NewVmWizard::isoMode() const
{
    return m_isoRadio->isChecked();
}

QString NewVmWizard::sourcePath() const
{
    return (isoMode() ? m_isoPath : m_diskPath)->text().trimmed();
}

void NewVmWizard::setSourcePath(const QString &path)
{
    const QString lower = path.toLower();
    if (isIsoPath(path)) {
        m_isoRadio->setChecked(true);
        m_isoPath->setText(path);
    } else if (lower.endsWith(QLatin1String(".qcow2")) || lower.endsWith(QLatin1String(".vmdk"))
               || lower.endsWith(QLatin1String(".vdi")) || lower.endsWith(QLatin1String(".raw"))
               || lower.endsWith(QLatin1String(".img")) || lower.endsWith(QLatin1String(".vhdx"))) {
        m_diskRadio->setChecked(true);
        m_diskPath->setText(path);
    }
}

void NewVmWizard::onSourceChanged()
{
    if (m_next)
        updateButtons();
}

void NewVmWizard::scanForIsos()
{
    const QStringList recent = AppSettings::instance().value(QStringLiteral("wizard/recentIsos")).toStringList();
    auto *watcher = new QFutureWatcher<QList<FoundIso>>(this);
    connect(watcher, &QFutureWatcher<QList<FoundIso>>::finished, this, [this, watcher] {
        const QList<FoundIso> found = watcher->result();
        watcher->deleteLater();
        m_isoList->clear();
        for (const FoundIso &iso : found) {
            if (iso.drivers) {
                m_driverIso = iso.path;
                continue;
            }
            auto *item = new QListWidgetItem(OsIcon::icon(iso.profileId),
                                             QStringLiteral("%1\n%2 · %3").arg(iso.label, QFileInfo(iso.path).fileName(),
                                                                               Paths::humanSize(iso.size)),
                                             m_isoList);
            item->setData(Qt::UserRole, iso.path);
            item->setToolTip(iso.path);
        }
        if (m_isoList->count() == 0) {
            auto *item = new QListWidgetItem(QStringLiteral("No ISO images found in Downloads. Use Browse… or drop one here."), m_isoList);
            item->setFlags(Qt::NoItemFlags);
        }
    });
    watcher->setFuture(QtConcurrent::run([recent] { return findIsos(recent); }));
}

void NewVmWizard::startDetection()
{
    const QString path = sourcePath();
    m_detectedFor = path;
    if (!isoMode()) {
        // Disk images: best effort guess from the file name.
        const QString guess = OsDb::guessFromText(QFileInfo(path).completeBaseName());
        m_detection = {};
        m_detection.profileId = guess.isEmpty() ? QStringLiteral("linux") : guess;
        m_detection.recognized = !guess.isEmpty();
        m_detectTitle->setText(QStringLiteral("Which system is installed on this disk?"));
        m_detectDetail->setText(m_detection.recognized
                                    ? QStringLiteral("Guessed from the file name. Change it below if that's wrong.")
                                    : QStringLiteral("Pick the operating system below so Bento can choose matching settings."));
        Combos::select(m_os, m_detection.profileId);
        Q_EMIT m_os->currentIndexChanged(m_os->currentIndex());
        return;
    }
    m_detectTitle->setText(QStringLiteral("Detecting the operating system…"));
    m_detectDetail->setText(QFileInfo(path).fileName());
    m_detectIcon->setPixmap(QIcon::fromTheme(QStringLiteral("search")).pixmap(64, 64));
    m_next->setEnabled(false);
    m_detectWatcher.setFuture(QtConcurrent::run([path] { return OsDb::detect(path); }));
}

void NewVmWizard::onDetected()
{
    m_detection = m_detectWatcher.result();
    m_next->setEnabled(true);
    if (m_detection.recognized) {
        m_detectTitle->setText(QStringLiteral("Detected %1").arg(m_detection.label));
        m_detectDetail->setText(m_detection.volumeId.isEmpty() ? QString()
                                                               : QStringLiteral("Volume label: %1").arg(m_detection.volumeId));
    } else {
        m_detectTitle->setText(QStringLiteral("Couldn't identify this image"));
        m_detectDetail->setText(QStringLiteral("Volume label: “%1”. Pick the operating system below.").arg(m_detection.volumeId));
    }
    if (!m_detection.bootable)
        m_detectDetail->setText(m_detectDetail->text()
                                + QStringLiteral("\nThis image doesn't look bootable; it may be a driver or data disc."));
    Combos::select(m_os, m_detection.profileId);
    Q_EMIT m_os->currentIndexChanged(m_os->currentIndex());
}

void NewVmWizard::applyProfile()
{
    const QString osId = m_os->currentData().toString();
    const OsProfile &p = OsDb::profile(osId);
    const HostInfo &h = HostInfo::get();
    const AppSettings &s = AppSettings::instance();
    m_appliedProfileFor = p.id + sourcePath();

    const Recommendation rec = OsDb::recommend(p, h);
    m_cpu->setRecommended(rec.cores, rec.threads);
    m_cpu->setValue(rec.cores, rec.threads);
    m_mem->setRecommended(rec.memoryMiB);
    m_mem->setValueMiB(rec.memoryMiB);
    m_diskSize->setValue(rec.diskGiB);
    m_diskSizeRow->setEnabled(isoMode());

    const bool linuxGuest = p.family == OsFamily::Linux;
    Combos::select(m_graphics, linuxGuest ? s.defaultGraphics() : p.graphics);
    Combos::select(m_hostmem, s.defaultHostmemMiB());
    Combos::select(m_display, s.defaultDisplay());
    QString fw = p.family == OsFamily::Windows ? p.firmware : (linuxGuest ? s.defaultFirmware() : p.firmware);
    if (!isoMode())
        fw = QStringLiteral("bios");   // most hand-made QEMU disks were installed with SeaBIOS
    Combos::select(m_firmware, fw);
    m_secureBoot->setChecked(p.secureBoot && !h.ovmfCodeSecure.isEmpty());
    m_tpm->setChecked(p.tpm && !h.swtpm.isEmpty());
    Combos::select(m_clipboard, s.defaultClipboard());
    m_fileTransfer->setChecked(s.defaultFileTransfer());
    m_audio->setChecked(true);
    m_ssh->setChecked(linuxGuest);
    m_sshPort->setValue(suggestSshPort());
    m_sshPort->setEnabled(m_ssh->isChecked());
}

void NewVmWizard::updateFeatureHints()
{
    const QString osId = m_os->currentData().toString();
    const OsProfile &p = OsDb::profile(osId);
    const QString g = m_graphics->currentData().toString();
    m_hostmem->setEnabled(g == QLatin1String("venus"));
    QString hint;
    if (p.family == OsFamily::Windows && g != QLatin1String("2d"))
        hint = QStringLiteral("Windows has no VirGL/Venus drivers; “Standard” is the right choice for Windows.");
    else if (g == QLatin1String("venus"))
        hint = QStringLiteral("Venus passes Vulkan through to your GPU. Inside the guest install Mesa's virtio Vulkan "
                              "driver (Arch: vulkan-virtio, Fedora/Ubuntu: mesa-vulkan-drivers).");
    else if (g == QLatin1String("virgl"))
        hint = QStringLiteral("OpenGL acceleration through virglrenderer. Works with any recent Linux guest out of the box.");
    else
        hint = QStringLiteral("A simple framebuffer, works with every operating system.");
    if (m_display->currentData() != QLatin1String("embedded"))
        hint += QStringLiteral("\nWith a QEMU window the clipboard is shared both ways or not at all, and files can't be dropped in.");
    m_featureHint->setText(hint);
    const bool uefi = m_firmware->currentData() == QLatin1String("uefi");
    m_secureBoot->setEnabled(uefi && !HostInfo::get().ovmfCodeSecure.isEmpty());
}

void NewVmWizard::updateDiskInfo()
{
    const QString p = m_diskPath->text().trimmed();
    if (p.isEmpty() || !QFileInfo(p).isFile()) {
        m_diskInfo->setText(p.isEmpty() ? QString() : QStringLiteral("File not found."));
        return;
    }
    const QByteArray enc = QFile::encodeName(p);
    const bool readable = ::access(enc.constData(), R_OK) == 0;
    const bool writable = ::access(enc.constData(), W_OK) == 0;
    QString text;
    if (!readable) {
        text = QStringLiteral("<span style='color:#ef4444'>You can't read this file.</span> Fix it with "
                              "<code>sudo chown $USER: '%1'</code>").arg(p.toHtmlEscaped());
    } else {
        const DiskOps::ImageInfo info = DiskOps::imageInfo(p);
        if (info.ok)
            text = QStringLiteral("%1 image · %2 virtual size · %3 used on disk")
                       .arg(info.format.toUpper(), Paths::humanSize(info.virtualSize), Paths::humanSize(info.actualSize));
        else
            text = QStringLiteral("Couldn't inspect the image: %1").arg(info.error.toHtmlEscaped());
        if (!writable) {
            text += QStringLiteral("<br><span style='color:#e8a317'>The file is read-only for you, so it will be copied. "
                                   "To use it in place run <code>sudo chown $USER: '%1'</code>.</span>").arg(p.toHtmlEscaped());
            m_diskCopy->setChecked(true);
        }
    }
    m_diskInPlace->setEnabled(writable);
    m_diskInfo->setText(text);
}

void NewVmWizard::updateLocationInfo()
{
    const QString loc = m_location->currentData().toString();
    if (loc.isEmpty())
        return;
    QString existing = loc;
    while (!existing.isEmpty() && !QFileInfo::exists(existing))
        existing = QFileInfo(existing).absolutePath() == existing ? QString() : QFileInfo(existing).absolutePath();
    if (existing.isEmpty()) {
        m_locationInfo->setText(QStringLiteral("This location isn't available."));
        return;
    }
    const QStorageInfo si(existing);
    QString text = QStringLiteral("%1 free on %2").arg(Paths::humanSize(si.bytesAvailable()), si.rootPath());
    qint64 need = isoMode() ? qint64(m_diskSize->value()) * 1024 * 1024 * 1024 : 0;
    if (!isoMode() && m_diskCopy->isChecked())
        need = QFileInfo(m_diskPath->text()).size();
    if (need > si.bytesAvailable())
        text += QStringLiteral(" — not enough space if the disk fills up completely");
    m_locationInfo->setText(text);
}

QString NewVmWizard::uniqueName(const QString &base) const
{
    if (!VmLibrary::instance()->nameInUse(base))
        return base;
    for (int n = 2;; ++n) {
        const QString c = QStringLiteral("%1 %2").arg(base).arg(n);
        if (!VmLibrary::instance()->nameInUse(c))
            return c;
    }
}

void NewVmWizard::updateSummary()
{
    const QString osId = m_os->currentData().toString();
    const OsProfile &p = OsDb::profile(osId);
    auto row = [](const QString &k, const QString &v) {
        return QStringLiteral("<tr><td style='padding:3px 18px 3px 0; color:gray'>%1</td><td style='padding:3px 0'>%2</td></tr>")
            .arg(k, v.toHtmlEscaped());
    };
    QString html = QStringLiteral("<h3>%1</h3><table>").arg(m_name->text().toHtmlEscaped());
    html += row(QStringLiteral("System"), (isoMode() && m_detection.profileId == p.id && m_detection.recognized) ? m_detection.label : p.name);
    html += row(isoMode() ? QStringLiteral("Installer") : QStringLiteral("Disk"),
                QStringLiteral("%1%2").arg(sourcePath(), !isoMode() && m_diskCopy->isChecked() ? QStringLiteral(" (copied)") : QString()));
    html += row(QStringLiteral("Processor"), QStringLiteral("%1 cores × %2 threads = %3 vCPUs")
                                                 .arg(m_cpu->cores()).arg(m_cpu->threads()).arg(m_cpu->cores() * m_cpu->threads()));
    html += row(QStringLiteral("Memory"), QStringLiteral("%1 GiB").arg(m_mem->valueMiB() / 1024.0, 0, 'f', 1));
    if (isoMode())
        html += row(QStringLiteral("New disk"), QStringLiteral("%1 GiB (%2)").arg(m_diskSize->value()).arg(p.diskBus));
    html += row(QStringLiteral("Folder"), VmLibrary::uniqueFolder(m_location->currentData().toString(), m_name->text().trimmed()));
    html += row(QStringLiteral("Graphics"), m_graphics->currentText());
    html += row(QStringLiteral("Display"), m_display->currentText());
    QString fw = m_firmware->currentText();
    if (m_secureBoot->isChecked() && m_secureBoot->isEnabled())
        fw += QStringLiteral(", Secure Boot");
    if (m_tpm->isChecked())
        fw += QStringLiteral(", TPM 2.0");
    html += row(QStringLiteral("Firmware"), fw);
    html += row(QStringLiteral("Clipboard"), m_clipboard->currentText());
    html += row(QStringLiteral("File drop"), m_fileTransfer->isChecked() ? QStringLiteral("Enabled") : QStringLiteral("Disabled"));
    if (m_ssh->isChecked())
        html += row(QStringLiteral("SSH"), QStringLiteral("ssh -p %1 user@localhost").arg(m_sshPort->value()));
    if (p.family == OsFamily::Windows && isoMode() && !m_driverIso.isEmpty())
        html += row(QStringLiteral("Drivers"), QStringLiteral("%1 attached as second CD").arg(QFileInfo(m_driverIso).fileName()));
    html += QStringLiteral("</table>");
    if (!p.hint.isEmpty())
        html += QStringLiteral("<p style='color:gray'>%1</p>").arg(p.hint.toHtmlEscaped());
    m_summary->setText(html);
}

// ---------------------------------------------------------------------------
// Creation

void NewVmWizard::create()
{
    const QString osId = m_os->currentData().toString();
    const OsProfile &p = OsDb::profile(osId);
    VmConfig c;
    c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.name = m_name->text().trimmed();
    c.osId = p.id;
    c.osLabel = (isoMode() && m_detection.recognized && m_detection.profileId == p.id) ? m_detection.label : QString();
    c.cores = m_cpu->cores();
    c.threads = m_cpu->threads();
    c.memoryMiB = m_mem->valueMiB();
    c.firmware = m_firmware->currentData().toString();
    c.secureBoot = c.firmware == QLatin1String("uefi") && m_secureBoot->isChecked() && m_secureBoot->isEnabled();
    c.tpm = m_tpm->isChecked() && m_tpm->isEnabled();
    c.graphics = m_graphics->currentData().toString();
    c.hostmemMiB = m_hostmem->currentData().toInt();
    c.display = m_display->currentData().toString();
    c.clipboard = m_clipboard->currentData().toString();
    c.fileTransfer = m_fileTransfer->isChecked();
    c.audio = m_audio->isChecked();
    c.localtime = p.localtime;
    c.hyperv = p.hyperv;
    c.nicModel = p.nicModel;
    c.network = true;
    if (m_ssh->isChecked()) {
        PortForward f;
        f.hostPort = m_sshPort->value();
        f.guestPort = 22;
        c.portForwards << f;
    }
    c.created = QDateTime::currentDateTime();

    const QString location = m_location->currentData().toString();
    const QString dir = VmLibrary::uniqueFolder(location, c.name);
    if (!Paths::ensureDir(dir)) {
        QMessageBox::warning(this, QStringLiteral("Couldn't create the VM"), QStringLiteral("Can't create %1").arg(dir));
        return;
    }
    c.dir = dir;
    auto cleanup = [dir] { QDir(dir).removeRecursively(); };

    if (isoMode()) {
        QString err;
        if (!DiskOps::createDisk(dir + QStringLiteral("/disk.qcow2"), m_diskSize->value(), &err)) {
            cleanup();
            QMessageBox::warning(this, QStringLiteral("Couldn't create the disk"), err);
            return;
        }
        c.disks << DiskSpec{QStringLiteral("disk.qcow2"), QStringLiteral("qcow2"), p.diskBus};
        c.cdroms << sourcePath();
        if (p.family == OsFamily::Windows && !m_driverIso.isEmpty())
            c.cdroms << m_driverIso;
        QStringList recent = AppSettings::instance().value(QStringLiteral("wizard/recentIsos")).toStringList();
        recent.removeAll(sourcePath());
        recent.prepend(sourcePath());
        AppSettings::instance().setValue(QStringLiteral("wizard/recentIsos"), recent.mid(0, 10));
    } else {
        // Imported systems keep SATA: disks booted with "-hda" (AHCI on q35) often have an
        // initramfs without virtio drivers (mkinitcpio's autodetect hook). Switch later in Settings.
        const QString src = QFileInfo(sourcePath()).absoluteFilePath();
        const DiskOps::ImageInfo info = DiskOps::imageInfo(src);
        const QString fmt = info.ok ? info.format : DiskOps::formatForPath(src);
        if (m_diskCopy->isChecked()) {
            const QString ext = QFileInfo(src).suffix().isEmpty() ? fmt : QFileInfo(src).suffix();
            const QString destName = QStringLiteral("disk.") + ext;
            QProgressDialog progress(QStringLiteral("Copying the disk image…"), QStringLiteral("Cancel"), 0, 100, this);
            progress.setWindowModality(Qt::WindowModal);
            progress.setMinimumDuration(0);
            DiskCopyJob job(src, dir + QLatin1Char('/') + destName);
            QEventLoop loop;
            bool ok = false;
            QString error;
            connect(&job, &DiskCopyJob::progress, &progress, &QProgressDialog::setValue);
            connect(&job, &DiskCopyJob::finished, &loop, [&](bool success, const QString &e) {
                ok = success;
                error = e;
                loop.quit();
            });
            connect(&progress, &QProgressDialog::canceled, &job, &DiskCopyJob::cancel);
            job.start();
            loop.exec();
            if (!ok) {
                cleanup();
                if (!progress.wasCanceled())
                    QMessageBox::warning(this, QStringLiteral("Copy failed"), error);
                return;
            }
            c.disks << DiskSpec{destName, fmt, QStringLiteral("sata")};
        } else {
            c.disks << DiskSpec{src, fmt, QStringLiteral("sata")};
        }
    }

    QString err;
    if (!c.save(&err)) {
        cleanup();
        QMessageBox::warning(this, QStringLiteral("Couldn't save the VM"), err);
        return;
    }
    Vm *vm = VmLibrary::instance()->add(c);
    Q_EMIT vmCreated(vm, m_startNow->isChecked());
    accept();
}

// ---------------------------------------------------------------------------

void NewVmWizard::dragEnterEvent(QDragEnterEvent *e)
{
    if (m_pages->currentIndex() == SourcePage && e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void NewVmWizard::dropEvent(QDropEvent *e)
{
    for (const QUrl &u : e->mimeData()->urls()) {
        if (u.isLocalFile()) {
            setSourcePath(u.toLocalFile());
            break;
        }
    }
}
