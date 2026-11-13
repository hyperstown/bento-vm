#include "DBusDisplay.h"
#include "GlibIntegration.h"
#include "core/QmpClient.h"

#include <QDebug>
#include <QPointer>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

static const char kConsoleIface[] = "org.qemu.Display1.Console";
static const char kKeyboardIface[] = "org.qemu.Display1.Keyboard";
static const char kMouseIface[] = "org.qemu.Display1.Mouse";
static const char kListenerPath[] = "/org/qemu/Display1/Listener";

// The subset of the listener interfaces we implement. QEMU asks for the
// "Interfaces" property to discover the optional ones.
static const char kListenerXml[] = R"XML(
<node>
  <interface name="org.qemu.Display1.Listener">
    <method name="Scanout">
      <arg type="u" name="width" direction="in"/>
      <arg type="u" name="height" direction="in"/>
      <arg type="u" name="stride" direction="in"/>
      <arg type="u" name="pixman_format" direction="in"/>
      <arg type="ay" name="data" direction="in"/>
    </method>
    <method name="Update">
      <arg type="i" name="x" direction="in"/>
      <arg type="i" name="y" direction="in"/>
      <arg type="i" name="width" direction="in"/>
      <arg type="i" name="height" direction="in"/>
      <arg type="u" name="stride" direction="in"/>
      <arg type="u" name="pixman_format" direction="in"/>
      <arg type="ay" name="data" direction="in"/>
    </method>
    <method name="ScanoutDMABUF">
      <arg type="h" name="dmabuf" direction="in"/>
      <arg type="u" name="width" direction="in"/>
      <arg type="u" name="height" direction="in"/>
      <arg type="u" name="stride" direction="in"/>
      <arg type="u" name="fourcc" direction="in"/>
      <arg type="t" name="modifier" direction="in"/>
      <arg type="b" name="y0_top" direction="in"/>
    </method>
    <method name="UpdateDMABUF">
      <arg type="i" name="x" direction="in"/>
      <arg type="i" name="y" direction="in"/>
      <arg type="i" name="width" direction="in"/>
      <arg type="i" name="height" direction="in"/>
    </method>
    <method name="Disable"/>
    <method name="MouseSet">
      <arg type="i" name="x" direction="in"/>
      <arg type="i" name="y" direction="in"/>
      <arg type="i" name="on" direction="in"/>
    </method>
    <method name="CursorDefine">
      <arg type="i" name="width" direction="in"/>
      <arg type="i" name="height" direction="in"/>
      <arg type="i" name="hot_x" direction="in"/>
      <arg type="i" name="hot_y" direction="in"/>
      <arg type="ay" name="data" direction="in"/>
    </method>
    <property name="Interfaces" type="as" access="read"/>
  </interface>
  <interface name="org.qemu.Display1.Listener.Unix.ScanoutDMABUF2">
    <method name="ScanoutDMABUF2">
      <arg type="ah" name="dmabuf" direction="in"/>
      <arg type="u" name="x" direction="in"/>
      <arg type="u" name="y" direction="in"/>
      <arg type="u" name="width" direction="in"/>
      <arg type="u" name="height" direction="in"/>
      <arg type="au" name="offset" direction="in"/>
      <arg type="au" name="stride" direction="in"/>
      <arg type="u" name="num_planes" direction="in"/>
      <arg type="u" name="fourcc" direction="in"/>
      <arg type="u" name="backing_width" direction="in"/>
      <arg type="u" name="backing_height" direction="in"/>
      <arg type="t" name="modifier" direction="in"/>
      <arg type="b" name="y0_top" direction="in"/>
    </method>
  </interface>
</node>
)XML";

void DmabufScanout::closeFds()
{
    for (DmabufPlane &p : planes) {
        if (p.fd >= 0)
            ::close(p.fd);
        p.fd = -1;
    }
}

// ---------------------------------------------------------------------------
// C callbacks. Every async call uses m_cancel, so after stop() they see
// G_IO_ERROR_CANCELLED and never touch a dead object.

static bool cancelled(GError *err)
{
    return err && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED);
}

static void listenerMethodCall(GDBusConnection *, const gchar *, const gchar *, const gchar *,
                               const gchar *method, GVariant *params, GDBusMethodInvocation *inv,
                               gpointer user_data)
{
    static_cast<DBusDisplay *>(user_data)->handleListenerCall(method, params, inv);
}

