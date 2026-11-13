// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Vm.h"
#include "HostInfo.h"
#include "Paths.h"
#include "QemuCommand.h"
#include "QmpClient.h"
#include "UsbDevices.h"
#include "agent/VdAgent.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QTimer>

#include <cerrno>
#include <csignal>
#include <unistd.h>

Vm::Vm(const VmConfig &cfg, QObject *parent)
    : QObject(parent)
    , m_cfg(cfg)
    , m_qmp(new QmpClient(this))
    , m_agent(new VdAgent(this))
    , m_startTimer(new QTimer(this))
{
    m_startTimer->setInterval(100);
    connect(m_startTimer, &QTimer::timeout, this, &Vm::pollStartup);
    connect(m_qmp, &QmpClient::ready, this, &Vm::onQmpReady);
    connect(m_qmp, &QmpClient::eventReceived, this, &Vm::onQmpEvent);
    connect(m_qmp, &QmpClient::disconnected, this, &Vm::onQmpDisconnected);

    m_agent->setClipboardMode(m_cfg.clipboard);
    m_agent->setFileTransferEnabled(m_cfg.fileTransfer);

    const QString thumb = Paths::thumbnailFile(m_cfg.id);
    if (QFileInfo::exists(thumb))
        m_thumbnail.load(thumb);
}

Vm::~Vm() = default;

QString Vm::stateLabel(State s)
{
    switch (s) {
    case State::Stopped: return QStringLiteral("Powered off");
    case State::Starting: return QStringLiteral("Starting…");
    case State::Running: return QStringLiteral("Running");
    case State::Paused: return QStringLiteral("Paused");
    case State::Stopping: return QStringLiteral("Shutting down…");
    }
    return {};
}

bool Vm::updateConfig(const VmConfig &cfg, QString *error)
{
    VmConfig c = cfg;
    c.dir = m_cfg.dir;
    c.id = m_cfg.id;
    if (!c.save(error))
        return false;
    m_cfg = c;
    m_agent->setClipboardMode(m_cfg.clipboard);
    m_agent->setFileTransferEnabled(m_cfg.fileTransfer);
    Q_EMIT configChanged();
    return true;
}

void Vm::reloadConfig(const VmConfig &cfg)
{
    if (isActive())
        return;   // never swap the config under a running VM
    const QString dir = m_cfg.dir;
    m_cfg = cfg;
    if (m_cfg.dir.isEmpty())
        m_cfg.dir = dir;
    m_agent->setClipboardMode(m_cfg.clipboard);
    m_agent->setFileTransferEnabled(m_cfg.fileTransfer);
    Q_EMIT configChanged();
}

void Vm::setAvailability(bool available, const QString &reason, const QString &shortReason)
{
    if (m_available == available && m_unavailableReason == reason)
        return;
    m_available = available;
    m_unavailableReason = available ? QString() : reason;
    m_unavailableShort = available ? QString() : (shortReason.isEmpty() ? reason : shortReason);
    Q_EMIT availabilityChanged();
}

QString Vm::runtimeDir() const { return Paths::vmRuntimeDir(m_cfg.id); }
QString Vm::logFile() const { return Paths::logFile(m_cfg.id); }

void Vm::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    Q_EMIT stateChanged(s);
}

void Vm::setThumbnail(const QImage &img)
{
    if (img.isNull())
        return;
    m_thumbnail = img.scaled(480, 270, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    Paths::ensureDir(QFileInfo(Paths::thumbnailFile(m_cfg.id)).absolutePath());
    m_thumbnail.save(Paths::thumbnailFile(m_cfg.id), "PNG");
    Q_EMIT thumbnailChanged();
}

bool Vm::pidAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
    if (::kill(pid_t(pid), 0) != 0 && errno != EPERM)
        return false;
    // Guard against pid reuse: it must still be a QEMU process.
    QFile cmd(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!cmd.open(QIODevice::ReadOnly))
        return false;
    return cmd.readAll().contains("qemu");
}

qint64 Vm::readPid() const
{
    QFile f(QemuRuntime::pidFile(runtimeDir()));
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    return f.readAll().trimmed().toLongLong();
}

