#include "core/AppSettings.h"
#include "core/Paths.h"
#include "core/Vm.h"
#include "core/VmLibrary.h"
#include "display/GlibIntegration.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

// A second launch hands its arguments to the running instance and exits, so
// two Bento processes never fight over the same VMs.
static QString instanceSocket()
{
    return Paths::runtimeDir() + QStringLiteral("/bento.sock");
}

static bool forwardToRunningInstance(const QJsonObject &msg)
{
    QLocalSocket s;
    s.connectToServer(instanceSocket());
    if (!s.waitForConnected(500))
        return false;
    s.write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');
    s.waitForBytesWritten(1000);
    s.disconnectFromServer();
    return true;
}

static void handleRequest(MainWindow *w, const QJsonObject &msg)
{
    w->show();
    w->raise();
    w->activateWindow();
    const QString open = msg[QStringLiteral("open")].toString();
    if (!open.isEmpty()) {
        VmLibrary *lib = VmLibrary::instance();
        Vm *vm = lib->find(open);
        if (!vm)
            vm = lib->findByName(open);
        if (vm)
            w->openVm(vm);
    }
    for (const QJsonValue &f : msg[QStringLiteral("files")].toArray())
        w->openWizard(f.toString());
}

int main(int argc, char *argv[])
{
    // The embedded display imports DMA-BUFs through EGL; make Qt use EGL on X11 too.
    if (qgetenv("XDG_SESSION_TYPE") == "x11" && qEnvironmentVariableIsEmpty("QT_XCB_GL_INTEGRATION"))
        qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("bento-vm"));
    QApplication::setApplicationDisplayName(QStringLiteral("Bento"));
    QApplication::setApplicationVersion(QStringLiteral(BENTO_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("bento-vm"));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("bento-vm"), QIcon(QStringLiteral(":/icons/bento-vm.svg"))));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("QEMU/KVM virtual machines with Venus GPU acceleration"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption openOpt({QStringLiteral("o"), QStringLiteral("open")},
                               QStringLiteral("Start/open the VM with this name or id."), QStringLiteral("vm"));
    parser.addOption(openOpt);
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("ISO or disk images to create VMs from."),
                                 QStringLiteral("[files...]"));
    parser.process(app);

    QJsonObject request;
    if (parser.isSet(openOpt))
        request[QStringLiteral("open")] = parser.value(openOpt);
    QJsonArray files;
    for (const QString &f : parser.positionalArguments())
        files.append(QFileInfo(f).absoluteFilePath());
    request[QStringLiteral("files")] = files;

    Paths::ensureDir(Paths::runtimeDir(), true);
    if (forwardToRunningInstance(request))
        return 0;
    QLocalServer::removeServer(instanceSocket());
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    server.listen(instanceSocket());

    GlibIntegration::ensure();
    Theme::instance()->init();
    Theme::instance()->apply(AppSettings::instance().theme());

    // First run: make sure the default storage location exists.
    const QString def = AppSettings::instance().defaultLocation();
    if (def == Paths::defaultStorageLocation())
        Paths::ensureDir(def);
    VmLibrary::instance()->refresh();

    MainWindow w;
    w.show();

    QObject::connect(&server, &QLocalServer::newConnection, &w, [&server, &w] {
        while (QLocalSocket *c = server.nextPendingConnection()) {
            QObject::connect(c, &QLocalSocket::readyRead, &w, [c, &w] {
                if (!c->canReadLine())
                    return;
                handleRequest(&w, QJsonDocument::fromJson(c->readLine()).object());
                c->deleteLater();
            });
            QObject::connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
        }
    });

    if (!request[QStringLiteral("open")].toString().isEmpty() || !files.isEmpty())
        QTimer::singleShot(0, &w, [&w, request] { handleRequest(&w, request); });

    const int rc = app.exec();
    server.close();
    return rc;
}
