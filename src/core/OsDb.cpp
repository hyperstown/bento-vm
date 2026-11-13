#include "OsDb.h"
#include "Paths.h"
#include "HostInfo.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace OsDb {

static const QString kLinuxHint = QStringLiteral(
    "For Vulkan inside the VM (Venus) install the Mesa virtio Vulkan driver in the guest "
    "(Arch: vulkan-virtio, Fedora/Ubuntu/Debian: mesa-vulkan-drivers). Install spice-vdagent "
    "for clipboard sharing and file drag & drop.");

static const QString kWindowsHint = QStringLiteral(
    "Venus/VirGL have no Windows guest drivers, so Windows uses a standard display. "
    "Install the virtio-win guest tools (includes the SPICE agent) for clipboard sharing and file drag & drop.");

static OsProfile linuxProfile(const char *id, const char *name, const char *color, const char *badge,
                              int recRam = 4096, int recDisk = 64)
{
    OsProfile p;
    p.id = QString::fromLatin1(id);
    p.name = QString::fromUtf8(name);
    p.family = OsFamily::Linux;
    p.color = QColor(QString::fromLatin1(color));
    p.badge = QString::fromUtf8(badge);
    p.minRamMiB = 1024;
    p.recRamMiB = recRam;
    p.minCpus = 1;
    p.recCpus = 4;
    p.recDiskGiB = recDisk;
    p.hint = kLinuxHint;
    return p;
}

static QList<OsProfile> buildProfiles()
{
    QList<OsProfile> l;
    l << linuxProfile("archlinux", "Arch Linux", "#1793D1", "A");
    l << linuxProfile("manjaro", "Manjaro", "#35BF5C", "M");
    l << linuxProfile("endeavouros", "EndeavourOS", "#7F3FBF", "E");
    l << linuxProfile("cachyos", "CachyOS", "#00B5A0", "C");
    l << linuxProfile("fedora", "Fedora", "#51A2DA", "F");
    l << linuxProfile("bazzite", "Bazzite", "#8B5CF6", "B");
    l << linuxProfile("ubuntu", "Ubuntu", "#E95420", "U");
    l << linuxProfile("kubuntu", "Kubuntu", "#0079C1", "K");
    l << linuxProfile("linuxmint", "Linux Mint", "#87CF3E", "LM");
    l << linuxProfile("popos", "Pop!_OS", "#48B9C7", "P");
    l << linuxProfile("elementary", "elementary OS", "#64BAFF", "e");
    l << linuxProfile("zorin", "Zorin OS", "#15A6F0", "Z");
    l << linuxProfile("debian", "Debian", "#A80030", "D");
    l << linuxProfile("kali", "Kali Linux", "#367BF0", "K");
    l << linuxProfile("opensuse", "openSUSE", "#73BA25", "S");
    l << linuxProfile("nixos", "NixOS", "#7EBAE4", "N");
    l << linuxProfile("rocky", "Rocky Linux", "#10B981", "R");
    l << linuxProfile("almalinux", "AlmaLinux", "#0F4266", "Al");
    l << linuxProfile("centos", "CentOS", "#9C2C7A", "C");
    l << linuxProfile("rhel", "Red Hat Enterprise Linux", "#EE0000", "RH");
    l << linuxProfile("alpine", "Alpine Linux", "#0D597F", "Al", 1024, 16);
    l << linuxProfile("void", "Void Linux", "#478061", "V");
    l << linuxProfile("gentoo", "Gentoo", "#54487A", "G");
    l << linuxProfile("linux", "Other Linux", "#5E6472", "L");

    {
        OsProfile p;
        p.id = QStringLiteral("windows");
        p.name = QStringLiteral("Windows 10 / 11");
        p.family = OsFamily::Windows;
        p.color = QColor(QStringLiteral("#0078D4"));
        p.badge = QStringLiteral("W");
        p.minRamMiB = 4096;
        p.recRamMiB = 8192;
        p.minCpus = 2;
        p.recCpus = 4;
        p.recDiskGiB = 128;
        p.graphics = QStringLiteral("2d");
        p.firmware = QStringLiteral("uefi");
        p.secureBoot = true;
        p.tpm = true;
        p.diskBus = QStringLiteral("sata");
        p.nicModel = QStringLiteral("e1000e");
        p.localtime = true;
        p.hyperv = true;
        p.hint = kWindowsHint;
        l << p;
    }
    {
        OsProfile p;
        p.id = QStringLiteral("windows7");
        p.name = QStringLiteral("Windows 7");
        p.family = OsFamily::Windows;
        p.color = QColor(QStringLiteral("#00A2ED"));
        p.badge = QStringLiteral("W7");
        p.minRamMiB = 2048;
        p.recRamMiB = 4096;
        p.recCpus = 2;
        p.recDiskGiB = 64;
        p.graphics = QStringLiteral("2d");
        p.firmware = QStringLiteral("bios");
        p.diskBus = QStringLiteral("sata");
        p.nicModel = QStringLiteral("e1000");
        p.localtime = true;
        p.hyperv = true;
        p.hint = kWindowsHint;
        l << p;
    }
    {
        OsProfile p;
        p.id = QStringLiteral("freebsd");
        p.name = QStringLiteral("FreeBSD");
        p.family = OsFamily::Bsd;
        p.color = QColor(QStringLiteral("#AB2B28"));
        p.badge = QStringLiteral("B");
        p.recRamMiB = 4096;
        p.recCpus = 2;
        p.recDiskGiB = 32;
        p.graphics = QStringLiteral("2d");
        l << p;
    }
    {
        OsProfile p;
        p.id = QStringLiteral("android");
        p.name = QStringLiteral("Android-x86");
        p.family = OsFamily::Android;
        p.color = QColor(QStringLiteral("#3DDC84"));
        p.badge = QStringLiteral("A");
        p.recRamMiB = 4096;
        p.recCpus = 4;
        p.recDiskGiB = 32;
        p.graphics = QStringLiteral("virgl");
        p.firmware = QStringLiteral("bios");
        p.diskBus = QStringLiteral("sata");
        l << p;
    }
    {
        OsProfile p;
        p.id = QStringLiteral("other");
        p.name = QStringLiteral("Other operating system");
        p.family = OsFamily::Other;
        p.color = QColor(QStringLiteral("#6B7280"));
        p.badge = QStringLiteral("?");
        p.recRamMiB = 2048;
        p.recCpus = 2;
        p.recDiskGiB = 32;
        p.graphics = QStringLiteral("2d");
        p.firmware = QStringLiteral("bios");
        p.diskBus = QStringLiteral("sata");
        p.nicModel = QStringLiteral("e1000e");
        l << p;
    }
    return l;
}

