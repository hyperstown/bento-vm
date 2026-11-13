#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QStringList>

class QFile;
class QLocalSocket;
class QTimer;

// Host side of the SPICE guest-agent protocol (the guest runs spice-vdagent).
// QEMU exposes the guest's com.redhat.spice.0 virtio-serial port as a unix
// socket and we speak the protocol directly. This gives us:
//   * clipboard sync with per-direction control
//   * file transfer (drag & drop into the VM)
class VdAgent : public QObject
{
    Q_OBJECT
public:
    explicit VdAgent(QObject *parent = nullptr);
    ~VdAgent() override;

    void connectTo(const QString &socketPath);
    void disconnectAgent();

    bool isGuestConnected() const { return m_guestConnected; }
    bool guestSupportsClipboard() const;
    bool guestSupportsFileTransfer() const;

    void setClipboardMode(const QString &mode);   // both | host-to-guest | guest-to-host | off
    QString clipboardMode() const { return m_clipboardMode; }
    void setFileTransferEnabled(bool on) { m_fileTransferEnabled = on; }
    bool fileTransferEnabled() const { return m_fileTransferEnabled; }

    // Queues files for transfer. Folders are skipped (reported via notice()).
    int sendFiles(const QStringList &paths);
    void cancelTransfers();
    int activeTransfers() const { return int(m_xfers.size()); }

Q_SIGNALS:
    void guestConnectedChanged(bool connected);
    void transferProgress(qint64 sent, qint64 total, int filesLeft);
    void transferFinished(const QString &name, bool ok, const QString &message);
    void notice(const QString &text);

private:
    struct Xfer
    {
        quint32 id = 0;
        QString name;
        qint64 size = 0;
        qint64 sent = 0;
        QFile *file = nullptr;
        bool canSend = false;
    };

    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void tryConnect();
    void processMessages();
    void handleMessage(quint32 type, const QByteArray &data);
    void sendMessage(quint32 type, const QByteArray &data);
    void sendCaps(bool request);
    void setGuestConnected(bool on);

    void onHostClipboardChanged();
    void handleGuestGrab(const QByteArray &data);
    void handleGuestRequest(const QByteArray &data);
    void handleGuestClipboard(const QByteArray &data);
    bool hostToGuest() const;
    bool guestToHost() const;

    void handleXferStatus(const QByteArray &data);
    void pumpTransfers();
    void finishXfer(Xfer *x, bool ok, const QString &message);
    void emitProgress();

    QLocalSocket *m_sock = nullptr;
    QTimer *m_retry = nullptr;
    QString m_path;
    bool m_wanted = false;

    QByteArray m_in;
    quint32 m_chunkRemaining = 0;
    QByteArray m_msg;

    bool m_guestConnected = false;
    QList<quint32> m_guestCaps;

    QString m_clipboardMode = QStringLiteral("both");
    bool m_fileTransferEnabled = true;
    bool m_hostGrabbed = false;
    bool m_settingHostClipboard = false;
    // What we last put on the host clipboard on the guest's behalf; used to
    // avoid echoing it straight back as a host grab.
    QString m_lastGuestText;
    size_t m_lastGuestImageHash = 0;

    QList<Xfer *> m_xfers;
    quint32 m_nextXferId = 1;
    qint64 m_batchTotal = 0;
    qint64 m_batchSent = 0;
};
