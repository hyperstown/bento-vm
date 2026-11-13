// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Icons.h"
#include "Theme.h"

#include <QApplication>
#include <QHash>
#include <QEvent>
#include <QIconEngine>
#include <QLabel>
#include <QLoggingCategory>
#include <QPainter>
#include <QPalette>

Q_LOGGING_CATEGORY(lcIcons, "bento.icons", QtInfoMsg)

namespace {

// Standard names for KDE icon names that other themes don't have.
const QHash<QString, QStringList> &alternatives()
{
    static const QHash<QString, QStringList> map{
        {QStringLiteral("application-menu"), {QStringLiteral("open-menu")}},
        {QStringLiteral("overflow-menu"), {QStringLiteral("view-more")}},
        {QStringLiteral("configure"), {QStringLiteral("emblem-system"), QStringLiteral("preferences-system")}},
        {QStringLiteral("search"), {QStringLiteral("edit-find"), QStringLiteral("system-search")}},
        {QStringLiteral("code-context"), {QStringLiteral("applications-engineering"), QStringLiteral("preferences-other")}},
        {QStringLiteral("cpu"), {QStringLiteral("computer")}},
        {QStringLiteral("documentinfo"), {QStringLiteral("document-properties"), QStringLiteral("dialog-information")}},
        {QStringLiteral("document-preview"), {QStringLiteral("view-reveal")}},
        {QStringLiteral("text-x-log"), {QStringLiteral("text-x-generic")}},
        {QStringLiteral("edit-clear-list"), {QStringLiteral("edit-clear-all"), QStringLiteral("edit-clear")}},
        {QStringLiteral("rating"), {QStringLiteral("starred")}},
        {QStringLiteral("dialog-cancel"), {QStringLiteral("process-stop"), QStringLiteral("window-close")}},
        {QStringLiteral("dialog-ok"), {QStringLiteral("object-select")}},
        {QStringLiteral("dialog-ok-apply"), {QStringLiteral("object-select")}},
        {QStringLiteral("dialog-close"), {QStringLiteral("window-close")}},
        {QStringLiteral("help-contents"), {QStringLiteral("help-browser")}},
        {QStringLiteral("document-import"), {QStringLiteral("document-open")}},
        {QStringLiteral("document-open-folder"), {QStringLiteral("folder-open"), QStringLiteral("document-open")}},
        {QStringLiteral("drive-removable-media-usb"), {QStringLiteral("drive-removable-media")}},
        {QStringLiteral("folder-important"), {QStringLiteral("emblem-important")}},
        {QStringLiteral("network-bluetooth"), {QStringLiteral("bluetooth")}},
        {QStringLiteral("preferences-desktop-mouse"), {QStringLiteral("input-mouse")}},
        // Not "preferences-desktop-appearance": Adwaita draws it with an SVG mask, which Qt can't render.
        {QStringLiteral("preferences-desktop-theme"), {QStringLiteral("applications-graphics")}},
        {QStringLiteral("preferences-system-windows"),
         {QStringLiteral("preferences-desktop-apps"), QStringLiteral("applications-system")}},
    };
    return map;
}

class SymbolicIconEngine : public QIconEngine
{
public:
    explicit SymbolicIconEngine(const QString &name) : m_name(name), m_icon(QIcon::fromTheme(name)) {}
    QIconEngine *clone() const override { return new SymbolicIconEngine(m_name); }
    QString key() const override { return QStringLiteral("bento-symbolic"); }
    QString iconName() override { return m_name; }
    bool isNull() override { return m_icon.isNull(); }
    QSize actualSize(const QSize &size, QIcon::Mode, QIcon::State) override { return size; }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        // Symbolic glyphs are drawn edge to edge on a 16 px grid. In larger slots
        // (toolbars, sidebars) keep them at that size or a little more, with
        // padding like other themes' icons have.
        const int side = std::min(size.width(), size.height());
        const int glyph = side <= 16 ? side : side < 32 ? 16 : side * 3 / 4;
        const QPixmap g = m_icon.pixmap(QSize(glyph, glyph), scale, QIcon::Normal, state);
        if (g.isNull())
            return g;
        QPixmap pm(size * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            p.drawPixmap(QPointF((size.width() - glyph) / 2.0, (size.height() - glyph) / 2.0), g);
        }
        // Taken when painting, so the glyphs follow theme switches.
        const QPalette pal = QApplication::palette();
        // The Modern themes keep the text colour on selected items.
        const bool highlighted = mode == QIcon::Selected && !Theme::instance()->isCustom();
        const QColor color = mode == QIcon::Disabled ? pal.color(QPalette::Disabled, QPalette::WindowText)
                             : highlighted           ? pal.color(QPalette::HighlightedText)
                                                     : pal.color(QPalette::WindowText);
        QPainter p(&pm);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(QRect(QPoint(), pm.size()), color);
        return pm;
    }
    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal dpr = p->device() ? p->device()->devicePixelRatio() : qApp->devicePixelRatio();
        p->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, dpr));
    }

private:
    QString m_name;
    QIcon m_icon;
};

QIcon symbolicIcon(const QStringList &names)
{
    for (const QString &n : names) {
        const QString s = n.endsWith(QLatin1String("-symbolic")) ? n : n + QStringLiteral("-symbolic");
        if (QIcon::hasThemeIcon(s))
            return QIcon(new SymbolicIconEngine(s));
    }
    return {};
}

class LabelIcon : public QObject
{
public:
    static constexpr const char *kName = "bentoLabelIcon";
    LabelIcon(QLabel *label, const QIcon &icon, int size) : QObject(label), m_label(label), m_icon(icon), m_size(size)
    {
        setObjectName(QLatin1String(kName));
        label->installEventFilter(this);
        update();
    }
    bool eventFilter(QObject *, QEvent *e) override
    {
        if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange)
            update();
        return false;
    }

private:
    void update() { m_label->setPixmap(m_icon.pixmap(QSize(m_size, m_size), m_label->devicePixelRatioF())); }
    QLabel *m_label;
    QIcon m_icon;
    int m_size;
};

QIcon resolve(const QString &name)
{
    const QStringList names = QStringList{name} + alternatives().value(name);
    const bool symbolicFirst = !Theme::instance()->hasDesktopStyle();
    if (symbolicFirst)
        if (QIcon icon = symbolicIcon(names); !icon.isNull())
            return icon;
    for (const QString &n : names)
        if (QIcon::hasThemeIcon(n))
            return QIcon::fromTheme(n);
    if (!symbolicFirst)
        if (QIcon icon = symbolicIcon(names); !icon.isNull())
            return icon;
    return {};
}

} // namespace

QIcon Icons::fromTheme(const QString &name)
{
    if (QIcon icon = resolve(name); !icon.isNull())
        return icon;
    qCDebug(lcIcons) << "no icon" << name << "in" << QIcon::themeName();
    return QIcon::fromTheme(name);   // still picks it up from a theme set later
}

QIcon Icons::fromTheme(const QString &name, const QIcon &fallback)
{
    const QIcon icon = resolve(name);
    return icon.isNull() ? fallback : icon;
}

void Icons::showIn(QLabel *label, const QIcon &icon, int size)
{
    // Replaces an earlier icon.
    delete label->findChild<QObject *>(QLatin1String(LabelIcon::kName), Qt::FindDirectChildrenOnly);
    new LabelIcon(label, icon, size);
}

bool Icons::isSymbolic(const QIcon &icon)
{
    return icon.name().endsWith(QLatin1String("-symbolic"));
}