const QList<OsProfile> &profiles()
{
    static const QList<OsProfile> p = buildProfiles();
    return p;
}

const OsProfile &profile(const QString &id)
{
    for (const OsProfile &p : profiles())
        if (p.id == id)
            return p;
    for (const OsProfile &p : profiles())
        if (p.id == QLatin1String("other"))
            return p;
    return profiles().last();
}

struct Rule
{
    const char *id;
    const char *pattern;
};

// Order matters: more specific names first.
static const Rule kRules[] = {
    {"archlinux", R"(^ARCH_\d{6}|\barch[\s_-]*linux|^archlinux|\barchlinux\b)"},
    {"manjaro", R"(manjaro)"},
    {"endeavouros", R"(endeavour)"},
    {"cachyos", R"(cachy)"},
    {"bazzite", R"(bazzite)"},
    {"kubuntu", R"(kubuntu)"},
    {"popos", R"(pop[_!\s-]*os)"},
    {"linuxmint", R"(linux[\s_-]*mint|^mint\b|linuxmint)"},
    {"elementary", R"(elementary)"},
    {"zorin", R"(zorin)"},
    {"ubuntu", R"(ubuntu)"},
    {"kali", R"(\bkali)"},
    {"debian", R"(debian|^d-live)"},
    {"fedora", R"(fedora)"},
    {"opensuse", R"(opensuse|\bsuse\b|tumbleweed|leap)"},
    {"nixos", R"(nixos)"},
    {"rocky", R"(rocky)"},
    {"almalinux", R"(alma[\s_-]*linux)"},
    {"centos", R"(centos)"},
    {"rhel", R"(^rhel|red[\s_-]*hat)"},
    {"alpine", R"(alpine)"},
    {"void", R"(^void\b|void[\s_-]*linux)"},
    {"gentoo", R"(gentoo)"},
    {"android", R"(android)"},
    {"freebsd", R"(freebsd|^\d+_\d+_RELEASE)"},
    {"windows7", R"(^GRMC|^GSP1RM|^GRTMU|windows[\s_-]*7|\bwin7)"},
    {"windows", R"(^(CCCOMA|CCSA|CPBA|CENA|CPRA|CCOMA|J_CCSA|SSS_|ESD-ISO|CESA|CCRA)|windows|\bwin[\s_-]?1[01]\b|^win1[01]|microsoft)"},
    {"linux", R"(linux)"},
};

QString guessFromText(const QString &text)
{
    if (text.trimmed().isEmpty())
        return {};
    for (const Rule &r : kRules) {
        static QHash<const char *, QRegularExpression> cache;
        auto it = cache.find(r.pattern);
        if (it == cache.end())
            it = cache.insert(r.pattern, QRegularExpression(QString::fromLatin1(r.pattern),
                                                            QRegularExpression::CaseInsensitiveOption));
        if (it->match(text).hasMatch())
            return QString::fromLatin1(r.id);
    }
    return {};
}

IsoInfo readIsoInfo(const QString &path)
{
    IsoInfo info;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return info;
    // Volume descriptors start at sector 16 (2048-byte sectors).
    for (int sector = 16; sector < 32; ++sector) {
        if (!f.seek(qint64(sector) * 2048))
            break;
        const QByteArray d = f.read(2048);
        if (d.size() < 2048 || d.mid(1, 5) != "CD001")
            break;
        const quint8 type = quint8(d[0]);
        if (type == 255)
            break;
        if (type != 1)
            continue;
        auto field = [&](int off, int len) { return QString::fromLatin1(d.mid(off, len)).trimmed(); };
        info.valid = true;
        info.systemId = field(8, 32);
        info.volumeId = field(40, 32);
        info.publisher = field(318, 128);
        info.application = field(574, 128);
        break;
    }
    return info;
}

