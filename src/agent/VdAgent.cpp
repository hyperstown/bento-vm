// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VdAgent.h"

#include <QBuffer>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QGuiApplication>
#include <QImage>
#include <QLocalSocket>
#include <QMimeData>
#include <QTimer>

#include <cstring>
#include <spice/vd_agent.h>

static constexpr qsizetype kChunkMax = VD_AGENT_MAX_DATA_SIZE;   // guest rejects larger chunks
static constexpr qsizetype kXferBlock = 64 * 1024;
static constexpr qint64 kWriteHighWater = 1024 * 1024;
static constexpr qsizetype kMaxClipboard = 128 * 1024 * 1024;

template<typename T>
static QByteArray pod(const T &v)
{
    return QByteArray(reinterpret_cast<const char *>(&v), sizeof(T));
}

static QByteArray u32(quint32 v) { return pod(v); }

static quint32 readU32(const QByteArray &d, qsizetype off)
{
    quint32 v = 0;
    if (off + 4 <= d.size())
        std::memcpy(&v, d.constData() + off, 4);
    return v;
}

VdAgent::VdAgent(QObject *parent)
    : QObject(parent)
    , m_sock(new QLocalSocket(this))
    , m_retry(new QTimer(this))
{
    m_retry->setSingleShot(true);
    m_retry->setInterval(1000);
    connect(m_retry, &QTimer::timeout, this, &VdAgent::tryConnect);
    connect(m_sock, &QLocalSocket::connected, this, &VdAgent::onConnected);
    connect(m_sock, &QLocalSocket::disconnected, this, &VdAgent::onDisconnected);
    connect(m_sock, &QLocalSocket::readyRead, this, &VdAgent::onReadyRead);
    connect(m_sock, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (m_wanted && m_sock->state() == QLocalSocket::UnconnectedState)
            m_retry->start();
    });
    connect(m_sock, &QLocalSocket::bytesWritten, this, &VdAgent::pumpTransfers);
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &VdAgent::onHostClipboardChanged);
}

VdAgent::~VdAgent()
{
    m_wanted = false;
    for (Xfer *x : std::as_const(m_xfers)) {
        delete x->file;
        delete x;
    }
}

void VdAgent::connectTo(const QString &socketPath)
{
    m_path = socketPath;
    m_wanted = true;
    tryConnect();
}

void VdAgent::disconnectAgent()
{
    m_wanted = false;
    m_retry->stop();
    m_sock->abort();
    onDisconnected();
}

void VdAgent::tryConnect()
{
    if (!m_wanted || m_sock->state() != QLocalSocket::UnconnectedState)
        return;
    m_sock->connectToServer(m_path);
}

void VdAgent::onConnected()
{
    m_in.clear();
    m_msg.clear();
    m_chunkRemaining = 0;
    // Ask the guest agent to announce itself. If it isn't running yet the data
    // waits in the virtio-serial queue until it opens the port.
    sendCaps(true);
}

void VdAgent::onDisconnected()
{
    setGuestConnected(false);
    m_hostGrabbed = false;
    const auto xfers = m_xfers;
    for (Xfer *x : xfers)
        finishXfer(x, false, QStringLiteral("Connection to the VM was lost"));
    if (m_wanted)
        m_retry->start();
}

void VdAgent::setGuestConnected(bool on)
{
    if (m_guestConnected == on)
        return;
    m_guestConnected = on;
    if (!on)
        m_guestCaps.clear();
    Q_EMIT guestConnectedChanged(on);
}

bool VdAgent::guestSupportsClipboard() const
{
    return m_guestConnected && !m_guestCaps.isEmpty()
        && VD_AGENT_HAS_CAPABILITY(m_guestCaps.constData(), m_guestCaps.size(), VD_AGENT_CAP_CLIPBOARD_BY_DEMAND);
}

bool VdAgent::guestSupportsFileTransfer() const
{
    if (!m_guestConnected)
        return false;
    if (m_guestCaps.isEmpty())
        return true;
    return !VD_AGENT_HAS_CAPABILITY(m_guestCaps.constData(), m_guestCaps.size(), VD_AGENT_CAP_FILE_XFER_DISABLED);
}

void VdAgent::setClipboardMode(const QString &mode)
{
    m_clipboardMode = mode;
    if (!hostToGuest() && m_hostGrabbed && m_guestConnected) {
        sendMessage(VD_AGENT_CLIPBOARD_RELEASE, {});
        m_hostGrabbed = false;
    } else if (hostToGuest()) {
        onHostClipboardChanged();
    }
}

bool VdAgent::hostToGuest() const
{
    return m_clipboardMode == QLatin1String("both") || m_clipboardMode == QLatin1String("host-to-guest");
}

