// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ResourceEditors.h"
#include "core/HostInfo.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

static QLabel *mutedLabel(QWidget *parent)
{
    auto *l = new QLabel(parent);
    l->setObjectName(QStringLiteral("hintLabel"));
    l->setWordWrap(true);
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, p.color(QPalette::Disabled, QPalette::WindowText));
    l->setPalette(p);
    return l;
}

static QLabel *warningLabel(QWidget *parent)
{
    auto *l = new QLabel(parent);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color: #e8a317;"));
    l->hide();
    return l;
}

static QPushButton *linkButton(const QString &text, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setFlat(true);
    b->setProperty("flat", true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

// ---------------------------------------------------------------------------

CpuEditor::CpuEditor(QWidget *parent)
    : QWidget(parent)
{
    const HostInfo &h = HostInfo::get();
    m_cores = new QSpinBox(this);
    m_cores->setRange(1, qMax(1, h.logicalCpus * 2));
    m_cores->setSuffix(QStringLiteral(" cores"));
    m_threads = new QComboBox(this);
    m_threads->addItem(QStringLiteral("1 thread per core"), 1);
    m_threads->addItem(QStringLiteral("2 threads per core (SMT)"), 2);
    m_total = new QLabel(this);
    QFont bold = m_total->font();
    bold.setBold(true);
    m_total->setFont(bold);
    m_hint = mutedLabel(this);
    m_warning = warningLabel(this);
    m_useRec = linkButton(QStringLiteral("Use recommended"), this);

    auto *row = new QHBoxLayout;
    row->addWidget(m_cores);
    row->addWidget(new QLabel(QStringLiteral("×"), this));
    row->addWidget(m_threads);
    row->addWidget(new QLabel(QStringLiteral("="), this));
    row->addWidget(m_total);
    row->addStretch();

    auto *hintRow = new QHBoxLayout;
    hintRow->addWidget(m_hint, 1);
    hintRow->addWidget(m_useRec);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addLayout(row);
    lay->addLayout(hintRow);
    lay->addWidget(m_warning);

    connect(m_cores, &QSpinBox::valueChanged, this, [this] { refresh(); Q_EMIT changed(); });
    connect(m_threads, &QComboBox::currentIndexChanged, this, [this] { refresh(); Q_EMIT changed(); });
    connect(m_useRec, &QPushButton::clicked, this, [this] { setValue(m_recCores, m_recThreads); });
    refresh();
}

void CpuEditor::setValue(int cores, int threads)
{
    m_cores->setValue(cores);
    m_threads->setCurrentIndex(threads >= 2 ? 1 : 0);
    refresh();
}

int CpuEditor::cores() const { return m_cores->value(); }
int CpuEditor::threads() const { return m_threads->currentData().toInt(); }

void CpuEditor::setRecommended(int cores, int threads)
{
    m_recCores = cores;
    m_recThreads = threads;
    refresh();
}

void CpuEditor::refresh()
{
    const HostInfo &h = HostInfo::get();
    const int total = cores() * threads();
    m_total->setText(QStringLiteral("%1 virtual CPU%2").arg(total).arg(total == 1 ? "" : "s"));
    m_hint->setText(QStringLiteral("Recommended: %1 × %2 (%3 vCPUs). Host: %4 cores / %5 threads%6")
                        .arg(m_recCores)
                        .arg(m_recThreads)
                        .arg(m_recCores * m_recThreads)
                        .arg(h.physicalCores)
                        .arg(h.logicalCpus)
                        .arg(h.cpuModel.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(h.cpuModel)));
    m_useRec->setVisible(cores() != m_recCores || threads() != m_recThreads);
    if (total > h.logicalCpus) {
        m_warning->setText(QStringLiteral("More virtual CPUs than the host has threads will make the VM slower, not faster."));
        m_warning->show();
    } else if (total == h.logicalCpus) {
        m_warning->setText(QStringLiteral("Using every host thread can make the desktop sluggish while the VM is busy."));
        m_warning->show();
    } else if (threads() == 2 && h.threadsPerCore < 2) {
        m_warning->setText(QStringLiteral("Your CPU has no SMT; 1 thread per core matches the host better."));
        m_warning->show();
    } else {
        m_warning->hide();
    }
}

// ---------------------------------------------------------------------------

MemoryEditor::MemoryEditor(QWidget *parent)
    : QWidget(parent)
{
    const HostInfo &h = HostInfo::get();
    const int maxMiB = int(qMax<qint64>(2048, h.totalRamMiB - 512));
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(1, maxMiB / 512);   // in 512 MiB steps
    m_slider->setPageStep(2);
    m_spin = new QDoubleSpinBox(this);
    m_spin->setDecimals(1);
    m_spin->setSingleStep(0.5);
    m_spin->setRange(0.5, maxMiB / 1024.0);
    m_spin->setSuffix(QStringLiteral(" GiB"));
    m_hint = mutedLabel(this);
    m_warning = warningLabel(this);
    m_useRec = linkButton(QStringLiteral("Use recommended"), this);

    auto *row = new QHBoxLayout;
    row->addWidget(m_slider, 1);
    row->addWidget(m_spin);
    auto *hintRow = new QHBoxLayout;
    hintRow->addWidget(m_hint, 1);
    hintRow->addWidget(m_useRec);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addLayout(row);
    lay->addLayout(hintRow);
    lay->addWidget(m_warning);

    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        if (m_syncing)
            return;
        m_syncing = true;
        m_spin->setValue(v / 2.0);
        m_syncing = false;
        refresh();
        Q_EMIT changed();
    });
    connect(m_spin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_syncing)
            return;
        m_syncing = true;
        m_slider->setValue(qRound(v * 2));
        m_syncing = false;
        refresh();
        Q_EMIT changed();
    });
    connect(m_useRec, &QPushButton::clicked, this, [this] { setValueMiB(m_recMiB); });
    refresh();
}

void MemoryEditor::setValueMiB(int mib)
{
    m_syncing = true;
    m_spin->setValue(mib / 1024.0);
    m_slider->setValue(qRound(mib / 512.0));
    m_syncing = false;
    refresh();
    Q_EMIT changed();
}

int MemoryEditor::valueMiB() const
{
    return int(qRound(m_spin->value() * 2) * 512);
}

void MemoryEditor::setRecommended(int mib)
{
    m_recMiB = mib;
    refresh();
}

void MemoryEditor::refresh()
{
    const HostInfo &h = HostInfo::get();
    const qint64 avail = HostInfo::availableRamMiB();
    m_hint->setText(QStringLiteral("Recommended: %1 GiB. Host: %2 GiB total, %3 GiB free right now.")
                        .arg(m_recMiB / 1024.0, 0, 'f', m_recMiB % 1024 ? 1 : 0)
                        .arg(h.totalRamMiB / 1024.0, 0, 'f', 1)
                        .arg(avail / 1024.0, 0, 'f', 1));
    m_useRec->setVisible(valueMiB() != m_recMiB);
    const int v = valueMiB();
    if (v > h.totalRamMiB * 3 / 4) {
        m_warning->setText(QStringLiteral("This leaves very little memory for the host."));
        m_warning->show();
    } else if (avail > 0 && v > avail) {
        m_warning->setText(QStringLiteral("More than is free right now; the host may start swapping while the VM runs."));
        m_warning->show();
    } else {
        m_warning->hide();
    }
}
