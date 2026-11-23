// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "SharedFolder.h"
#include "Errors.h"
#include "HostInfo.h"
#include "Paths.h"
#include "QemuCommand.h"
#include "VmConfig.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

#include <cerrno>
#include <csignal>
#include <unistd.h>

namespace SharedFolder {

// Linux distributions give the first user account UID and GID 1000. Mapping
// the host user onto it lets that guest user work with the shared files.
static constexpr uint kGuestUserId = 1000;

// virtiofsd's own sandbox (user, mount and pid namespaces) keeps a guest from
// reaching files outside the shared folder. Some systems don't let unprivileged
// programs set one up (e.g. AppArmor's user namespace restrictions); once that
// happened, later starts in this session skip the attempt.
static bool s_noSandbox = false;

QString tag()
{
    return QStringLiteral("shared");
}

const Support &support()
{
    static const Support s = [] {
        Support r;
        const QString exe = HostInfo::get().virtiofsd;
        if (exe.isEmpty())
            return r;
        r.installed = true;
        QProcess p;
        p.setProcessEnvironment(Paths::hostEnvironment());
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(exe, {QStringLiteral("--help")});
        if (p.waitForFinished(5000)) {
            const QByteArray help = p.readAll();
            r.readOnly = help.contains("--readonly");
            r.translateIds = help.contains("--translate-uid");
        } else if (p.state() != QProcess::NotRunning) {
            p.kill();
            p.waitForFinished(1000);
        }
        return r;
    }();
    return s;
}

QString problem(const VmConfig &cfg)
{
    if (cfg.sharedFolder.isEmpty())
        return {};
    if (!support().installed)
        return QStringLiteral("virtiofsd isn't installed, so the VM starts without its shared folder.");
    const QString folder = cfg.absolutePath(cfg.sharedFolder);
    if (!QFileInfo(folder).isDir())
        return QStringLiteral("The shared folder “%1” wasn't found, so the VM starts without it.").arg(folder);
    if (cfg.sharedFolderReadOnly && !support().readOnly)
        return QStringLiteral("The installed virtiofsd can't share folders read-only (newer versions can), so the VM "
                              "starts without its shared folder.");
    return {};
}

static QString canonical(const QString &path)
{
    const QFileInfo fi(path);
    const QString c = fi.canonicalFilePath();
    return c.isEmpty() ? QDir::cleanPath(fi.absoluteFilePath()) : c;
}

bool exposesHome(const QString &folder)
{
    if (folder.trimmed().isEmpty())
        return false;
    const QString f = canonical(folder);
    const QString home = canonical(QDir::homePath());
    return home == f || home.startsWith(f.endsWith(QLatin1Char('/')) ? f : f + QLatin1Char('/'));
}

QStringList arguments(const VmConfig &cfg, const QString &runtimeDir, bool sandbox)
{
    QStringList a{QStringLiteral("--shared-dir"), cfg.absolutePath(cfg.sharedFolder),
                  QStringLiteral("--socket-path"), QemuRuntime::virtiofsSocket(runtimeDir),
                  QStringLiteral("--sandbox"), sandbox ? QStringLiteral("namespace") : QStringLiteral("none")};
    if (cfg.sharedFolderReadOnly)
        a << QStringLiteral("--readonly");
    // virtiofsd runs as the user, so everything the guest creates belongs to
    // the user on the host anyway. This only changes the owner the guest sees.
    if (support().translateIds) {
        const uint uid = ::getuid();
        const uint gid = ::getgid();
        if (uid != kGuestUserId)
            a << QStringLiteral("--translate-uid") << QStringLiteral("map:%1:%2:1").arg(kGuestUserId).arg(uid);
        if (gid != kGuestUserId)
            a << QStringLiteral("--translate-gid") << QStringLiteral("map:%1:%2:1").arg(kGuestUserId).arg(gid);
    }
    return a;
}

// The process exists and has not exited (a zombie waiting to be reaped has).
static bool running(qint64 pid)
{
    if (pid <= 0 || (::kill(pid_t(pid), 0) != 0 && errno != EPERM))
        return false;
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly))
        return false;
    const QByteArray s = stat.readAll();
    const qsizetype paren = s.lastIndexOf(')');
    return paren < 0 || s.mid(paren + 2, 1) != "Z";
}

static QString outputSince(const QString &log, qint64 offset)
{
    QFile f(log);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    f.seek(offset);
    return QString::fromLocal8Bit(f.readAll());
}