bool VdAgent::guestToHost() const
{
    return m_clipboardMode == QLatin1String("both") || m_clipboardMode == QLatin1String("guest-to-host");
}

// ---------------------------------------------------------------------------
// Wire protocol

void VdAgent::sendMessage(quint32 type, const QByteArray &data)
{
    if (m_sock->state() != QLocalSocket::ConnectedState)
        return;
    VDAgentMessage hdr{};
    hdr.protocol = VD_AGENT_PROTOCOL;
    hdr.type = type;
    hdr.opaque = 0;
    hdr.size = quint32(data.size());
    const QByteArray msg = pod(hdr) + data;
    for (qsizetype off = 0; off < msg.size(); off += kChunkMax) {
        VDIChunkHeader ch{};
        ch.port = VDP_CLIENT_PORT;
        ch.size = quint32(qMin(kChunkMax, msg.size() - off));
        m_sock->write(pod(ch));
        m_sock->write(msg.constData() + off, ch.size);
    }
}

void VdAgent::sendCaps(bool request)
{
    quint32 caps = 0;
    VD_AGENT_SET_CAPABILITY(&caps, VD_AGENT_CAP_CLIPBOARD_BY_DEMAND);
    sendMessage(VD_AGENT_ANNOUNCE_CAPABILITIES, u32(request ? 1 : 0) + u32(caps));
}

void VdAgent::onReadyRead()
{
    m_in += m_sock->readAll();
    for (;;) {
        if (m_chunkRemaining == 0) {
            if (m_in.size() < qsizetype(sizeof(VDIChunkHeader)))
                break;
            VDIChunkHeader ch;
            std::memcpy(&ch, m_in.constData(), sizeof(ch));
            m_in.remove(0, sizeof(ch));
            m_chunkRemaining = ch.size;
            if (m_chunkRemaining == 0)
                continue;
        }
        const qsizetype take = qMin<qsizetype>(m_chunkRemaining, m_in.size());
        if (take == 0)
            break;
        m_msg.append(m_in.constData(), take);
        m_in.remove(0, take);
        m_chunkRemaining -= quint32(take);
        processMessages();
    }
}

void VdAgent::processMessages()
{
    while (m_msg.size() >= qsizetype(sizeof(VDAgentMessage))) {
        VDAgentMessage hdr;
        std::memcpy(&hdr, m_msg.constData(), sizeof(hdr));
        const qsizetype total = qsizetype(sizeof(VDAgentMessage)) + hdr.size;
        if (hdr.protocol != VD_AGENT_PROTOCOL || hdr.size > kMaxClipboard + 1024) {
            // Out of sync: drop everything and resync on the next message.
            m_msg.clear();
            return;
        }
        if (m_msg.size() < total)
            return;
        const QByteArray data = m_msg.mid(sizeof(VDAgentMessage), hdr.size);
        m_msg.remove(0, total);
        handleMessage(hdr.type, data);
    }
}

