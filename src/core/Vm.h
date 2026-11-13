#pragma once

#include "VmConfig.h"

#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QImage>
#include <QObject>

#include <functional>

class QProcess;
class QmpClient;
struct UsbDevice;

// A host USB device currently plugged into the VM (QEMU usb-host device).
struct UsbAttachment
{
    QString qdevId;
    int hostBus = 0;        // set for hot-plugged devices (matched by port)
    QString hostPort;
    QString vendorId;       // set for auto-connect devices (matched by id)
    QString productId;
    bool autoConnect() const { return hostBus == 0; }
    bool matches(const UsbDevice &d) const;
};
class QTimer;
class VdAgent;

// Runtime representation of one VM: owns the QEMU process lifecycle (QEMU
// runs detached so VMs survive the GUI closing), the QMP connection and the
// guest agent channel.
class Vm : public QObject
{
    Q_OBJECT
public:
    enum class State { Stopped, Starting, Running, Paused, Stopping };
    Q_ENUM(State)

    explicit Vm(const VmConfig &cfg, QObject *parent = nullptr);
    ~Vm() override;

    QString id() const { return m_cfg.id; }
    QString name() const { return m_cfg.name; }
    const VmConfig &config() const { return m_cfg; }
    bool updateConfig(const VmConfig &cfg, QString *error = nullptr);   // saves to disk
    void reloadConfig(const VmConfig &cfg);                             // from disk, no save

    State state() const { return m_state; }
    bool isActive() const { return m_state != State::Stopped; }
    static QString stateLabel(State s);

    bool isAvailable() const { return m_available; }
    QString unavailableReason() const { return m_unavailableReason; }
    QString unavailableShort() const { return m_unavailableShort; }
    void setAvailability(bool available, const QString &reason = {}, const QString &shortReason = {});

    bool start(QString *error = nullptr);
    void shutdown();      // ACPI power button
    void forceStop();
    void reboot();
    void pause();
    void resume();
    bool tryAttach();     // reconnect to an already-running QEMU (after GUI restart)

    // CD-ROM media at runtime (also persisted to the config).
    void changeCdrom(int index, const QString &isoPath);

    // Host USB passthrough while running.
    const QList<UsbAttachment> &usbAttachments() const { return m_usb; }
    const UsbAttachment *usbAttachmentFor(const UsbDevice &d) const;
    // `accessHolder` (optional) keeps temporary device access alive while attached.
    void attachUsb(const UsbDevice &d, std::function<void(const QString &error)> done = {},
                   QProcess *accessHolder = nullptr);
    void detachUsb(const QString &qdevId, std::function<void(const QString &error)> done = {});
    void refreshUsb();

    QString runtimeDir() const;
    QString logFile() const;
    QmpClient *qmp() const { return m_qmp; }
    VdAgent *agent() const { return m_agent; }

    QImage thumbnail() const { return m_thumbnail; }
    void setThumbnail(const QImage &img);

    QStringList lastWarnings() const { return m_warnings; }

Q_SIGNALS:
    void stateChanged(Vm::State state);
    void configChanged();
    void availabilityChanged();
    void thumbnailChanged();
    void errorOccurred(const QString &title, const QString &details);
    void qmpReady();
    void usbChanged();

private:
    void setState(State s);
    void pollStartup();
    void onQmpReady();
    void onQmpEvent(const QString &event, const QJsonObject &data);
    void onQmpDisconnected();
    void cleanupRuntime();
    bool startSwtpm(QString *error);
    QString logTail(int maxLines = 25) const;
    qint64 readPid() const;
    static bool pidAlive(qint64 pid);

    VmConfig m_cfg;
    State m_state = State::Stopped;
    bool m_available = true;
    QString m_unavailableReason;
    QString m_unavailableShort;
    QmpClient *m_qmp;
    VdAgent *m_agent;
    QTimer *m_startTimer;
    qint64 m_pid = 0;
    QDateTime m_startedAt;
    qint64 m_logOffset = 0;
    bool m_shutdownRequested = false;
    bool m_guestShutdown = false;
    QStringList m_warnings;
    QImage m_thumbnail;
    QList<UsbAttachment> m_usb;
    QHash<QString, QPointer<QProcess>> m_usbAccess;   // qdev id -> ACL helper
    void releaseUnusedUsbAccess();
};