// Runs virtiofsd once and waits until it serves the socket. On failure,
// *failure gets what it printed.
static bool launch(const QStringList &args, const QString &runtimeDir, const QString &log, QString *failure)
{
    const QString program = HostInfo::get().virtiofsd;
    const QString sock = QemuRuntime::virtiofsSocket(runtimeDir);
    const QString pidFile = QemuRuntime::virtiofsdPidFile(runtimeDir);
    QFile::remove(sock);
    {
        QFile f(log);
        if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
            QemuCommand shown;
            shown.program = program;
            shown.args = args;
            f.write(QStringLiteral("=== %1\n%2\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                                                     shown.toShellString()).toUtf8());
        }
    }
    const qint64 offset = QFileInfo(log).size();

    QProcess p;
    p.setProcessEnvironment(Paths::hostEnvironment());
    p.setProgram(program);
    p.setArguments(args);
    p.setStandardInputFile(QProcess::nullDevice());
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setStandardOutputFile(log, QIODevice::Append);
    p.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession
                               | QProcess::UnixProcessFlag::ResetSignalHandlers
                               | QProcess::UnixProcessFlag::CloseFileDescriptors);
    qint64 pid = 0;
    if (!p.startDetached(&pid)) {
        *failure = QStringLiteral("%1 couldn't be started: %2.").arg(program, p.errorString());
        return false;
    }
    {
        QFile f(pidFile);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QByteArray::number(pid) + '\n');
    }

    QElapsedTimer timer;
    timer.start();
    qint64 socketSeen = -1;
    for (;;) {
        const bool up = running(pid);
        if (up && QFileInfo::exists(sock)) {
            if (socketSeen < 0)
                socketSeen = timer.elapsed();
            // Printed once the sandbox is set up and it waits for QEMU.
            if (outputSince(log, offset).contains(QLatin1String("Waiting for vhost-user socket connection")))
                return true;
            // A sandbox failure ends it right after the socket appears; if the
            // message ever changes, still running a moment later means serving.
            if (timer.elapsed() - socketSeen > 1500)
                return true;
        }
        if (!up) {
            QFile::remove(pidFile);
            const QString out = Errors::lastLines(outputSince(log, offset), 6);
            *failure = out.isEmpty() ? QStringLiteral("virtiofsd exited without printing an error.") : out;
            return false;
        }
        if (timer.elapsed() > 10000) {
            ::kill(pid_t(pid), SIGTERM);
            QFile::remove(pidFile);
            const QString out = Errors::lastLines(outputSince(log, offset), 6);
            *failure = QStringLiteral("virtiofsd didn't get ready within 10 seconds.");
            if (!out.isEmpty())
                *failure += QLatin1Char('\n') + out;
            return false;
        }
        ::usleep(10000);
    }
}

bool start(const VmConfig &cfg, const QString &runtimeDir, const QString &logFile, QStringList *notes,
           QString *error)
{
    const QString folder = cfg.absolutePath(cfg.sharedFolder);
    QString dirError;
    if (!Paths::ensureDir(QFileInfo(logFile).absolutePath(), false, &dirError)) {
        *error = QStringLiteral("The shared folder couldn't be set up: %1").arg(dirError);
        return false;
    }
    QFile::remove(logFile);

    if (support().installed && !support().translateIds && ::getuid() != kGuestUserId) {
        notes->append(QStringLiteral("In the VM, files in the shared folder belong to user ID %1. The installed "
                                     "virtiofsd can't show them as the VM's first user (ID %2); newer versions can.")
                          .arg(::getuid()).arg(kGuestUserId));
    }

    QString failure;
    if (!s_noSandbox) {
        if (launch(arguments(cfg, runtimeDir, true), runtimeDir, logFile, &failure))
            return true;
        const QString sandboxFailure = Errors::lastLines(failure, 1);
        if (launch(arguments(cfg, runtimeDir, false), runtimeDir, logFile, &failure)) {
            s_noSandbox = true;
            notes->append(QStringLiteral("virtiofsd couldn't set up its sandbox on this system (%1), so it runs "
                                         "without one. It is less protected against a guest that tries to reach "
                                         "files outside the shared folder.").arg(sandboxFailure));
            return true;
        }
    } else if (launch(arguments(cfg, runtimeDir, false), runtimeDir, logFile, &failure)) {
        notes->append(QStringLiteral("virtiofsd runs without its sandbox, which it couldn't set up on this system."));
        return true;
    }
    *error = QStringLiteral("The shared folder “%1” couldn't be set up. virtiofsd said:\n%2\n\n"
                            "To start the VM without it, turn the shared folder off in Settings → Shared Folder.")
                 .arg(folder, failure);
    return false;
}

void stop(const QString &runtimeDir)
{
    QFile f(QemuRuntime::virtiofsdPidFile(runtimeDir));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const qint64 pid = f.readAll().trimmed().toLongLong();
    if (!running(pid))
        return;
    // Guard against pid reuse: it must still be virtiofsd.
    QFile cmd(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (cmd.open(QIODevice::ReadOnly) && cmd.readAll().contains("virtiofsd"))
        ::kill(pid_t(pid), SIGTERM);
}

} // namespace SharedFolder