static QString prettyLabel(const QString &profileId, const IsoInfo &iso)
{
    const OsProfile &p = profile(profileId);
    const QString vol = iso.volumeId;
    if (profileId == QLatin1String("archlinux")) {
        static const QRegularExpression re(QStringLiteral(R"(ARCH_(\d{4})(\d{2}))"));
        const auto m = re.match(vol);
        if (m.hasMatch())
            return QStringLiteral("Arch Linux %1.%2").arg(m.captured(1), m.captured(2));
        return p.name;
    }
    if (profileId == QLatin1String("windows")) {
        static const QRegularExpression re(QStringLiteral(R"(_([A-Z]{2}-[A-Z]{2})_)"));
        const auto m = re.match(vol);
        return m.hasMatch() ? QStringLiteral("Windows 10 / 11 (%1)").arg(m.captured(1)) : p.name;
    }
    if (profileId == QLatin1String("windows7"))
        return p.name;
    // Most distros use a readable label ("Linux Mint 21.1 Cinnamon 64-bit").
    QString label = vol;
    label.replace(QLatin1Char('_'), QLatin1Char(' '));
    label = label.simplified();
    if (label.contains(QLatin1Char(' ')) && label.size() > 4 && label != label.toUpper())
        return label;
    return p.name;
}

Detection detect(const QString &isoPath)
{
    Detection det;
    const IsoInfo iso = readIsoInfo(isoPath);
    det.volumeId = iso.volumeId;

    QString id;
    if (iso.valid) {
        id = guessFromText(iso.volumeId);
        if (id.isEmpty())
            id = guessFromText(iso.publisher + QLatin1Char(' ') + iso.application);
    }

    // osinfo-detect knows about media that use cryptic labels; use it as a second opinion.
    const QString osinfo = QStandardPaths::findExecutable(QStringLiteral("osinfo-detect"));
    if (!osinfo.isEmpty()) {
        QProcess p;
        p.setProcessEnvironment(Paths::hostEnvironment());
        p.start(osinfo, {isoPath});
        if (p.waitForFinished(20000)) {
            const QString out = QString::fromUtf8(p.readAllStandardOutput());
            if (out.contains(QLatin1String("not bootable")))
                det.bootable = false;
            static const QRegularExpression re(QStringLiteral("for OS '([^']+)'"));
            const auto m = re.match(out);
            if (m.hasMatch()) {
                det.osinfoName = m.captured(1);
                det.osinfoName.remove(QRegularExpression(QStringLiteral(R"(\s*\((x86_64|amd64|i686|x86)\)\s*$)")));
            }
        } else {
            p.kill();
            p.waitForFinished(1000);
        }
    }

    if (id.isEmpty() && !det.osinfoName.isEmpty())
        id = guessFromText(det.osinfoName);
    if (id.isEmpty())
        id = guessFromText(QFileInfo(isoPath).completeBaseName());

    det.recognized = !id.isEmpty() && id != QLatin1String("linux");
    if (id.isEmpty())
        id = QStringLiteral("other");
    det.profileId = id;

    const bool windows = profile(id).family == OsFamily::Windows;
    const QString pretty = prettyLabel(id, iso);
    if (windows || det.osinfoName.isEmpty() || pretty != profile(id).name)
        det.label = pretty;   // ours carries version info (e.g. "Arch Linux 2024.07")
    else
        det.label = det.osinfoName;
    return det;
}

Recommendation recommend(const OsProfile &p, const HostInfo &h)
{
    Recommendation r;
    const int maxCpus = qMax(1, h.logicalCpus - (h.logicalCpus > 4 ? 2 : 1));
    int target = qMax(p.recCpus, h.logicalCpus / 2);
    target = qBound(qMin(p.minCpus, maxCpus), target, qMin(maxCpus, 16));
    if (h.threadsPerCore >= 2 && target >= 2) {
        target -= target % 2;
        r.threads = 2;
        r.cores = target / 2;
    } else {
        r.threads = 1;
        r.cores = target;
    }

    const qint64 host = h.totalRamMiB > 0 ? h.totalRamMiB : 8192;
    const qint64 quarter = qMax<qint64>(1024, qRound64(host / 4.0 / 1024.0) * 1024);
    qint64 mem = qMax<qint64>(p.recRamMiB, qMin<qint64>(quarter, 16384));
    mem = qMin<qint64>(mem, (host / 2) / 512 * 512);
    mem = qMax<qint64>(mem, qMin<qint64>(p.minRamMiB, host - 1024));
    r.memoryMiB = int(qMax<qint64>(512, mem));
    r.diskGiB = p.recDiskGiB;
    return r;
}

} // namespace OsDb
