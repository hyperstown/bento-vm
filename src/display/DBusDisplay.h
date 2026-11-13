#pragma once

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QRect>
#include <QVector>

#include <cstdint>

class QmpClient;

typedef struct _GCancellable GCancellable;
typedef struct _GDBusConnection GDBusConnection;
typedef struct _GDBusMethodInvocation GDBusMethodInvocation;
typedef struct _GVariant GVariant;

struct DmabufPlane
{
    int fd = -1;            // owned by whoever holds the DmabufScanout
    uint32_t offset = 0;
    uint32_t stride = 0;
};

struct DmabufScanout
{
    QVector<DmabufPlane> planes;
    uint32_t fourcc = 0;
    uint64_t modifier = 0x00ffffffffffffffULL;   // DRM_FORMAT_MOD_INVALID
    uint32_t backingWidth = 0;
    uint32_t backingHeight = 0;
    QRect visible;          // region of the backing buffer that is the screen
    bool y0Top = false;

    void closeFds();
};

// Client for QEMU's D-Bus display (-display dbus,p2p=yes). The connection is
// handed to QEMU through QMP add_client; frames arrive via a listener object
// we export on a second peer-to-peer connection. With gl=on frames are
// DMA-BUFs rendered by virglrenderer (this is what makes Venus work).
class DBusDisplay : public QObject
{
    Q_OBJECT
public:
    explicit DBusDisplay(QmpClient *qmp, QObject *parent = nullptr);
    ~DBusDisplay() override;

    void start();
    void stop();
    bool isConnected() const { return m_listenerConn != nullptr; }

    void keyPress(uint32_t qnum);
    void keyRelease(uint32_t qnum);
    void mousePress(uint32_t button);
    void mouseRelease(uint32_t button);
    void mouseMoveAbs(uint32_t x, uint32_t y);
    void mouseMoveRel(int dx, int dy);
    bool mouseIsAbsolute() const { return m_mouseAbsolute; }
    void setUiInfo(uint32_t width, uint32_t height, uint16_t widthMm, uint16_t heightMm);

    // QEMU blocks the guest's GPU queue until each UpdateDMABUF is answered;
    // the widget calls this after it has drawn the frame.
    void ackFrame();
    bool hasPendingFrame() const { return m_pendingUpdate != nullptr; }

    enum MouseButton : uint32_t { Left = 0, Middle = 1, Right = 2, WheelUp = 3, WheelDown = 4, Side = 5, Extra = 6 };

Q_SIGNALS:
    void connected();
    void failed(const QString &error);
    void disconnected();

    void scanoutDmabuf(const DmabufScanout &scanout);   // receiver takes ownership of fds
    void updateDmabuf(const QRect &rect);
    void scanoutImage(const QImage &image);
    void updateImage(const QRect &rect, const QImage &patch);
    void disabled();
    void cursorDefined(const QImage &cursor, int hotX, int hotY);
    void mouseSet(int x, int y, bool visible);
    void mouseModeChanged(bool absolute);

public:
    // Entry points for the C callbacks.
    void handleListenerCall(const char *method, GVariant *params, GDBusMethodInvocation *inv);
    void onBusConnection(GDBusConnection *conn);
    void onListenerConnection(GDBusConnection *conn);
    void onConsoleIds(GVariant *reply);
    void onMouseAbsolute(bool abs);
    void onRegistered();
    void fail(const QString &error);
    void onClosed();

private:
    void registerListener();
    void callConsole(const char *iface, const char *method, GVariant *params);
    void maybeConnected();

    QmpClient *m_qmp;
    GCancellable *m_cancel = nullptr;
    GDBusConnection *m_conn = nullptr;
    GDBusConnection *m_listenerConn = nullptr;
    QByteArray m_consolePath;
    QVector<unsigned> m_registrations;
    unsigned m_propsSub = 0;
    GDBusMethodInvocation *m_pendingUpdate = nullptr;
    bool m_mouseAbsolute = true;
    bool m_registered = false;
    bool m_listenerReady = false;
    bool m_failed = false;
};
