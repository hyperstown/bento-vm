#include "VmConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

QString VmConfig::absolutePath(const QString &p) const
{
    if (p.isEmpty() || QDir::isAbsolutePath(p))
        return p;
    return QDir::cleanPath(dir + QLatin1Char('/') + p);
}

QString VmConfig::diskPath(int index) const
{
    if (index < 0 || index >= disks.size())
        return {};
    return absolutePath(disks[index].path);
}

QJsonObject VmConfig::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("version")] = kVersion;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("os")] = QJsonObject{{QStringLiteral("id"), osId}, {QStringLiteral("label"), osLabel}};
    o[QStringLiteral("notes")] = notes;
    o[QStringLiteral("cpu")] = QJsonObject{{QStringLiteral("cores"), cores}, {QStringLiteral("threads"), threads}};
    o[QStringLiteral("memoryMiB")] = memoryMiB;
    o[QStringLiteral("firmware")] = QJsonObject{{QStringLiteral("type"), firmware},
                                               {QStringLiteral("secureBoot"), secureBoot},
                                               {QStringLiteral("tpm"), tpm}};
    o[QStringLiteral("graphics")] = QJsonObject{{QStringLiteral("mode"), graphics},
                                               {QStringLiteral("hostmemMiB"), hostmemMiB},
                                               {QStringLiteral("display"), display},
                                               {QStringLiteral("autoResize"), autoResize}};
    QJsonArray d;
    for (const DiskSpec &disk : disks)
        d.append(QJsonObject{{QStringLiteral("path"), disk.path},
                             {QStringLiteral("format"), disk.format},
                             {QStringLiteral("bus"), disk.bus}});
    o[QStringLiteral("disks")] = d;
    o[QStringLiteral("cdroms")] = QJsonArray::fromStringList(cdroms);

    QJsonArray fw;
    for (const PortForward &p : portForwards)
        fw.append(QJsonObject{{QStringLiteral("proto"), p.proto},
                              {QStringLiteral("hostAddress"), p.hostAddress},
                              {QStringLiteral("hostPort"), p.hostPort},
                              {QStringLiteral("guestPort"), p.guestPort}});
    o[QStringLiteral("network")] = QJsonObject{{QStringLiteral("enabled"), network},
                                              {QStringLiteral("nic"), nicModel},
                                              {QStringLiteral("portForwards"), fw}};
    o[QStringLiteral("integration")] = QJsonObject{{QStringLiteral("audio"), audio},
                                                  {QStringLiteral("clipboard"), clipboard},
                                                  {QStringLiteral("fileTransfer"), fileTransfer}};
    o[QStringLiteral("advanced")] = QJsonObject{{QStringLiteral("localtime"), localtime},
                                               {QStringLiteral("hyperv"), hyperv},
                                               {QStringLiteral("extraArgs"), extraArgs}};
    o[QStringLiteral("created")] = created.toString(Qt::ISODate);
    return o;
}