bool Vm::startSwtpm(QString *error)
{
    const HostInfo &host = HostInfo::get();
    const QString rt = runtimeDir();
    const QString state = QemuRuntime::tpmStateDir(m_cfg.dir);
    Paths::ensureDir(state, true);
    QProcess p;
    p.setProcessEnvironment(Paths::hostEnvironment());
    p.setProgram(host.swtpm);
    p.setArguments({QStringLiteral("socket"), QStringLiteral("--tpm2"),
                    QStringLiteral("--tpmstate"), QStringLiteral("dir=%1").arg(state),
                    QStringLiteral("--ctrl"), QStringLiteral("type=unixio,path=%1").arg(QemuRuntime::swtpmSocket(rt)),
                    QStringLiteral("--log"), QStringLiteral("file=%1/swtpm.log").arg(rt),
                    QStringLiteral("--terminate"), QStringLiteral("--daemon")});
    p.start();
    if (!p.waitForFinished(10000) || p.exitCode() != 0) {
        if (error)
            *error = QStringLiteral("Failed to start swtpm: %1").arg(QString::fromLocal8Bit(p.readAllStandardError()));
        return false;
    }
    for (int i = 0; i < 50 && !QFileInfo::exists(QemuRuntime::swtpmSocket(rt)); ++i)
        ::usleep(20000);
    return true;
}

