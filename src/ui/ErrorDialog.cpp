// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ErrorDialog.h"
#include "core/HostInfo.h"

#include <QGridLayout>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QSpacerItem>
#include <QSysInfo>

Q_LOGGING_CATEGORY(lcErrors, "bento.errors")

namespace {

QString versions()
{
    const QVersionNumber qemu = HostInfo::qemuVersion();
    return QStringLiteral("BentoVM %1 · QEMU %2 · Qt %3 · %4 · Linux %5")
        .arg(QStringLiteral(BENTO_VERSION), qemu.isNull() ? QStringLiteral("not found") : qemu.toString(),
             QString::fromLatin1(qVersion()), QSysInfo::prettyProductName(), QSysInfo::kernelVersion());
}

QString paragraphs(const QString &text)
{
    return QStringLiteral("<p>%1</p>").arg(text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
}

} // namespace

void ErrorDialog::show(QWidget *parent, const QString &title, const QString &reason, const QString &details)
{
    const QString what = title.trimmed().isEmpty() ? QStringLiteral("Something went wrong") : title.trimmed();
    QString why = reason.trimmed();
    QString more = details.trimmed();
    if (why.isEmpty()) {
        why = QStringLiteral("BentoVM didn't get a reason for this, which is a bug. Please report it with the details "
                             "below.");
        more = QStringLiteral("Error: %1\n%2").arg(what, more).trimmed();
    }
    qCWarning(lcErrors).noquote() << what + QStringLiteral(": ") + why + (more.isEmpty() ? QString() : QLatin1Char('\n') + more);

    QMessageBox box(QMessageBox::Warning, what, QString(), QMessageBox::Ok, parent);
    // The title also goes into the message: window titles get cut short.
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral("<b>%1</b>").arg(what.toHtmlEscaped()));
    box.setInformativeText(paragraphs(why));
    if (!more.isEmpty())
        box.setDetailedText(more + QStringLiteral("\n\n") + versions());
    // QMessageBox sizes itself to the text and ignores a minimum width;
    // a spacer keeps sentences and details from wrapping after a few words.
    if (auto *grid = qobject_cast<QGridLayout *>(box.layout()))
        grid->addItem(new QSpacerItem(500, 0, QSizePolicy::Minimum, QSizePolicy::Fixed), grid->rowCount(), 0, 1,
                      grid->columnCount());
    box.exec();
}
