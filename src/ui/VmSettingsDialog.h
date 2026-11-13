#pragma once

#include "PageDialog.h"
#include "core/VmConfig.h"

#include <QPointer>

class CpuEditor;
class MemoryEditor;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;
class QTreeWidget;
class Vm;

class VmSettingsDialog : public PageDialog
{
    Q_OBJECT
public:
    explicit VmSettingsDialog(Vm *vm, QWidget *parent = nullptr);

protected:
    void accept() override;

private:
    void buildGeneral();
    void buildResources();
    void buildDisplay();
    void buildStorage();
    void buildNetwork();
    void buildIntegration();
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
    QComboBox *m_clipboard = nullptr;
    QCheckBox *m_fileTransfer = nullptr;
    QCheckBox *m_audio = nullptr;
    QComboBox *m_firmware = nullptr;
    QCheckBox *m_secureBoot = nullptr;
    QCheckBox *m_tpm = nullptr;
    QCheckBox *m_localtime = nullptr;
    QCheckBox *m_hyperv = nullptr;
    QLineEdit *m_extraArgs = nullptr;
    QPlainTextEdit *m_command = nullptr;
};