bool Vm::start(QString *error)
{
    auto fail = [&](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (isActive())
        return fail(QStringLiteral("The VM is already running."));
    if (!m_available)
        return fail(m_unavailableReason);
    const QString missing = m_cfg.firstMissingRequiredPath();
    if (!missing.isEmpty())
        return fail(QStringLiteral("Disk image not found:\n%1").arg(missing));

    const QString rt = runtimeDir();
    QDir(rt).removeRecursively();
    if (!Paths::ensureDir(rt, true))
        return fail(QStringLiteral("Can't create runtime directory %1").arg(rt));

    const HostInfo &host = HostInfo::get();
    if (m_cfg.firmware == QLatin1String("uefi")) {
        const QString vars = QemuRuntime::efiVarsFile(m_cfg.dir);
        if (!QFileInfo::exists(vars)) {
            if (host.ovmfVars.isEmpty() || !QFile::copy(host.ovmfVars, vars))
                return fail(QStringLiteral("Can't create the UEFI variable store (is edk2-ovmf installed?)."));
            QFile::setPermissions(vars, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
    }

    QemuCommand cmd = buildQemuCommand(m_cfg, rt);
    if (!cmd.error.isEmpty())
        return fail(cmd.error);
    m_warnings = cmd.warnings;
    if (!m_cfg.usbDevices.isEmpty()) {
        for (const UsbDevice &d : Usb::enumerate()) {
            for (const UsbAutoConnect &u : m_cfg.usbDevices) {
                if (u.vendorId == d.vendorId && u.productId == d.productId && !d.accessible())
                    m_warnings << QStringLiteral("No permission to use %1; allow it in Settings → USB.").arg(d.displayName());
            }
        }
    }

    if (m_cfg.tpm && !host.swtpm.isEmpty()) {
        QString err;
        if (!startSwtpm(&err))
            return fail(err);
    }

    // Log header with the exact command, then let QEMU append its output.
    const QString log = logFile();
    Paths::ensureDir(QFileInfo(log).absolutePath());
    {
        QFile f(log);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(QStringLiteral("=== %1 — starting %2\n%3\n\n")
                        .arg(QDateTime::currentDateTime().toString(Qt::ISODate), m_cfg.name, cmd.toShellString())
                        .toUtf8());
            for (const QString &w : std::as_const(cmd.warnings))
                f.write(QStringLiteral("warning: %1\n").arg(w).toUtf8());
            m_logOffset = f.pos();
        }
    }

    QProcess p;

    p.setProcessEnvironment(Paths::hostEnvironment());
    p.setProgram(cmd.program);
    p.setArguments(cmd.args);
    p.setWorkingDirectory(m_cfg.dir);
    p.setStandardInputFile(QProcess::nullDevice());
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setStandardOutputFile(log, QIODevice::Append);
    p.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession
                               | QProcess::UnixProcessFlag::ResetSignalHandlers
                               | QProcess::UnixProcessFlag::CloseFileDescriptors);
    qint64 pid = 0;
    if (!p.startDetached(&pid))
        return fail(QStringLiteral("Failed to launch %1: %2").arg(cmd.program, p.errorString()));

    m_pid = pid;
    m_startedAt = QDateTime::currentDateTime();
    m_shutdownRequested = false;
    m_guestShutdown = false;
    setState(State::Starting);
    m_startTimer->start();
    return true;
}

void Vm::pollStartup()
{
    const QString sock = QemuRuntime::qmpSocket(runtimeDir());
    if (!pidAlive(m_pid)) {
        m_startTimer->stop();
        cleanupRuntime();
        setState(State::Stopped);
        Q_EMIT errorOccurred(QStringLiteral("%1 failed to start").arg(m_cfg.name), logTail());
        return;
    }
    if (QFileInfo::exists(sock) && !m_qmp->isConnected()) {
        if (m_qmp->connectTo(sock)) {
            m_startTimer->stop();
            return;
        }
    }
    if (m_startedAt.msecsTo(QDateTime::currentDateTime()) > 60000) {
        m_startTimer->stop();
        Q_EMIT errorOccurred(QStringLiteral("%1 did not respond").arg(m_cfg.name),
                             QStringLiteral("QEMU is running but its control socket never appeared.\n\n") + logTail());
    }
}

bool Vm::tryAttach()
{
    if (isActive())
        return true;
    const QString sock = QemuRuntime::qmpSocket(runtimeDir());
    const qint64 pid = readPid();
    if (!QFileInfo::exists(sock) || !pidAlive(pid))
        return false;
    if (!m_qmp->connectTo(sock))
        return false;
    m_pid = pid;
    m_startedAt = QDateTime::currentDateTime().addSecs(-3600);
    setState(State::Starting);
    return true;
}

void Vm::onQmpReady()
{
    m_qmp->execute(QStringLiteral("query-status"), {}, [this](const QJsonValue &r, const QString &err) {
        if (!err.isEmpty())
            return;
        const QString status = r.toObject()[QStringLiteral("status")].toString();
        setState(status == QLatin1String("paused") ? State::Paused : State::Running);
    });
    if (m_cfg.display == QLatin1String("embedded"))
        m_agent->connectTo(QemuRuntime::agentSocket(runtimeDir()));
    refreshUsb();
    Q_EMIT qmpReady();
}

void Vm::onQmpEvent(const QString &event, const QJsonObject &data)
{
    if (event == QLatin1String("STOP")) {
        if (m_state == State::Running)
            setState(State::Paused);
    } else if (event == QLatin1String("RESUME")) {
        setState(State::Running);
    } else if (event == QLatin1String("SHUTDOWN")) {
        m_guestShutdown = true;
        setState(State::Stopping);
    } else if (event == QLatin1String("POWERDOWN")) {
        m_shutdownRequested = true;
    } else if (event == QLatin1String("DEVICE_DELETED")) {
        refreshUsb();
    }
    Q_UNUSED(data);
}

void Vm::onQmpDisconnected()
{
    m_agent->disconnectAgent();
    const bool expected = m_guestShutdown || m_shutdownRequested || m_state == State::Stopping;
    const qint64 uptime = m_startedAt.isValid() ? m_startedAt.secsTo(QDateTime::currentDateTime()) : 0;
    // Give QEMU a moment to really exit, then tidy up.
    QTimer::singleShot(300, this, [this, expected, uptime] {
        if (pidAlive(m_pid)) {
            // QMP dropped but the process lives on; try to reconnect once.
            if (m_qmp->connectTo(QemuRuntime::qmpSocket(runtimeDir())))
                return;
        }
        cleanupRuntime();
        setState(State::Stopped);
        if (!expected && uptime < 15) {
            Q_EMIT errorOccurred(QStringLiteral("%1 stopped unexpectedly").arg(m_cfg.name), logTail());
        }
    });
}

void Vm::cleanupRuntime()
{
    QDir(runtimeDir()).removeRecursively();
    m_pid = 0;
    if (!m_usb.isEmpty()) {
        m_usb.clear();
        Q_EMIT usbChanged();
    }
    releaseUnusedUsbAccess();
}

QString Vm::logTail(int maxLines) const
{
    QFile f(logFile());
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("(no log available)");
    if (m_logOffset > 0 && m_logOffset < f.size())
        f.seek(m_logOffset);
    QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() > maxLines)
        lines = lines.mid(lines.size() - maxLines);
    if (lines.isEmpty())
        return QStringLiteral("QEMU exited without printing an error. Full log: %1").arg(logFile());
    return lines.join(QLatin1Char('\n'));
}