static GVariant *listenerGetProperty(GDBusConnection *, const gchar *, const gchar *, const gchar *iface,
                                     const gchar *prop, GError **, gpointer)
{
    if (g_strcmp0(iface, "org.qemu.Display1.Listener") == 0 && g_strcmp0(prop, "Interfaces") == 0) {
        const gchar *ifaces[] = {"org.qemu.Display1.Listener.Unix.ScanoutDMABUF2", nullptr};
        return g_variant_new_strv(ifaces, -1);
    }
    return nullptr;
}

static const GDBusInterfaceVTable kListenerVTable = {listenerMethodCall, listenerGetProperty, nullptr, {nullptr}};

static void onBusReadyCb(GObject *, GAsyncResult *res, gpointer user_data)
{
    GError *err = nullptr;
    GDBusConnection *conn = g_dbus_connection_new_finish(res, &err);
    if (!conn) {
        if (!cancelled(err))
            static_cast<DBusDisplay *>(user_data)->fail(QStringLiteral("D-Bus handshake with QEMU failed: %1")
                                                            .arg(QString::fromUtf8(err->message)));
        g_error_free(err);
        return;
    }
    static_cast<DBusDisplay *>(user_data)->onBusConnection(conn);
}

static void onListenerReadyCb(GObject *, GAsyncResult *res, gpointer user_data)
{
    GError *err = nullptr;
    GDBusConnection *conn = g_dbus_connection_new_finish(res, &err);
    if (!conn) {
        if (!cancelled(err))
            static_cast<DBusDisplay *>(user_data)->fail(QStringLiteral("Display listener handshake failed: %1")
                                                            .arg(QString::fromUtf8(err->message)));
        g_error_free(err);
        return;
    }
    static_cast<DBusDisplay *>(user_data)->onListenerConnection(conn);
}

static void onConsoleIdsCb(GObject *src, GAsyncResult *res, gpointer user_data)
{
    GError *err = nullptr;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);
    if (!reply) {
        if (!cancelled(err))
            static_cast<DBusDisplay *>(user_data)->fail(QStringLiteral("Can't query QEMU consoles: %1")
                                                            .arg(QString::fromUtf8(err->message)));
        g_error_free(err);
        return;
    }
    static_cast<DBusDisplay *>(user_data)->onConsoleIds(reply);
    g_variant_unref(reply);
}

static void onMouseAbsCb(GObject *src, GAsyncResult *res, gpointer user_data)
{
    GError *err = nullptr;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);
    if (!reply) {
        g_error_free(err);
        return;
    }
    GVariant *v = nullptr;
    g_variant_get(reply, "(v)", &v);
    if (v && g_variant_is_of_type(v, G_VARIANT_TYPE_BOOLEAN))
        static_cast<DBusDisplay *>(user_data)->onMouseAbsolute(g_variant_get_boolean(v));
    if (v)
        g_variant_unref(v);
    g_variant_unref(reply);
}

static void onRegisterCb(GObject *src, GAsyncResult *res, gpointer user_data)
{
    GError *err = nullptr;
    GVariant *reply = g_dbus_connection_call_with_unix_fd_list_finish(G_DBUS_CONNECTION(src), nullptr, res, &err);
    if (!reply) {
        if (!cancelled(err))
            static_cast<DBusDisplay *>(user_data)->fail(QStringLiteral("RegisterListener failed: %1")
                                                            .arg(QString::fromUtf8(err->message)));
        g_error_free(err);
        return;
    }
    g_variant_unref(reply);
    static_cast<DBusDisplay *>(user_data)->onRegistered();
}

static void onPropsChangedCb(GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *,
                             GVariant *params, gpointer user_data)
{
    const gchar *iface = nullptr;
    GVariant *changed = nullptr;
    g_variant_get(params, "(&s@a{sv}@as)", &iface, &changed, nullptr);
    if (g_strcmp0(iface, kMouseIface) == 0) {
        gboolean abs = FALSE;
        if (g_variant_lookup(changed, "IsAbsolute", "b", &abs))
            static_cast<DBusDisplay *>(user_data)->onMouseAbsolute(abs);
    }
    g_variant_unref(changed);
}

static void onClosedCb(GDBusConnection *, gboolean, GError *, gpointer user_data)
{
    static_cast<DBusDisplay *>(user_data)->onClosed();
}

static void ignoreReplyCb(GObject *src, GAsyncResult *res, gpointer)
{
    GError *err = nullptr;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);
    if (reply)
        g_variant_unref(reply);
    if (err)
        g_error_free(err);
}

