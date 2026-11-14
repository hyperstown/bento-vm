// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>

class QProcess;

// Error messages for people. Every function returns a non-empty sentence, so
// no error dialog ever ends up blank.
namespace Errors {

// Why a helper program (qemu-img, swtpm, pkexec, …) failed: couldn't be
// started, timed out, crashed, its own message, or its exit code. Call it once
// the process has finished or a waitFor…() call gave up; it reads the
// process's remaining output.
QString fromProcess(QProcess &process, const QString &program);

// strerror(errno) of the last failed system call, e.g. "Permission denied".
QString fromErrno();
QString fromErrno(int error);

// At most `lines` trailing non-empty lines of `text`, trimmed.
QString lastLines(const QString &text, int lines);

} // namespace Errors