void Vm::shutdown()
{
    if (!m_qmp->isReady())
        return;
    m_shutdownRequested = true;
    m_qmp->execute(QStringLiteral("system_powerdown"));
}

void Vm::forceStop()
{
    m_shutdownRequested = true;
    if (m_qmp->isReady()) {
        setState(State::Stopping);
        m_qmp->execute(QStringLiteral("quit"));
    } else if (m_pid > 0 && pidAlive(m_pid)) {
        ::kill(pid_t(m_pid), SIGTERM);
    }
    if (m_state == State::Starting) {
        m_startTimer->stop();
        QTimer::singleShot(500, this, [this] {
            if (!pidAlive(m_pid)) {
                cleanupRuntime();
                setState(State::Stopped);
            }
        });
    }
}

void Vm::reboot()
{
    if (m_qmp->isReady())
        m_qmp->execute(QStringLiteral("system_reset"));
}

void Vm::pause()
{
    if (m_qmp->isReady())
        m_qmp->execute(QStringLiteral("stop"));
}

void Vm::resume()
{
    if (m_qmp->isReady())
        m_qmp->execute(QStringLiteral("cont"));
}

void Vm::changeCdrom(int index, const QString &isoPath)
{
    VmConfig c = m_cfg;
    while (c.cdroms.size() <= index)
        c.cdroms << QString();
    c.cdroms[index] = isoPath;
    updateConfig(c);

    if (!m_qmp->isReady())
        return;
    const QString dev = QString::fromLatin1(QemuRuntime::cdromDeviceId(index));
    if (isoPath.isEmpty()) {
        m_qmp->execute(QStringLiteral("eject"), {{QStringLiteral("id"), dev}, {QStringLiteral("force"), true}},
                       [this](const QJsonValue &, const QString &err) {
                           if (!err.isEmpty())
                               Q_EMIT errorOccurred(QStringLiteral("Couldn't eject the disc"), err);
                       });
    } else {
        m_qmp->execute(QStringLiteral("blockdev-change-medium"),
                       {{QStringLiteral("id"), dev},
                        {QStringLiteral("filename"), isoPath},
                        {QStringLiteral("format"), QStringLiteral("raw")},
                        {QStringLiteral("read-only-mode"), QStringLiteral("read-only")}},
                       [this](const QJsonValue &, const QString &err) {
                           if (!err.isEmpty())
                               Q_EMIT errorOccurred(QStringLiteral("Couldn't insert the disc"), err);
                       });
    }
}

// ---------------------------------------------------------------------------
// USB passthrough

bool UsbAttachment::matches(const UsbDevice &d) const
{
    if (hostBus)
        return hostBus == d.bus && hostPort == d.port;
    return vendorId == d.vendorId && productId == d.productId;
}

void Vm::releaseUnusedUsbAccess()
{
    // Devices handed back to the host (or a stopped VM) lose their temporary access.
    for (auto it = m_usbAccess.begin(); it != m_usbAccess.end();) {
        bool attached = false;
        for (const UsbAttachment &a : std::as_const(m_usb))
            attached |= a.qdevId == it.key();
        if (attached) {
            ++it;
        } else {
            Usb::releaseTemporaryAccess(it.value());
            it = m_usbAccess.erase(it);
        }
    }
}

const UsbAttachment *Vm::usbAttachmentFor(const UsbDevice &d) const
{
    for (const UsbAttachment &a : m_usb)
        if (a.matches(d))
            return &a;
    return nullptr;
}

