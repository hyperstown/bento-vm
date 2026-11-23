// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "PageDialog.h"
#include "core/VmConfig.h"

#include <QPointer>

class CpuEditor;
class MemoryEditor;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QPlainTextEdit;
class QTableWidget;
class QTreeWidget;
class Vm;

class VmSettingsDialog : public PageDialog
{
    Q_OBJECT
public:
    explicit VmSettingsDialog(Vm *vm, QWidget *parent = nullptr);
    void showPage(const QString &name);   // "usb", "sharing", "advanced"

protected:
    void accept() override;

private:
    void buildGeneral();
    void buildResources();
    void buildDisplay();
    void buildStorage();
    void buildNetwork();
    void buildUsb();
    void refreshUsbList();
    void buildIntegration();
    void buildSharing();
    void refreshSharing();
    void buildFirmware();
    void buildAdvanced();
    void refreshDisks();
    void refreshCommand();
    VmConfig collect() const;
    bool apply();

    QPointer<Vm> m_vm;
    VmConfig m_cfg;

    QLineEdit *m_name = nullptr;
    QComboBox *m_os = nullptr;
    QPlainTextEdit *m_notes = nullptr;
    CpuEditor *m_cpu = nullptr;
    MemoryEditor *m_mem = nullptr;
    QComboBox *m_graphics = nullptr;
    QComboBox *m_hostmem = nullptr;
    QComboBox *m_display = nullptr;
    QCheckBox *m_autoResize = nullptr;
    QTreeWidget *m_disks = nullptr;
    QLineEdit *m_cd[2] = {nullptr, nullptr};
    QCheckBox *m_network = nullptr;
    QComboBox *m_nic = nullptr;
    QTableWidget *m_forwards = nullptr;
    QListWidget *m_usbList = nullptr;
    QPushButton *m_usbAllow = nullptr;
    int m_usbPage = -1;
    int m_advancedPage = -1;
    QComboBox *m_clipboard = nullptr;
    QCheckBox *m_fileTransfer = nullptr;
    QCheckBox *m_audio = nullptr;
    int m_sharePage = -1;
    QCheckBox *m_share = nullptr;
    QLineEdit *m_shareFolder = nullptr;
    QPushButton *m_shareBrowse = nullptr;
    QCheckBox *m_shareReadOnly = nullptr;
    QWidget *m_shareWarning = nullptr;
    QGroupBox *m_shareGuideBox = nullptr;
    QWidget *m_shareGuide = nullptr;
    QComboBox *m_firmware = nullptr;
    QCheckBox *m_secureBoot = nullptr;
    QCheckBox *m_tpm = nullptr;
    QCheckBox *m_localtime = nullptr;
    QCheckBox *m_hyperv = nullptr;
    QLineEdit *m_extraArgs = nullptr;
    QPlainTextEdit *m_command = nullptr;
};
