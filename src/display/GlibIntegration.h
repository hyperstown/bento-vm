#pragma once

// GDBus delivers callbacks through the default GLib main context. Qt on Linux
// normally runs on top of GLib already; if it doesn't (QT_NO_GLIB=1), pump the
// GLib context from a Qt timer instead.
namespace GlibIntegration {
void ensure();
bool qtUsesGlib();
}