void Vm::attachUsb(const UsbDevice &d, std::function<void(const QString &)> done, QProcess *accessHolder)
{
    if (!m_qmp->isReady()) {
        Usb::releaseTemporaryAccess(accessHolder);
        if (done)
            done(QStringLiteral("The VM isn't running."));
        return;
    }
    // Bind to the physical port, so the device stays attached if it
    // re-enumerates (many controllers do when they switch modes).
    QString port = d.port;
    const QString id = QStringLiteral("usb-h%1-%2").arg(d.bus).arg(port.replace(QLatin1Char('.'), QLatin1Char('_')));
    m_qmp->execute(QStringLiteral("device_add"),
                   {{QStringLiteral("driver"), QStringLiteral("usb-host")},
                    {QStringLiteral("id"), id},
                    {QStringLiteral("bus"), QStringLiteral("xhci.0")},
                    {QStringLiteral("hostbus"), d.bus},
                    {QStringLiteral("hostport"), d.port}},
                   [this, done, id, accessHolder](const QJsonValue &, const QString &err) {
                       if (accessHolder) {
                           if (err.isEmpty()) {
                               Usb::releaseTemporaryAccess(m_usbAccess.take(id));
                               accessHolder->setParent(this);
                               m_usbAccess.insert(id, accessHolder);
                           } else {
                               Usb::releaseTemporaryAccess(accessHolder);
                           }
                       }
                       refreshUsb();
                       if (done)
                           done(err);
                   });
}

void Vm::detachUsb(const QString &qdevId, std::function<void(const QString &)> done)
{
    if (!m_qmp->isReady()) {
        if (done)
            done(QStringLiteral("The VM isn't running."));
        return;
    }
    m_qmp->execute(QStringLiteral("device_del"), {{QStringLiteral("id"), qdevId}},
                   [this, done](const QJsonValue &, const QString &err) {
                       refreshUsb();
                       if (done)
                           done(err);
                   });
}

void Vm::refreshUsb()
{
    if (!m_qmp->isReady())
        return;
    // QEMU is the source of truth: list usb-host devices and read their match properties.
    m_qmp->execute(QStringLiteral("qom-list"), {{QStringLiteral("path"), QStringLiteral("/machine/peripheral")}},
                   [this](const QJsonValue &r, const QString &err) {
        if (!err.isEmpty())
            return;
        QStringList ids;
        for (const QJsonValue &v : r.toArray()) {
            const QJsonObject o = v.toObject();
            if (o[QStringLiteral("type")].toString() == QLatin1String("child<usb-host>"))
                ids << o[QStringLiteral("name")].toString();
        }
        auto result = std::make_shared<QMap<QString, UsbAttachment>>();
        auto pending = std::make_shared<int>(int(ids.size()) * 4);
        if (ids.isEmpty()) {
            m_usb.clear();
            releaseUnusedUsbAccess();
            Q_EMIT usbChanged();
            return;
        }
        for (const QString &id : std::as_const(ids)) {
            (*result)[id].qdevId = id;
            for (const char *prop : {"hostbus", "hostport", "vendorid", "productid"}) {
                const QString p = QString::fromLatin1(prop);
                m_qmp->execute(QStringLiteral("qom-get"),
                               {{QStringLiteral("path"), QStringLiteral("/machine/peripheral/") + id},
                                {QStringLiteral("property"), p}},
                               [this, id, p, result, pending](const QJsonValue &v, const QString &) {
                                   UsbAttachment &a = (*result)[id];
                                   const auto hex = [](const QJsonValue &x) {
                                       return QStringLiteral("%1").arg(x.toInteger(), 4, 16, QLatin1Char('0'));
                                   };
                                   if (p == QLatin1String("hostbus"))
                                       a.hostBus = int(v.toInteger());
                                   else if (p == QLatin1String("hostport"))
                                       a.hostPort = v.toString();
                                   else if (p == QLatin1String("vendorid"))
                                       a.vendorId = hex(v);
                                   else
                                       a.productId = hex(v);
                                   if (--*pending == 0) {
                                       m_usb = result->values();
                                       releaseUnusedUsbAccess();
                                       Q_EMIT usbChanged();
                                   }
                               });
            }
        }
    });
}
