#pragma once

#include "core/OsDb.h"

#include <QDialog>
#include <QFutureWatcher>

class CpuEditor;
class MemoryEditor;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;
class Vm;

// Step-by-step VM creation: source → OS → resources → features → summary.
class NewVmWizard : public QDialog
{
    Q_OBJECT
public:
    explicit NewVmWizard(QWidget *parent = nullptr);
    void setSourcePath(const QString &path);   // pre-fill from a dropped ISO/disk

Q_SIGNALS:
    void vmCreated(Vm *vm, bool startNow);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    enum Page { SourcePage, OsPage, ResourcesPage, FeaturesPage, SummaryPage, PageCount };

    QWidget *buildSourcePage();
    QWidget *buildOsPage();
    QWidget *buildResourcesPage();
    QWidget *buildFeaturesPage();
    QWidget *buildSummaryPage();
    QFrame *optionCard(QRadioButton *radio, const QString &description, QWidget *extra);

    void goTo(int page);
    void next();
    void back();
    bool validatePage(int page);
    void updateButtons();
    void updateStepLabel();

    bool isoMode() const;
    QString sourcePath() const;
    void onSourceChanged();
    void scanForIsos();
    void startDetection();
    void onDetected();
    void applyProfile();
    void updateDiskInfo();
    void updateLocationInfo();
    void updateSummary();
    void updateFeatureHints();
    QString uniqueName(const QString &base) const;
    void create();

    QStackedWidget *m_pages = nullptr;
    QLabel *m_stepTitle = nullptr;
    QLabel *m_stepCounter = nullptr;
    QPushButton *m_back = nullptr;
    QPushButton *m_next = nullptr;
    QPushButton *m_cancel = nullptr;

    // Source
    QRadioButton *m_isoRadio = nullptr;
    QRadioButton *m_diskRadio = nullptr;
    QFrame *m_isoCard = nullptr;
    QFrame *m_diskCard = nullptr;
    QLineEdit *m_isoPath = nullptr;
    QListWidget *m_isoList = nullptr;
    QLineEdit *m_diskPath = nullptr;
    QRadioButton *m_diskInPlace = nullptr;
    QRadioButton *m_diskCopy = nullptr;
    QLabel *m_diskInfo = nullptr;

    // OS
    QLabel *m_detectIcon = nullptr;
    QLabel *m_detectTitle = nullptr;
    QLabel *m_detectDetail = nullptr;
    QComboBox *m_os = nullptr;
    QLineEdit *m_name = nullptr;
    QLabel *m_osHint = nullptr;
    bool m_nameEdited = false;

    // Resources
    CpuEditor *m_cpu = nullptr;
    MemoryEditor *m_mem = nullptr;
    QSpinBox *m_diskSize = nullptr;
    QWidget *m_diskSizeRow = nullptr;
    QComboBox *m_location = nullptr;
    QLabel *m_locationInfo = nullptr;

    // Features
    QComboBox *m_graphics = nullptr;
    QComboBox *m_hostmem = nullptr;
    QComboBox *m_display = nullptr;
    QComboBox *m_firmware = nullptr;
    QCheckBox *m_secureBoot = nullptr;
    QCheckBox *m_tpm = nullptr;
    QComboBox *m_clipboard = nullptr;
    QCheckBox *m_fileTransfer = nullptr;
    QCheckBox *m_audio = nullptr;
    QCheckBox *m_ssh = nullptr;
    QSpinBox *m_sshPort = nullptr;
    QLabel *m_featureHint = nullptr;

    // Summary
    QLabel *m_summary = nullptr;
    QCheckBox *m_startNow = nullptr;

    QString m_detectedFor;
    QString m_appliedProfileFor;
    OsDb::Detection m_detection;
    QFutureWatcher<OsDb::Detection> m_detectWatcher;
    QString m_driverIso;   // virtio-win ISO found next to Windows media
};
