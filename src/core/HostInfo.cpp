// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "HostInfo.h"
#include "Paths.h"
#include "AppSettings.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

#include <fcntl.h>
#include <unistd.h>

static qint64 readMeminfo(const char *key)
{
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    // procfs files report size 0, so QFile::atEnd() is useless here: read everything.
    const QByteArray k(key);
    for (const QByteArray &line : f.readAll().split('\n')) {
        if (line.startsWith(k)) {
            const QList<QByteArray> parts = line.simplified().split(' ');
            if (parts.size() >= 2)
                return parts[1].toLongLong() / 1024;
        }
    }
    return 0;
}

static QString firstExisting(const QStringList &candidates)
{
    for (const QString &c : candidates)
        if (QFileInfo::exists(c))
            return c;
    return {};
}

static QString findExecutable(const QString &name, const QStringList &extraPaths = {})
{
    QString p = QStandardPaths::findExecutable(name);
    if (p.isEmpty())
        p = QStandardPaths::findExecutable(name, extraPaths);
    return p;
}

static HostInfo detect()
{
    HostInfo h;

    // CPU topology from /proc/cpuinfo
    QFile f(QStringLiteral("/proc/cpuinfo"));
    if (f.open(QIODevice::ReadOnly)) {
        QSet<QString> cores;
        int logical = 0;
        QString physId, coreId;
        const QList<QByteArray> lines = f.readAll().split('\n');
        for (const QByteArray &raw : lines) {
            const int colon = raw.indexOf(':');
            const QString key = QString::fromLatin1(raw.left(colon)).trimmed();
            const QString val = colon >= 0 ? QString::fromUtf8(raw.mid(colon + 1)).trimmed() : QString();
            if (key == QLatin1String("processor")) {
                ++logical;
            } else if (key == QLatin1String("physical id")) {
                physId = val;
            } else if (key == QLatin1String("core id")) {
                coreId = val;
                cores.insert(physId + QLatin1Char(':') + coreId);
            } else if (key == QLatin1String("vendor_id")) {
                h.amd = val.contains(QLatin1String("AMD"));
                h.intel = val.contains(QLatin1String("Intel"));
            } else if (key == QLatin1String("model name") && h.cpuModel.isEmpty()) {
                h.cpuModel = val;
            }
        }
        h.logicalCpus = qMax(1, logical);
        h.physicalCores = cores.isEmpty() ? h.logicalCpus : int(cores.size());
        h.threadsPerCore = qMax(1, h.logicalCpus / qMax(1, h.physicalCores));
    } else {
        h.logicalCpus = qMax(1, int(sysconf(_SC_NPROCESSORS_ONLN)));
        h.physicalCores = h.logicalCpus;
    }

    h.totalRamMiB = readMeminfo("MemTotal:");

    const QStringList libexec = {QStringLiteral("/usr/lib"), QStringLiteral("/usr/libexec"),
                                 QStringLiteral("/usr/lib/qemu")};
    h.qemuImg = findExecutable(QStringLiteral("qemu-img"));
    h.swtpm = findExecutable(QStringLiteral("swtpm"));
    h.virtiofsd = findExecutable(QStringLiteral("virtiofsd"), libexec);

    // OVMF locations across distributions (Arch, Fedora, Debian/Ubuntu, openSUSE).
    const QStringList dirs = {
        QStringLiteral("/usr/share/edk2/x64"),
        QStringLiteral("/usr/share/edk2-ovmf/x64"),
        QStringLiteral("/usr/share/edk2/ovmf"),
        QStringLiteral("/usr/share/OVMF"),
        QStringLiteral("/usr/share/ovmf"),
        QStringLiteral("/usr/share/qemu"),
    };
    QStringList code, secure, vars;
    for (const QString &d : dirs) {
        code << d + QStringLiteral("/OVMF_CODE.4m.fd") << d + QStringLiteral("/OVMF_CODE_4M.fd")
             << d + QStringLiteral("/OVMF_CODE.fd") << d + QStringLiteral("/ovmf-x86_64-code.bin");
        secure << d + QStringLiteral("/OVMF_CODE.secboot.4m.fd") << d + QStringLiteral("/OVMF_CODE_4M.secboot.fd")
               << d + QStringLiteral("/OVMF_CODE.secboot.fd") << d + QStringLiteral("/ovmf-x86_64-smm-code.bin");
        vars << d + QStringLiteral("/OVMF_VARS.4m.fd") << d + QStringLiteral("/OVMF_VARS_4M.fd")
             << d + QStringLiteral("/OVMF_VARS.fd") << d + QStringLiteral("/ovmf-x86_64-vars.bin");
    }
    h.ovmfCode = firstExisting(code);
    h.ovmfCodeSecure = firstExisting(secure);
    h.ovmfVars = firstExisting(vars);

    const int fd = ::open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (fd >= 0) {
        h.kvmUsable = true;
        ::close(fd);
    } else if (!QFileInfo::exists(QStringLiteral("/dev/kvm"))) {
        h.kvmProblem = QStringLiteral("/dev/kvm does not exist. Enable virtualization (SVM/VT-x) in the BIOS and load the kvm module.");
    } else {
        h.kvmProblem = QStringLiteral("/dev/kvm is not accessible. Add your user to the 'kvm' group and log in again.");
    }
    return h;
}

const HostInfo &HostInfo::get()
{
    static const HostInfo info = detect();
    return info;
}

QString HostInfo::qemuBinary()
{
    const QString custom = AppSettings::instance().qemuBinary();
    if (!custom.isEmpty())
        return custom;
    const QString p = QStandardPaths::findExecutable(QStringLiteral("qemu-system-x86_64"));
    return p.isEmpty() ? QStringLiteral("qemu-system-x86_64") : p;
}

QVersionNumber HostInfo::qemuVersion()
{
    static QHash<QString, QVersionNumber> cache;
    const QString bin = qemuBinary();
    auto it = cache.constFind(bin);
    if (it != cache.cend())
        return *it;
    QProcess p;
    p.setProcessEnvironment(Paths::hostEnvironment());
    p.start(bin, {QStringLiteral("--version")});
    QVersionNumber v;
    if (p.waitForFinished(5000)) {
        static const QRegularExpression re(QStringLiteral(R"(version (\d+\.\d+(\.\d+)?))"));
        const auto m = re.match(QString::fromLocal8Bit(p.readAllStandardOutput()));
        if (m.hasMatch())
            v = QVersionNumber::fromString(m.captured(1));
    }
    cache.insert(bin, v);
    return v;
}

qint64 HostInfo::availableRamMiB()
{
    return readMeminfo("MemAvailable:");
}
