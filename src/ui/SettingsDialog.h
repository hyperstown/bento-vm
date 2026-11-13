#pragma once

#include "PageDialog.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;

// Application preferences.
class SettingsDialog : public PageDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void showStoragePage();

protected:
    void accept() override;
    void reject() override;

private:
    void buildAppearance();
    void buildStorage();
    void buildDefaults();
    void buildBehaviour();
    void buildQemu();
    void refreshLocations();
    void save();

    QString m_originalTheme;
    QButtonGroup *m_themeGroup = nullptr;
    QStringList m_locations;
    QString m_defaultLocation;
    QListWidget *m_locationList = nullptr;
    QComboBox *m_graphics = nullptr;
    QComboBox *m_hostmem = nullptr;
    QComboBox *m_display = nullptr;
    QComboBox *m_clipboard = nullptr;
    QCheckBox *m_fileTransfer = nullptr;
    QComboBox *m_firmware = nullptr;
    QComboBox *m_closeAction = nullptr;
    QLineEdit *m_qemuBinary = nullptr;
    QLineEdit *m_extraArgs = nullptr;
    int m_storagePage = 1;
};

// Shared combo helpers (also used by the wizard and VM settings).
namespace Combos {
QComboBox *graphics(QWidget *parent);
QComboBox *hostmem(QWidget *parent);
QComboBox *display(QWidget *parent);
QComboBox *clipboard(QWidget *parent);
QComboBox *firmware(QWidget *parent);
void select(QComboBox *box, const QVariant &data);
}
