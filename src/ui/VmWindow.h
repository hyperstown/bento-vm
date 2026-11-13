#pragma once

#include <QMainWindow>
#include <QPointer>

class DBusDisplay;
class DisplayWidget;
class QAction;
class QActionGroup;
class QFrame;
class QLabel;
class QProcess;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTimer;
class QToolButton;
class Vm;
struct UsbDevice;

// One window per VM showing the embedded display plus VM controls.
class VmWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit VmWindow(Vm *vm, QWidget *parent = nullptr);
    ~VmWindow() override;

    Vm *vm() const { return m_vm; }
    void sendFiles(const QStringList &files);

Q_SIGNALS:
    void settingsRequested(Vm *vm, const QString &page = QString());

protected:
    void closeEvent(QCloseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    void buildUi();
    void buildToolbar();
    QWidget *buildOffPage();
    QWidget *buildWaitPage();
    QWidget *buildExternalPage();
    void updateState();
    void updateAgentStatus();
    void updateClipboardButton();
    void updateMediaMenu();
    void connectDisplay();
    void disconnectDisplay();
    void onDisplayFailed(const QString &error);
    void showToast(const QString &text, int ms = 4500, bool progress = false);
    void positionToast();
    void toggleFullscreen();
    void saveThumbnail();
    void sendCombo(const QList<uint32_t> &qnums);
    void startVm();
    void takeScreenshot();
    void rebuildUsbMenu();
    void updateUsbButton();
    void connectUsb(const UsbDevice &d);
    void attachUsbNow(const UsbDevice &d, QProcess *accessHolder = nullptr);
    void requestOneTimeUsbAccess(const UsbDevice &d);
    void disconnectUsb(const QString &qdevId, const QString &name, bool autoConnect);

    QPointer<Vm> m_vm;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_offPage = nullptr;
    QWidget *m_waitPage = nullptr;
    QLabel *m_waitTitle = nullptr;
    QLabel *m_waitDetail = nullptr;
    QPushButton *m_retryButton = nullptr;
    QWidget *m_externalPage = nullptr;
    QLabel *m_offBadge = nullptr;
    QLabel *m_offTitle = nullptr;
    QLabel *m_offSpecs = nullptr;
    DisplayWidget *m_display = nullptr;
    DBusDisplay *m_dbus = nullptr;

    QToolBar *m_toolbar = nullptr;
    QAction *m_actStart = nullptr;
    QAction *m_actShutdown = nullptr;
    QAction *m_actPause = nullptr;
    QAction *m_actKeys = nullptr;
    QAction *m_actClipboard = nullptr;
    QAction *m_actSendFiles = nullptr;
    QAction *m_actMedia = nullptr;
    QAction *m_actUsb = nullptr;
    QAction *m_actScreenshot = nullptr;
    QAction *m_actFullscreen = nullptr;
    QToolButton *m_clipButton = nullptr;
    QToolButton *m_mediaButton = nullptr;
    QActionGroup *m_clipGroup = nullptr;
    QLabel *m_agentStatus = nullptr;

    QFrame *m_toast = nullptr;
    QLabel *m_toastLabel = nullptr;
    QProgressBar *m_toastProgress = nullptr;
    QPushButton *m_toastCancel = nullptr;
    QTimer *m_toastTimer = nullptr;
    QTimer *m_thumbTimer = nullptr;
    int m_displayRetries = 0;
    bool m_closingForGood = false;
};