// ---------------------------------------------------------------------------

DBusDisplay::DBusDisplay(QmpClient *qmp, QObject *parent)
    : QObject(parent)
    , m_qmp(qmp)
{
    GlibIntegration::ensure();
}

DBusDisplay::~DBusDisplay()
{
    stop();
}


void DBusDisplay::start()
{
    stop();
    m_failed = false;
    m_cancel = g_cancellable_new();

    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) {
        fail(QStringLiteral("socketpair() failed"));
        return;
    }

    // Hand one end to QEMU: getfd (SCM_RIGHTS) + add_client.
    QPointer<DBusDisplay> self(this);
    m_qmp->executeWithFd(QStringLiteral("getfd"), {{QStringLiteral("fdname"), QStringLiteral("bento-dbus")}}, sv[1],
                         [self, qmp = m_qmp](const QJsonValue &, const QString &err) {
                             if (!self)
                                 return;
                             if (!err.isEmpty()) {
                                 self->fail(QStringLiteral("QMP getfd failed: %1").arg(err));
                                 return;
                             }
                             qmp->execute(QStringLiteral("add_client"),
                                          {{QStringLiteral("protocol"), QStringLiteral("@dbus-display")},
                                           {QStringLiteral("fdname"), QStringLiteral("bento-dbus")}},
                                          [self](const QJsonValue &, const QString &err) {
                                              if (self && !err.isEmpty())
                                                  self->fail(QStringLiteral("QEMU refused the display connection: %1")
                                                                 .arg(err));
                                          });
                         });
    ::close(sv[1]);

    GError *err = nullptr;
    GSocket *sock = g_socket_new_from_fd(sv[0], &err);
    if (!sock) {
        ::close(sv[0]);
        fail(QString::fromUtf8(err->message));
        g_error_free(err);
        return;
    }
    GSocketConnection *sc = g_socket_connection_factory_create_connection(sock);
    g_object_unref(sock);
    // QEMU is the auth server on add_client connections.
    g_dbus_connection_new(G_IO_STREAM(sc), nullptr, G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT, nullptr,
                          m_cancel, onBusReadyCb, this);
    g_object_unref(sc);
}

void DBusDisplay::stop()
{
    if (m_cancel) {
        g_cancellable_cancel(m_cancel);
        g_object_unref(m_cancel);
        m_cancel = nullptr;
    }
    ackFrame();
    if (m_listenerConn) {
        for (unsigned id : std::as_const(m_registrations))
            g_dbus_connection_unregister_object(m_listenerConn, id);
        m_registrations.clear();
        g_signal_handlers_disconnect_by_data(m_listenerConn, this);
        g_dbus_connection_close(m_listenerConn, nullptr, nullptr, nullptr);
        g_object_unref(m_listenerConn);
        m_listenerConn = nullptr;
    }
    if (m_conn) {
        if (m_propsSub)
            g_dbus_connection_signal_unsubscribe(m_conn, m_propsSub);
        m_propsSub = 0;
        g_signal_handlers_disconnect_by_data(m_conn, this);
        g_dbus_connection_close(m_conn, nullptr, nullptr, nullptr);
        g_object_unref(m_conn);
        m_conn = nullptr;
    }
    m_consolePath.clear();
    m_registered = false;
    m_listenerReady = false;
}

void DBusDisplay::fail(const QString &error)
{
    if (m_failed)
        return;
    m_failed = true;
    qWarning() << "dbus display:" << error;
    stop();
    Q_EMIT failed(error);
}

void DBusDisplay::onClosed()
{
    const bool wasConnected = m_listenerReady;
    stop();
    if (wasConnected)
        Q_EMIT disconnected();
}

void DBusDisplay::onBusConnection(GDBusConnection *conn)
{
    m_conn = conn;
    g_signal_connect(conn, "closed", G_CALLBACK(onClosedCb), this);
    g_dbus_connection_call(conn, nullptr, "/org/qemu/Display1/VM", "org.freedesktop.DBus.Properties", "Get",
                           g_variant_new("(ss)", "org.qemu.Display1.VM", "ConsoleIDs"), G_VARIANT_TYPE("(v)"),
                           G_DBUS_CALL_FLAGS_NONE, 10000, m_cancel, onConsoleIdsCb, this);
}

