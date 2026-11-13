// Protocol test for VdAgent against the real spice-vdagentd/spice-vdagent
// (running in a container, with the virtio port emulated by a pty bridged to
// a unix socket). The container is driven through `podman exec`.
//
//   vdagent_test <socket> <container> <file-to-send> <guest-download-dir>
#include "agent/VdAgent.h"

#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QFile>
#include <QEventLoop>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

#include <cstdio>
#include <functional>

static QString g_ctr;
static int g_failures = 0;

static QString guest(const QString &cmd)
{
    QProcess p;
    p.start(QStringLiteral("podman"), {QStringLiteral("exec"), g_ctr, QStringLiteral("sh"), QStringLiteral("-c"),
                                       QStringLiteral("export DISPLAY=:1; ") + cmd});
    // Keep our event loop running: the guest may be waiting for us to answer
    // a clipboard request while this command runs.
    QEventLoop loop;
    QObject::connect(&p, &QProcess::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    if (p.state() != QProcess::NotRunning)
        loop.exec();
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

static void check(const char *what, bool ok, const QString &detail = {})
{
    std::printf("%s  %s%s\n", ok ? "PASS" : "FAIL", what, detail.isEmpty() ? "" : qPrintable(QStringLiteral("  (") + detail + QLatin1Char(')')));
    std::fflush(stdout);
    if (!ok)
        ++g_failures;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc < 5)
        return 2;
    const QString sock = QString::fromLocal8Bit(argv[1]);
    g_ctr = QString::fromLocal8Bit(argv[2]);
    const QString file = QString::fromLocal8Bit(argv[3]);
    const QString dlDir = QString::fromLocal8Bit(argv[4]);

    VdAgent agent;
    QList<std::pair<int, std::function<void()>>> steps;
    auto run = [&](auto &&self, int i) -> void {
        if (i >= steps.size()) {
            app.exit(g_failures ? 1 : 0);
            return;
        }
        QTimer::singleShot(steps[i].first, [&, i, self] {
            steps[i].second();
            self(self, i + 1);
        });
    };

    const QString a = QStringLiteral("host→guest ✓ %1").arg(QCoreApplication::applicationPid());
    const QString b = QStringLiteral("guest-to-host-%1").arg(QCoreApplication::applicationPid());
    QClipboard *cb = QApplication::clipboard();

    steps << std::make_pair(1500, [&] {
        check("guest agent announced itself", agent.isGuestConnected());
        check("guest supports clipboard", agent.guestSupportsClipboard());
        check("guest supports file transfer", agent.guestSupportsFileTransfer());
        cb->setText(a);
    });
    steps << std::make_pair(2000, [&] {
        const QString got = guest(QStringLiteral("timeout 5 xclip -o -selection clipboard"));
        check("host → guest clipboard", got == a, got);
        guest(QStringLiteral("printf '%1' | xclip -i -selection clipboard").arg(b));
    });
    steps << std::make_pair(2500, [&] {
        check("guest → host clipboard", cb->text() == b, cb->text());
        agent.setClipboardMode(QStringLiteral("host-to-guest"));
        guest(QStringLiteral("printf 'must-not-arrive' | xclip -i -selection clipboard"));
    });
    steps << std::make_pair(2500, [&] {
        check("mode host-to-guest blocks guest → host", cb->text() == b, cb->text());
        agent.setClipboardMode(QStringLiteral("guest-to-host"));
        cb->setText(QStringLiteral("must-not-arrive-either"));
    });
    steps << std::make_pair(2000, [&] {
        const QString got = guest(QStringLiteral("timeout 3 xclip -o -selection clipboard"));
        check("mode guest-to-host blocks host → guest", got != QLatin1String("must-not-arrive-either"), got);
        agent.setClipboardMode(QStringLiteral("both"));
    });

    bool xferDone = false, xferOk = false;
    QString xferMsg;
    QObject::connect(&agent, &VdAgent::transferFinished, [&](const QString &, bool ok, const QString &msg) {
        xferDone = true;
        xferOk = ok;
        xferMsg = msg;
    });
    QObject::connect(&agent, &VdAgent::notice, [](const QString &t) { std::printf("notice: %s\n", qPrintable(t)); });
    steps << std::make_pair(500, [&] { check("file transfer queued", agent.sendFiles({file}) == 1); });
    steps << std::make_pair(8000, [&] {
        check("file transfer finished OK", xferDone && xferOk, xferMsg);
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            check("open local payload", false);
        const QString want = QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());
        const QString got = guest(QStringLiteral("sha256sum '%1/%2' | cut -d' ' -f1").arg(dlDir, QFileInfo(file).fileName()));
        check("file arrived intact in the guest", got == want, QStringLiteral("%1 bytes").arg(QFileInfo(file).size()));
    });

    QObject::connect(&agent, &VdAgent::guestConnectedChanged, [&](bool on) {
        static bool started = false;
        if (on && !started) {
            started = true;
            run(run, 0);
        }
    });
    QTimer::singleShot(20000, [&] {
        if (!agent.isGuestConnected()) {
            check("guest agent connected within 20s", false);
            app.exit(1);
        }
    });
    agent.connectTo(sock);
    return app.exec();
}
