// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

// Cores × threads editor with the host's topology and a recommendation.
class CpuEditor : public QWidget
{
    Q_OBJECT
public:
    explicit CpuEditor(QWidget *parent = nullptr);
    void setValue(int cores, int threads);
    int cores() const;
    int threads() const;
    void setRecommended(int cores, int threads);

Q_SIGNALS:
    void changed();

private:
    void refresh();
    QSpinBox *m_cores = nullptr;
    QComboBox *m_threads = nullptr;
    QLabel *m_total = nullptr;
    QLabel *m_hint = nullptr;
    QLabel *m_warning = nullptr;
    QPushButton *m_useRec = nullptr;
    int m_recCores = 2;
    int m_recThreads = 1;
};

// RAM editor (GiB with 0.5 steps) with a recommendation and host limits.
class MemoryEditor : public QWidget
{
    Q_OBJECT
public:
    explicit MemoryEditor(QWidget *parent = nullptr);
    void setValueMiB(int mib);
    int valueMiB() const;
    void setRecommended(int mib);

Q_SIGNALS:
    void changed();

private:
    void refresh();
    QSlider *m_slider = nullptr;
    QDoubleSpinBox *m_spin = nullptr;
    QLabel *m_hint = nullptr;
    QLabel *m_warning = nullptr;
    QPushButton *m_useRec = nullptr;
    int m_recMiB = 4096;
    bool m_syncing = false;
};