void DBusDisplay::onConsoleIds(GVariant *reply)
{
    GVariant *v = nullptr;
    g_variant_get(reply, "(v)", &v);
    gsize n = 0;
    const guint32 *ids = v ? static_cast<const guint32 *>(g_variant_get_fixed_array(v, &n, sizeof(guint32))) : nullptr;
    const guint32 id = (ids && n > 0) ? ids[0] : 0;
    if (v)
        g_variant_unref(v);
    m_consolePath = QByteArray("/org/qemu/Display1/Console_") + QByteArray::number(id);

    m_propsSub = g_dbus_connection_signal_subscribe(m_conn, nullptr, "org.freedesktop.DBus.Properties",
                                                    "PropertiesChanged", m_consolePath.constData(), nullptr,
                                                    G_DBUS_SIGNAL_FLAGS_NONE, onPropsChangedCb, this, nullptr);
    g_dbus_connection_call(m_conn, nullptr, m_consolePath.constData(), "org.freedesktop.DBus.Properties", "Get",
                           g_variant_new("(ss)", kMouseIface, "IsAbsolute"), G_VARIANT_TYPE("(v)"),
                           G_DBUS_CALL_FLAGS_NONE, 10000, m_cancel, onMouseAbsCb, this);
    registerListener();
}

void DBusDisplay::registerListener()
{
    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) {
        fail(QStringLiteral("socketpair() failed"));
        return;
    }
    GError *err = nullptr;
    GSocket *sock = g_socket_new_from_fd(sv[0], &err);
    if (!sock) {
        ::close(sv[0]);
        ::close(sv[1]);
        fail(QString::fromUtf8(err->message));
        g_error_free(err);
        return;
    }
    GSocketConnection *sc = g_socket_connection_factory_create_connection(sock);
    g_object_unref(sock);
    // QEMU authenticates as server here too; it does so synchronously right
    // after replying to RegisterListener, so our side must already be running.
    g_dbus_connection_new(G_IO_STREAM(sc), nullptr,
                          GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT
                                               | G_DBUS_CONNECTION_FLAGS_DELAY_MESSAGE_PROCESSING),
                          nullptr, m_cancel, onListenerReadyCb, this);
    g_object_unref(sc);

    GUnixFDList *fds = g_unix_fd_list_new_from_array(&sv[1], 1);   // takes ownership of sv[1]
    g_dbus_connection_call_with_unix_fd_list(m_conn, nullptr, m_consolePath.constData(), kConsoleIface,
                                             "RegisterListener", g_variant_new("(h)", 0), nullptr,
                                             G_DBUS_CALL_FLAGS_NONE, -1, fds, m_cancel, onRegisterCb, this);
    g_object_unref(fds);
}

void DBusDisplay::onListenerConnection(GDBusConnection *conn)
{
    m_listenerConn = conn;
    static GDBusNodeInfo *node = [] {
        GError *err = nullptr;
        GDBusNodeInfo *n = g_dbus_node_info_new_for_xml(kListenerXml, &err);
        if (!n) {
            qCritical() << "listener XML:" << err->message;
            g_error_free(err);
        }
        return n;
    }();
    if (!node) {
        fail(QStringLiteral("internal error: listener interface"));
        return;
    }
    for (GDBusInterfaceInfo **iface = node->interfaces; iface && *iface; ++iface) {
        GError *err = nullptr;
        const unsigned id = g_dbus_connection_register_object(conn, kListenerPath, *iface, &kListenerVTable, this,
                                                              nullptr, &err);
        if (!id) {
            fail(QString::fromUtf8(err->message));
            g_error_free(err);
            return;
        }
        m_registrations << id;
    }
    g_signal_connect(conn, "closed", G_CALLBACK(onClosedCb), this);
    g_dbus_connection_start_message_processing(conn);
    m_listenerReady = true;
    maybeConnected();
}

void DBusDisplay::onRegistered()
{
    m_registered = true;
    maybeConnected();
}

void DBusDisplay::maybeConnected()
{
    if (m_registered && m_listenerReady)
        Q_EMIT connected();
}

void DBusDisplay::onMouseAbsolute(bool abs)
{
    if (m_mouseAbsolute == abs)
        return;
    m_mouseAbsolute = abs;
    Q_EMIT mouseModeChanged(abs);
}

// ---------------------------------------------------------------------------
// Listener method dispatch

