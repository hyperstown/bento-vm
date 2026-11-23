// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "SharedFolderGuide.h"
#include "Icons.h"
#include "PageDialog.h"
#include "core/SharedFolder.h"
#include "core/Vm.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace SharedFolderGuide {

static QLabel *text(const QString &s, QWidget *parent)
{
    auto *l = new QLabel(s, parent);
    l->setWordWrap(true);
    l->setOpenExternalLinks(true);
    l->setTextInteractionFlags(Qt::TextBrowserInteraction);
    return l;
}

// A command in a read-only field with a Copy button.
static QWidget *code(const QString &command, QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    auto *edit = new QLineEdit(command, w);
    edit->setReadOnly(true);
    edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    edit->setCursorPosition(0);
    auto *copy = new QPushButton(Icons::fromTheme(QStringLiteral("edit-copy")), QString(), w);
    copy->setToolTip(QStringLiteral("Copy"));
    copy->setAutoDefault(false);   // in a dialog, Enter stays with its own buttons
    QObject::connect(copy, &QPushButton::clicked, w, [command] { QApplication::clipboard()->setText(command); });
    l->addWidget(edit, 1);
    l->addWidget(copy);
    return w;
}

QWidget *create(OsFamily family, QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    const QString tag = SharedFolder::tag();
    const QString mountPoint = QStringLiteral("/mnt/") + tag;

    if (family == OsFamily::Linux) {
        l->addWidget(text(QStringLiteral("Mount it with:"), w));
        l->addWidget(code(QStringLiteral("sudo mkdir -p %1").arg(mountPoint), w));
        l->addWidget(code(QStringLiteral("sudo mount -t virtiofs %1 %2").arg(tag, mountPoint), w));
        l->addWidget(text(QStringLiteral("To mount it every time the VM starts, add this line to <tt>/etc/fstab</tt>:"), w));
        l->addWidget(code(QStringLiteral("%1  %2  virtiofs  defaults,nofail  0  0").arg(tag, mountPoint), w));
        l->addWidget(PageDialog::hint(QStringLiteral("virtio-fs is built into the kernels of current Linux distributions "
                                                     "(Linux 5.4 and newer)."), w));
    } else if (family == OsFamily::Windows) {
        l->addWidget(text(QStringLiteral(
            "<ol style='margin-left:-20px'>"
            "<li>Install the virtio-win guest tools. They include the VirtIO-FS driver and service.</li>"
            "<li>Install <a href='https://winfsp.dev/rel/'>WinFsp</a>.</li>"
            "<li>In <i>Services</i>, set <i>VirtIO-FS Service</i> to start automatically, then start it.</li>"
            "</ol>"
            "The folder appears in File Explorer as a new drive, usually <b>Z:</b>."), w));
    } else {
        l->addWidget(text(QStringLiteral("The guest needs a virtio-fs driver to open it. Linux and Windows have one; "
                                         "it mounts the folder by its tag, <tt>%1</tt>.").arg(tag), w));
    }
    return w;
}

QString openOnHost(const Vm *vm)
{
    const VmConfig &c = vm->config();
    if (c.sharedFolder.isEmpty())
        return QStringLiteral("%1 has no shared folder. Choose one in Settings → Shared Folder.").arg(c.name);
    const QString folder = c.absolutePath(c.sharedFolder);
    if (!QFileInfo(folder).isDir())
        return QStringLiteral("The shared folder “%1” wasn't found.").arg(folder);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))
        return QStringLiteral("No file manager could open “%1”.").arg(folder);
    return {};
}

void showDialog(Vm *vm, QWidget *parent)
{
    const VmConfig &c = vm->config();
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("%1 — Shared Folder").arg(c.name));
    dlg.setMinimumWidth(640);
    auto *lay = new QVBoxLayout(&dlg);

    auto *hostBox = new QGroupBox(QStringLiteral("On this computer"), &dlg);
    auto *hl = new QVBoxLayout(hostBox);
    const QString folder = c.absolutePath(c.sharedFolder);
    hl->addWidget(text(QStringLiteral("<a href=\"%1\">%2</a>%3")
                           .arg(QUrl::fromLocalFile(folder).toString(), folder.toHtmlEscaped(),
                                c.sharedFolderReadOnly ? QStringLiteral(" (read-only for the VM)") : QString()),
                       hostBox));
    const QString problem = SharedFolder::problem(c);
    if (!problem.isEmpty())
        hl->addWidget(PageDialog::hint(problem, hostBox));
    lay->addWidget(hostBox);

    auto *guestBox = new QGroupBox(QStringLiteral("In the VM"), &dlg);
    auto *gl = new QVBoxLayout(guestBox);
    gl->addWidget(create(OsDb::profile(c.osId).family, guestBox));
    lay->addWidget(guestBox);

    lay->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(buttons);
    dlg.exec();
}

} // namespace SharedFolderGuide
