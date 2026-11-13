#include "GlibIntegration.h"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QTimer>

#include <glib.h>

namespace GlibIntegration {

bool qtUsesGlib()
{
    auto *d = QAbstractEventDispatcher::instance();
    return d && QByteArray(d->metaObject()->className()).contains("Glib");
}

void ensure()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    if (qtUsesGlib())
        return;
    auto *t = new QTimer(QCoreApplication::instance());
    t->setInterval(2);
    QObject::connect(t, &QTimer::timeout, [] {
        for (int i = 0; i < 64 && g_main_context_iteration(nullptr, FALSE); ++i) {
        }
    });
    t->start();
}

} // namespace GlibIntegration