static QImage::Format pixmanToQImage(uint32_t fmt)
{
    switch (fmt) {
    case 0x20020888:   // x8r8g8b8
    case 0x20028888:   // a8r8g8b8
        return QImage::Format_RGB32;
    case 0x20030888:   // x8b8g8r8
    case 0x20038888:   // a8b8g8r8
        return QImage::Format_RGBX8888;
    case 0x10020565:   // r5g6b5
        return QImage::Format_RGB16;
    case 0x18020888:   // r8g8b8
        return QImage::Format_BGR888;
    default:
        return QImage::Format_RGB32;
    }
}

static QImage copyImage(int w, int h, uint32_t stride, uint32_t fmt, GVariant *data)
{
    gsize n = 0;
    const auto *src = static_cast<const uchar *>(g_variant_get_fixed_array(data, &n, 1));
    QImage img(qMax(1, w), qMax(1, h), pixmanToQImage(fmt));
    if (!src || img.isNull())
        return img;
    const qsizetype rowBytes = qMin<qsizetype>(img.bytesPerLine(), stride);
    for (int y = 0; y < h; ++y) {
        const gsize off = gsize(y) * stride;
        if (off + gsize(rowBytes) > n)
            break;
        std::memcpy(img.scanLine(y), src + off, size_t(rowBytes));
    }
    return img;
}

void DBusDisplay::handleListenerCall(const char *method, GVariant *params, GDBusMethodInvocation *inv)
{
    GDBusMessage *msg = g_dbus_method_invocation_get_message(inv);
    GUnixFDList *fdList = g_dbus_message_get_unix_fd_list(msg);

    if (std::strcmp(method, "UpdateDMABUF") == 0) {
        gint32 x, y, w, h;
        g_variant_get(params, "(iiii)", &x, &y, &w, &h);
        ackFrame();   // never hold more than one
        m_pendingUpdate = inv;
        Q_EMIT updateDmabuf(QRect(x, y, w, h));
        return;   // answered from ackFrame()
    }

    if (std::strcmp(method, "ScanoutDMABUF") == 0) {
        gint32 handle;
        guint32 w, h, stride, fourcc;
        guint64 modifier;
        gboolean y0top;
        g_variant_get(params, "(huuuutb)", &handle, &w, &h, &stride, &fourcc, &modifier, &y0top);
        DmabufScanout s;
        GError *err = nullptr;
        const int fd = fdList ? g_unix_fd_list_get(fdList, handle, &err) : -1;
        if (err)
            g_error_free(err);
        if (fd >= 0) {
            s.planes.append({fd, 0, stride});
            s.fourcc = fourcc;
            s.modifier = modifier;
            s.backingWidth = w;
            s.backingHeight = h;
            s.visible = QRect(0, 0, int(w), int(h));
            s.y0Top = y0top;
            ackFrame();
            Q_EMIT scanoutDmabuf(s);
        }
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }

    if (std::strcmp(method, "ScanoutDMABUF2") == 0) {
        GVariant *fdArr = nullptr, *offArr = nullptr, *strideArr = nullptr;
        guint32 x, y, w, h, nplanes, fourcc, bw, bh;
        guint64 modifier;
        gboolean y0top;
        g_variant_get(params, "(@ahuuuu@au@auuuuutb)", &fdArr, &x, &y, &w, &h, &offArr, &strideArr, &nplanes,
                      &fourcc, &bw, &bh, &modifier, &y0top);
        gsize nfd = 0, noff = 0, nstr = 0;
        const auto *handles = static_cast<const gint32 *>(g_variant_get_fixed_array(fdArr, &nfd, sizeof(gint32)));
        const auto *offs = static_cast<const guint32 *>(g_variant_get_fixed_array(offArr, &noff, sizeof(guint32)));
        const auto *strides = static_cast<const guint32 *>(g_variant_get_fixed_array(strideArr, &nstr, sizeof(guint32)));
        DmabufScanout s;
        s.fourcc = fourcc;
        s.modifier = modifier;
        s.backingWidth = bw;
        s.backingHeight = bh;
        s.visible = QRect(int(x), int(y), int(w), int(h));
        s.y0Top = y0top;
        for (guint32 i = 0; i < nplanes && i < 4; ++i) {
            // Planes may share one fd; reuse the last one if fewer fds than planes.
            const gsize hi = qMin<gsize>(i, nfd ? nfd - 1 : 0);
            int fd = -1;
            if (fdList && nfd) {
                GError *err = nullptr;
                fd = g_unix_fd_list_get(fdList, handles[hi], &err);
                if (err)
                    g_error_free(err);
            }
            s.planes.append({fd, i < noff ? offs[i] : 0, i < nstr ? strides[i] : 0});
        }
        g_variant_unref(fdArr);
        g_variant_unref(offArr);
        g_variant_unref(strideArr);
        ackFrame();
        if (!s.planes.isEmpty() && s.planes[0].fd >= 0)
            Q_EMIT scanoutDmabuf(s);
        else
            s.closeFds();
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }

    if (std::strcmp(method, "Scanout") == 0) {
        guint32 w, h, stride, fmt;
        GVariant *data = nullptr;
        g_variant_get(params, "(uuuu@ay)", &w, &h, &stride, &fmt, &data);
        const QImage img = copyImage(int(w), int(h), stride, fmt, data);
        g_variant_unref(data);
        ackFrame();
        Q_EMIT scanoutImage(img);
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }

    if (std::strcmp(method, "Update") == 0) {
        gint32 x, y, w, h;
        guint32 stride, fmt;
        GVariant *data = nullptr;
        g_variant_get(params, "(iiiiuu@ay)", &x, &y, &w, &h, &stride, &fmt, &data);
        const QImage patch = copyImage(w, h, stride, fmt, data);
        g_variant_unref(data);
        Q_EMIT updateImage(QRect(x, y, w, h), patch);
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }

    if (std::strcmp(method, "Disable") == 0) {
        ackFrame();
        Q_EMIT disabled();
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }

    if (std::strcmp(method, "MouseSet") == 0) {
        gint32 x, y, on;
        g_variant_get(params, "(iii)", &x, &y, &on);
        Q_EMIT mouseSet(x, y, on != 0);
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }

    if (std::strcmp(method, "CursorDefine") == 0) {
        gint32 w, h, hx, hy;
        GVariant *data = nullptr;
        g_variant_get(params, "(iiii@ay)", &w, &h, &hx, &hy, &data);
        gsize n = 0;
        const auto *src = static_cast<const uchar *>(g_variant_get_fixed_array(data, &n, 1));
        QImage cursor;
        if (w > 0 && h > 0 && src && n >= gsize(w) * gsize(h) * 4)
            cursor = QImage(src, w, h, w * 4, QImage::Format_ARGB32).copy();
        g_variant_unref(data);
        Q_EMIT cursorDefined(cursor, hx, hy);
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }

    g_dbus_method_invocation_return_dbus_error(inv, "org.freedesktop.DBus.Error.UnknownMethod", method);
}