void VdAgent::handleMessage(quint32 type, const QByteArray &data)
{
    switch (type) {
    case VD_AGENT_ANNOUNCE_CAPABILITIES: {
        const quint32 request = readU32(data, 0);
        m_guestCaps.clear();
        for (qsizetype off = 4; off + 4 <= data.size(); off += 4)
            m_guestCaps << readU32(data, off);
        const bool wasConnected = m_guestConnected;
        setGuestConnected(true);
        if (request)
            sendCaps(false);
        if (!wasConnected) {
            m_hostGrabbed = false;
            onHostClipboardChanged();   // offer whatever the host currently has
        }
        break;
    }
    case VD_AGENT_CLIPBOARD_GRAB:
        handleGuestGrab(data);
        break;
    case VD_AGENT_CLIPBOARD_REQUEST:
        handleGuestRequest(data);
        break;
    case VD_AGENT_CLIPBOARD:
        handleGuestClipboard(data);
        break;
    case VD_AGENT_CLIPBOARD_RELEASE:
        break;
    case VD_AGENT_FILE_XFER_STATUS:
        handleXferStatus(data);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Clipboard

static bool guestUsesCrlf(const QList<quint32> &caps)
{
    return !caps.isEmpty() && VD_AGENT_HAS_CAPABILITY(caps.constData(), caps.size(), VD_AGENT_CAP_GUEST_LINEEND_CRLF);
}

void VdAgent::onHostClipboardChanged()
{
    if (!m_guestConnected || !hostToGuest() || m_settingHostClipboard)
        return;
    QClipboard *cb = QGuiApplication::clipboard();
    const QMimeData *md = cb->mimeData();
    if (md && md->hasText() && !m_lastGuestText.isEmpty() && md->text() == m_lastGuestText)
        return;   // that's the guest's own data coming back
    if (md && md->hasImage() && m_lastGuestImageHash) {
        const QImage img = qvariant_cast<QImage>(md->imageData());
        if (qHashBits(img.constBits(), size_t(img.sizeInBytes())) == m_lastGuestImageHash)
            return;
    }
    QByteArray types;
    if (md && md->hasText())
        types += u32(VD_AGENT_CLIPBOARD_UTF8_TEXT);
    if (md && md->hasImage())
        types += u32(VD_AGENT_CLIPBOARD_IMAGE_PNG);
    if (types.isEmpty()) {
        if (m_hostGrabbed)
            sendMessage(VD_AGENT_CLIPBOARD_RELEASE, {});
        m_hostGrabbed = false;
        return;
    }
    sendMessage(VD_AGENT_CLIPBOARD_GRAB, types);
    m_hostGrabbed = true;
}

void VdAgent::handleGuestRequest(const QByteArray &data)
{
    const quint32 type = readU32(data, 0);
    QByteArray payload;
    if (hostToGuest()) {
        const QMimeData *md = QGuiApplication::clipboard()->mimeData();
        if (md && type == VD_AGENT_CLIPBOARD_UTF8_TEXT && md->hasText()) {
            QString text = md->text();
            if (guestUsesCrlf(m_guestCaps)) {
                text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
                text.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
            }
            payload = text.toUtf8();
        } else if (md && type == VD_AGENT_CLIPBOARD_IMAGE_PNG && md->hasImage()) {
            const QImage img = qvariant_cast<QImage>(md->imageData());
            QBuffer buf(&payload);
            buf.open(QIODevice::WriteOnly);
            img.save(&buf, "PNG");
        }
    }
    sendMessage(VD_AGENT_CLIPBOARD, u32(type) + payload.left(kMaxClipboard));
}

void VdAgent::handleGuestGrab(const QByteArray &data)
{
    m_hostGrabbed = false;   // guest owns the clipboard now
    if (!guestToHost())
        return;
    QList<quint32> types;
    for (qsizetype off = 0; off + 4 <= data.size(); off += 4)
        types << readU32(data, off);
    static const quint32 preferred[] = {VD_AGENT_CLIPBOARD_UTF8_TEXT, VD_AGENT_CLIPBOARD_IMAGE_PNG,
                                        VD_AGENT_CLIPBOARD_IMAGE_BMP, VD_AGENT_CLIPBOARD_IMAGE_JPG,
                                        VD_AGENT_CLIPBOARD_IMAGE_TIFF};
    for (quint32 t : preferred) {
        if (types.contains(t)) {
            // Fetch eagerly; Qt has no lazy clipboard provider API.
            sendMessage(VD_AGENT_CLIPBOARD_REQUEST, u32(t));
            return;
        }
    }
}

void VdAgent::handleGuestClipboard(const QByteArray &data)
{
    if (!guestToHost() || data.size() < 4)
        return;
    const quint32 type = readU32(data, 0);
    const QByteArray payload = data.mid(4);
    QClipboard *cb = QGuiApplication::clipboard();
    m_settingHostClipboard = true;
    if (type == VD_AGENT_CLIPBOARD_UTF8_TEXT) {
        QString text = QString::fromUtf8(payload);
        if (text.endsWith(QChar(0)))
            text.chop(1);
        if (guestUsesCrlf(m_guestCaps))
            text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        m_lastGuestText = text;
        m_lastGuestImageHash = 0;
        cb->setText(text);
    } else if (type != VD_AGENT_CLIPBOARD_NONE) {
        QImage img;
        if (img.loadFromData(payload)) {
            m_lastGuestText.clear();
            m_lastGuestImageHash = qHashBits(img.constBits(), size_t(img.sizeInBytes()));
            cb->setImage(img);
        }
    }
    m_settingHostClipboard = false;
}

// ---------------------------------------------------------------------------
// File transfer

int VdAgent::sendFiles(const QStringList &paths)
{
    if (!m_fileTransferEnabled) {
        Q_EMIT notice(QStringLiteral("File transfer is disabled for this VM."));
        return 0;
    }
    if (!m_guestConnected) {
        Q_EMIT notice(QStringLiteral("The guest agent isn't running. Install spice-vdagent in the VM to drop files into it."));
        return 0;
    }
    if (!guestSupportsFileTransfer()) {
        Q_EMIT notice(QStringLiteral("File transfer is disabled inside the guest."));
        return 0;
    }
    int queued = 0;
    for (const QString &path : paths) {
        const QFileInfo fi(path);
        if (fi.isDir()) {
            Q_EMIT notice(QStringLiteral("Folders can't be dropped yet, skipped \"%1\". Compress it first.").arg(fi.fileName()));
            continue;
        }
        if (!fi.isFile() || !fi.isReadable()) {
            Q_EMIT notice(QStringLiteral("Can't read \"%1\".").arg(fi.fileName()));
            continue;
        }
        auto *x = new Xfer;
        x->id = m_nextXferId++;
        x->name = fi.fileName();
        x->size = fi.size();
        x->file = new QFile(fi.absoluteFilePath());
        if (!x->file->open(QIODevice::ReadOnly)) {
            Q_EMIT notice(QStringLiteral("Can't open \"%1\": %2").arg(fi.fileName(), x->file->errorString()));
            delete x->file;
            delete x;
            continue;
        }
        if (m_xfers.isEmpty()) {
            m_batchTotal = 0;
            m_batchSent = 0;
        }
        m_batchTotal += x->size;
        m_xfers << x;

        QString name = x->name;
        name.remove(QLatin1Char('\n')).remove(QLatin1Char('\r'));
        const QByteArray keyfile = QStringLiteral("[vdagent-file-xfer]\nname=%1\nsize=%2\n")
                                       .arg(name).arg(x->size).toUtf8() + '\0';
        sendMessage(VD_AGENT_FILE_XFER_START, u32(x->id) + keyfile);
        ++queued;
    }
    if (queued)
        emitProgress();
    return queued;
}

void VdAgent::cancelTransfers()
{
    const auto xfers = m_xfers;
    for (Xfer *x : xfers) {
        VDAgentFileXferStatusMessage st{};
        st.id = x->id;
        st.result = VD_AGENT_FILE_XFER_STATUS_CANCELLED;
        sendMessage(VD_AGENT_FILE_XFER_STATUS, pod(st));
        finishXfer(x, false, QStringLiteral("Cancelled"));
    }
}

void VdAgent::handleXferStatus(const QByteArray &data)
{
    if (data.size() < 8)
        return;
    const quint32 id = readU32(data, 0);
    const quint32 result = readU32(data, 4);
    Xfer *x = nullptr;
    for (Xfer *c : std::as_const(m_xfers))
        if (c->id == id)
            x = c;
    if (!x)
        return;
    switch (result) {
    case VD_AGENT_FILE_XFER_STATUS_CAN_SEND_DATA:
        x->canSend = true;
        if (x->size == 0)
            sendMessage(VD_AGENT_FILE_XFER_DATA, u32(x->id) + pod(quint64(0)));
        pumpTransfers();
        break;
    case VD_AGENT_FILE_XFER_STATUS_SUCCESS:
        finishXfer(x, true, QString());
        break;
    case VD_AGENT_FILE_XFER_STATUS_CANCELLED:
        finishXfer(x, false, QStringLiteral("Cancelled in the guest"));
        break;
    case VD_AGENT_FILE_XFER_STATUS_NOT_ENOUGH_SPACE:
        finishXfer(x, false, QStringLiteral("Not enough space in the guest"));
        break;
    case VD_AGENT_FILE_XFER_STATUS_SESSION_LOCKED:
        finishXfer(x, false, QStringLiteral("The guest session is locked"));
        break;
    case VD_AGENT_FILE_XFER_STATUS_VDAGENT_NOT_CONNECTED:
        finishXfer(x, false, QStringLiteral("Log in to the guest desktop first"));
        break;
    case VD_AGENT_FILE_XFER_STATUS_DISABLED:
        finishXfer(x, false, QStringLiteral("File transfer is disabled in the guest"));
        break;
    default:
        finishXfer(x, false, QStringLiteral("The guest reported an error"));
        break;
    }
}

void VdAgent::pumpTransfers()
{
    bool progressed = false;
    while (m_sock->state() == QLocalSocket::ConnectedState && m_sock->bytesToWrite() < kWriteHighWater) {
        Xfer *x = nullptr;
        for (Xfer *c : std::as_const(m_xfers)) {
            if (c->canSend && c->sent < c->size) {
                x = c;
                break;
            }
        }
        if (!x)
            break;
        const QByteArray block = x->file->read(kXferBlock);
        if (block.isEmpty()) {
            finishXfer(x, false, QStringLiteral("Read error: %1").arg(x->file->errorString()));
            continue;
        }
        VDAgentFileXferDataMessage hdr{};
        hdr.id = x->id;
        hdr.size = quint64(block.size());
        sendMessage(VD_AGENT_FILE_XFER_DATA, pod(hdr) + block);
        x->sent += block.size();
        m_batchSent += block.size();
        progressed = true;
    }
    if (progressed)
        emitProgress();
}

void VdAgent::finishXfer(Xfer *x, bool ok, const QString &message)
{
    m_xfers.removeOne(x);
    if (!ok)
        m_batchSent += x->size - x->sent;
    Q_EMIT transferFinished(x->name, ok, message);
    delete x->file;
    delete x;
    emitProgress();
}

void VdAgent::emitProgress()
{
    Q_EMIT transferProgress(m_batchSent, m_batchTotal, int(m_xfers.size()));
}