VmConfig VmConfig::fromJson(const QJsonObject &o)
{
    VmConfig c;
    c.id = o[QStringLiteral("id")].toString();
    c.name = o[QStringLiteral("name")].toString();
    const QJsonObject os = o[QStringLiteral("os")].toObject();
    c.osId = os[QStringLiteral("id")].toString(c.osId);
    c.osLabel = os[QStringLiteral("label")].toString();
    c.notes = o[QStringLiteral("notes")].toString();

    const QJsonObject cpu = o[QStringLiteral("cpu")].toObject();
    c.cores = qMax(1, cpu[QStringLiteral("cores")].toInt(c.cores));
    c.threads = qBound(1, cpu[QStringLiteral("threads")].toInt(c.threads), 2);
    c.memoryMiB = qMax(256, o[QStringLiteral("memoryMiB")].toInt(c.memoryMiB));

    const QJsonObject fw = o[QStringLiteral("firmware")].toObject();
    c.firmware = fw[QStringLiteral("type")].toString(c.firmware);
    c.secureBoot = fw[QStringLiteral("secureBoot")].toBool(c.secureBoot);
    c.tpm = fw[QStringLiteral("tpm")].toBool(c.tpm);

    const QJsonObject gfx = o[QStringLiteral("graphics")].toObject();
    c.graphics = gfx[QStringLiteral("mode")].toString(c.graphics);
    c.hostmemMiB = gfx[QStringLiteral("hostmemMiB")].toInt(c.hostmemMiB);
    c.display = gfx[QStringLiteral("display")].toString(c.display);
    c.autoResize = gfx[QStringLiteral("autoResize")].toBool(c.autoResize);

    for (const QJsonValue &v : o[QStringLiteral("disks")].toArray()) {
        const QJsonObject d = v.toObject();
        DiskSpec s;
        s.path = d[QStringLiteral("path")].toString();
        s.format = d[QStringLiteral("format")].toString(s.format);
        s.bus = d[QStringLiteral("bus")].toString(s.bus);
        if (!s.path.isEmpty())
            c.disks << s;
    }
    for (const QJsonValue &v : o[QStringLiteral("cdroms")].toArray())
        c.cdroms << v.toString();

    const QJsonObject net = o[QStringLiteral("network")].toObject();
    c.network = net[QStringLiteral("enabled")].toBool(c.network);
    c.nicModel = net[QStringLiteral("nic")].toString(c.nicModel);
    for (const QJsonValue &v : net[QStringLiteral("portForwards")].toArray()) {
        const QJsonObject p = v.toObject();
        PortForward f;
        f.proto = p[QStringLiteral("proto")].toString(f.proto);
        f.hostAddress = p[QStringLiteral("hostAddress")].toString(f.hostAddress);
        f.hostPort = p[QStringLiteral("hostPort")].toInt();
        f.guestPort = p[QStringLiteral("guestPort")].toInt();
        if (f.hostPort > 0 && f.guestPort > 0)
            c.portForwards << f;
    }

    const QJsonObject integ = o[QStringLiteral("integration")].toObject();
    c.audio = integ[QStringLiteral("audio")].toBool(c.audio);
    c.clipboard = integ[QStringLiteral("clipboard")].toString(c.clipboard);
    c.fileTransfer = integ[QStringLiteral("fileTransfer")].toBool(c.fileTransfer);

    const QJsonObject adv = o[QStringLiteral("advanced")].toObject();
    c.localtime = adv[QStringLiteral("localtime")].toBool(c.localtime);
    c.hyperv = adv[QStringLiteral("hyperv")].toBool(c.hyperv);
    c.extraArgs = adv[QStringLiteral("extraArgs")].toString();

    c.created = QDateTime::fromString(o[QStringLiteral("created")].toString(), Qt::ISODate);
    return c;
}

bool VmConfig::save(QString *error) const
{
    QSaveFile f(configFile());
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

bool VmConfig::load(const QString &dirPath, VmConfig *out, QString *error)
{
    QFile f(dirPath + QLatin1Char('/') + fileName());
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject()) {
        if (error)
            *error = pe.errorString();
        return false;
    }
    *out = fromJson(doc.object());
    out->dir = QDir::cleanPath(dirPath);
    if (out->id.isEmpty()) {
        if (error)
            *error = QStringLiteral("vm.json has no id");
        return false;
    }
    return true;
}

QString VmConfig::firstMissingRequiredPath() const
{
    for (int i = 0; i < disks.size(); ++i) {
        const QString p = diskPath(i);
        if (!QFileInfo::exists(p))
            return p;
    }
    return {};
}

QString clipboardModeLabel(const QString &mode)
{
    if (mode == QLatin1String("host-to-guest"))
        return QStringLiteral("Host → Guest only");
    if (mode == QLatin1String("guest-to-host"))
        return QStringLiteral("Guest → Host only");
    if (mode == QLatin1String("off"))
        return QStringLiteral("Disabled");
    return QStringLiteral("Both directions");
}

QString graphicsModeLabel(const QString &mode)
{
    if (mode == QLatin1String("virgl"))
        return QStringLiteral("VirGL (OpenGL)");
    if (mode == QLatin1String("2d"))
        return QStringLiteral("Standard (2D)");
    return QStringLiteral("Venus (Vulkan + OpenGL)");
}