void DBusDisplay::ackFrame()
{
    if (!m_pendingUpdate)
        return;
    g_dbus_method_invocation_return_value(m_pendingUpdate, nullptr);
    m_pendingUpdate = nullptr;
}

// ---------------------------------------------------------------------------
// Input

void DBusDisplay::callConsole(const char *iface, const char *method, GVariant *params)
{
    if (!m_conn || m_consolePath.isEmpty()) {
        g_variant_unref(g_variant_ref_sink(params));
        return;
    }
    g_dbus_connection_call(m_conn, nullptr, m_consolePath.constData(), iface, method, params, nullptr,
                           G_DBUS_CALL_FLAGS_NONE, -1, nullptr, ignoreReplyCb, nullptr);
}

void DBusDisplay::keyPress(uint32_t qnum) { callConsole(kKeyboardIface, "Press", g_variant_new("(u)", qnum)); }
void DBusDisplay::keyRelease(uint32_t qnum) { callConsole(kKeyboardIface, "Release", g_variant_new("(u)", qnum)); }
void DBusDisplay::mousePress(uint32_t b) { callConsole(kMouseIface, "Press", g_variant_new("(u)", b)); }
void DBusDisplay::mouseRelease(uint32_t b) { callConsole(kMouseIface, "Release", g_variant_new("(u)", b)); }
void DBusDisplay::mouseMoveAbs(uint32_t x, uint32_t y) { callConsole(kMouseIface, "SetAbsPosition", g_variant_new("(uu)", x, y)); }
void DBusDisplay::mouseMoveRel(int dx, int dy) { callConsole(kMouseIface, "RelMotion", g_variant_new("(ii)", dx, dy)); }

void DBusDisplay::setUiInfo(uint32_t width, uint32_t height, uint16_t widthMm, uint16_t heightMm)
{
    callConsole(kConsoleIface, "SetUIInfo", g_variant_new("(qqiiuu)", widthMm, heightMm, 0, 0, width, height));
}
