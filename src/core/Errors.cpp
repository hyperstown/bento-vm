// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Errors.h"

#include <QFileInfo>
#include <QProcess>
#include <QStringList>

#include <cerrno>
#include <cstring>

namespace Errors {

QString lastLines(const QString &text, int lines)
{
    QStringList l = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString &s : l)
        s = s.trimmed();
    l.removeAll(QString());
    if (l.size() > lines)
        l = l.mid(l.size() - lines);
    return l.join(QLatin1Char('\n'));
}

QString fromProcess(QProcess &process, const QString &program)
{
    QString output = lastLines(QString::fromLocal8Bit(process.readAllStandardError()), 12);
    if (output.isEmpty())
        output = lastLines(QString::fromLocal8Bit(process.readAllStandardOutput()), 12);
    // "qemu-img: /path: Could not …" reads better without the program name.
    const QString prefix = QFileInfo(process.program()).fileName() + QStringLiteral(": ");
    if (output.startsWith(prefix))
        output = output.mid(prefix.size());

    switch (process.error()) {
    case QProcess::FailedToStart:
        return QStringLiteral("%1 couldn't be started (%2). Make sure it's installed.").arg(program, process.errorString());
    case QProcess::Timedout:
        if (process.state() != QProcess::NotRunning)
            return QStringLiteral("%1 didn't finish in time.").arg(program)
                   + (output.isEmpty() ? QString() : QStringLiteral(" Its last output:\n") + output);
        break;
    default:
        break;
    }
    if (process.exitStatus() == QProcess::CrashExit)
        return QStringLiteral("%1 stopped unexpectedly.").arg(program)
               + (output.isEmpty() ? QString() : QLatin1Char('\n') + output);
    if (!output.isEmpty())
        return output;
    return QStringLiteral("%1 failed with exit code %2 without saying why.").arg(program).arg(process.exitCode());
}

QString fromErrno(int error)
{
    const QString s = QString::fromLocal8Bit(::strerror(error));
    return s.isEmpty() ? QStringLiteral("error %1").arg(error) : s;
}

QString fromErrno()
{
    return fromErrno(errno);
}

} // namespace Errors
