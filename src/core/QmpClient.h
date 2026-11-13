#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>

#include <functional>

class QSocketNotifier;

// Minimal asynchronous QMP client over a unix socket. Uses a raw socket (not
// QLocalSocket) because some commands ("getfd") need SCM_RIGHTS fd passing.
class QmpClient : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const QJsonValue &result, const QString &error)>;

    explicit QmpClient(QObject *parent = nullptr);
    ~QmpClient() override;

    bool connectTo(const QString &socketPath, QString *error = nullptr);
    void disconnectFromQemu();
    bool isConnected() const { return m_fd >= 0; }
    bool isReady() const { return m_ready; }

    void execute(const QString &command, const QJsonObject &args = {}, Callback cb = {});
    // Sends `fd` along with the command (SCM_RIGHTS). The descriptor is duplicated
    // internally, the caller keeps ownership of the one passed in.
    void executeWithFd(const QString &command, const QJsonObject &args, int fd, Callback cb = {});

Q_SIGNALS:
    void ready();
    void eventReceived(const QString &event, const QJsonObject &data);
    void disconnected();

private:
    struct Pending
    {
        QByteArray line;
        int fd = -1;
    };

    void enqueue(const QString &command, const QJsonObject &args, int fd, Callback cb);
    void onReadable();
    void handleMessage(const QJsonObject &msg);
    bool sendRaw(const QByteArray &data, int fd);
    void flushQueue();
    void closeSocket();

    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QByteArray m_buf;
    bool m_ready = false;
    quint64 m_nextId = 1;
    QHash<QString, Callback> m_callbacks;
    QList<Pending> m_queue;
};
