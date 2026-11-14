// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "QmpClient.h"

#include <QDebug>
#include <QJsonDocument>
#include <QSocketNotifier>

#include <cerrno>
#include <utility>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

QmpClient::QmpClient(QObject *parent)
    : QObject(parent)
{
}

QmpClient::~QmpClient()
{
    closeSocket();
    for (Pending &p : m_queue)
        if (p.fd >= 0)
            ::close(p.fd);
}

bool QmpClient::connectTo(const QString &socketPath, QString *error)
{
    closeSocket();
    const QByteArray path = socketPath.toLocal8Bit();
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (size_t(path.size()) >= sizeof(addr.sun_path)) {
        if (error)
            *error = QStringLiteral("socket path too long");
        return false;
    }
    std::memcpy(addr.sun_path, path.constData(), size_t(path.size()));

    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        if (error)
            *error = QString::fromLocal8Bit(strerror(errno));
        return false;
    }
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        if (error)
            *error = QString::fromLocal8Bit(strerror(errno));
        ::close(fd);
        return false;
    }
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);

    m_fd = fd;
    m_ready = false;
    m_buf.clear();
    m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &QmpClient::onReadable);
    return true;
}

void QmpClient::closeSocket()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_ready = false;
}

void QmpClient::disconnectFromQemu()
{
    const bool was = m_fd >= 0;
    closeSocket();
    const auto callbacks = std::exchange(m_callbacks, {});
    for (const Callback &cb : callbacks)
        if (cb)
            cb({}, QStringLiteral("The connection to QEMU was lost; the VM may have stopped."));
    if (was)
        Q_EMIT disconnected();
}

void QmpClient::execute(const QString &command, const QJsonObject &args, Callback cb)
{
    enqueue(command, args, -1, std::move(cb));
}

void QmpClient::executeWithFd(const QString &command, const QJsonObject &args, int fd, Callback cb)
{
    const int dup = fd >= 0 ? ::fcntl(fd, F_DUPFD_CLOEXEC, 0) : -1;
    enqueue(command, args, dup, std::move(cb));
}

void QmpClient::enqueue(const QString &command, const QJsonObject &args, int fd, Callback cb)
{
    if (m_fd < 0) {
        if (fd >= 0)
            ::close(fd);
        if (cb)
            cb({}, QStringLiteral("BentoVM isn't connected to QEMU; the VM may not be running."));
        return;
    }
    const QString id = QString::number(m_nextId++);
    QJsonObject msg{{QStringLiteral("execute"), command}, {QStringLiteral("id"), id}};
    if (!args.isEmpty())
        msg[QStringLiteral("arguments")] = args;
    if (cb)
        m_callbacks.insert(id, std::move(cb));
    m_queue.append({QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n', fd});
    if (m_ready)
        flushQueue();
}

void QmpClient::flushQueue()
{
    while (!m_queue.isEmpty() && m_fd >= 0) {
        Pending p = m_queue.takeFirst();
        const bool ok = sendRaw(p.line, p.fd);
        if (p.fd >= 0)
            ::close(p.fd);
        if (!ok) {
            disconnectFromQemu();
            return;
        }
    }
}

bool QmpClient::sendRaw(const QByteArray &data, int fd)
{
    qsizetype off = 0;
    bool fdSent = fd < 0;
    while (off < data.size()) {
        iovec iov{const_cast<char *>(data.constData() + off), size_t(data.size() - off)};
        msghdr msg{};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        alignas(cmsghdr) char cbuf[CMSG_SPACE(sizeof(int))];
        if (!fdSent) {
            std::memset(cbuf, 0, sizeof(cbuf));
            msg.msg_control = cbuf;
            msg.msg_controllen = sizeof(cbuf);
            cmsghdr *cm = CMSG_FIRSTHDR(&msg);
            cm->cmsg_level = SOL_SOCKET;
            cm->cmsg_type = SCM_RIGHTS;
            cm->cmsg_len = CMSG_LEN(sizeof(int));
            std::memcpy(CMSG_DATA(cm), &fd, sizeof(int));
        }
        const ssize_t n = ::sendmsg(m_fd, &msg, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pollfd pfd{m_fd, POLLOUT, 0};
                if (::poll(&pfd, 1, 2000) <= 0)
                    return false;
                continue;
            }
            return false;
        }
        fdSent = true;
        off += n;
    }
    return true;
}

void QmpClient::onReadable()
{
    bool eof = false;
    char tmp[65536];
    for (;;) {
        const ssize_t n = ::recv(m_fd, tmp, sizeof(tmp), 0);
        if (n > 0) {
            m_buf.append(tmp, n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        eof = true;
        break;
    }
    qsizetype nl;
    while (m_fd >= 0 && (nl = m_buf.indexOf('\n')) >= 0) {
        const QByteArray line = m_buf.left(nl).trimmed();
        m_buf.remove(0, nl + 1);
        if (line.isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isObject())
            handleMessage(doc.object());
    }
    if (eof && m_fd >= 0)
        disconnectFromQemu();
}

void QmpClient::handleMessage(const QJsonObject &msg)
{
    if (msg.contains(QStringLiteral("QMP"))) {
        // Greeting: negotiate capabilities, then release queued commands.
        const QByteArray caps = R"({"execute":"qmp_capabilities","id":"caps"})" "\n";
        if (!sendRaw(caps, -1))
            disconnectFromQemu();
        return;
    }
    if (msg.contains(QStringLiteral("event"))) {
        Q_EMIT eventReceived(msg[QStringLiteral("event")].toString(), msg[QStringLiteral("data")].toObject());
        return;
    }
    const QString id = msg[QStringLiteral("id")].toString();
    if (id == QLatin1String("caps")) {
        m_ready = true;
        flushQueue();
        Q_EMIT ready();
        return;
    }
    const Callback cb = m_callbacks.take(id);
    if (!cb)
        return;
    if (msg.contains(QStringLiteral("error"))) {
        const QJsonObject e = msg[QStringLiteral("error")].toObject();
        QString desc = e[QStringLiteral("desc")].toString().trimmed();
        if (desc.isEmpty())
            desc = QStringLiteral("QEMU rejected the request (%1) without saying why.")
                       .arg(e[QStringLiteral("class")].toString(QStringLiteral("no error class")));
        cb({}, desc);
    } else {
        cb(msg[QStringLiteral("return")], QString());
    }
}
