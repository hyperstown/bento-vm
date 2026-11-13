// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Theme.h"
#include "Icons.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QIconEngine>
#include <QLoggingCategory>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

Q_LOGGING_CATEGORY(lcTheme, "bento.theme", QtInfoMsg)

namespace {

struct Colors
{
    const char *bg, *surface, *surface2, *hover, *border, *borderHover, *text, *muted, *accent, *accent2,
        *bannerBg, *bannerBorder;
    bool dark;
    const char *iconTheme;
};

// Modern look: flat, rounded surfaces with a green gradient accent.
constexpr Colors kModernDark{"#0e1015", "#161922", "#1d2130", "#232838", "#262b3a", "#3a4156", "#e7e9f0", "#8b92a6",
                             "#1fae5f", "#0b8f73", "#13261d", "#1f5a3d", true, "breeze-dark"};
constexpr Colors kModernLight{"#f3f4f8", "#ffffff", "#eef0f6", "#e7eaf3", "#dde1ec", "#c3c9da", "#1b1f2a", "#6b7285",
                              "#16a051", "#0a8570", "#ecfdf3", "#b7ebcd", false, "breeze"};

const char *kQss = R"QSS(
QMainWindow, QDialog, QWizard, QWizardPage { background: @bg; }
QDialogButtonBox { dialogbuttonbox-buttons-have-icons: 0; }   /* set by updateDialogButtonIcons() */
QWidget { color: @text; }
QWidget#pageBackground, QStackedWidget#mainStack, QWidget#vmGrid { background: @bg; }
QMenuBar { background: @bg; }
QMenuBar::item:selected { background: @hover; border-radius: 6px; }
QStatusBar { background: @bg; color: @muted; }
QToolBar { background: @bg; border: none; padding: 10px 14px; spacing: 6px; }
QToolBar::separator { background: @border; width: 1px; margin: 6px 6px; }
QToolBar QToolButton { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 7px 12px; color: @text; }
QToolBar QToolButton:hover { background: @hover; border-color: @border; }
QToolBar QToolButton:pressed, QToolBar QToolButton:checked { background: @surface2; border-color: @border; }
QToolBar QToolButton:disabled { color: @muted; }
QToolButton#cardButton { background: @surface2; border: 1px solid @border; border-radius: 6px; padding: 5px 12px; }
QToolButton#cardButton:hover { background: @hover; }
QToolButton#cardPrimary { color: white; border: none; border-radius: 6px; padding: 5px 14px; font-weight: 600;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 @accent, stop:1 @accent2); }
QToolButton#cardPrimary:disabled { background: @surface2; color: @muted; }
QPushButton { background: @surface2; border: 1px solid @border; border-radius: 6px; padding: 8px 18px; color: @text; }
QPushButton:hover { background: @hover; border-color: @borderHover; }
QPushButton:pressed { background: @border; }
QPushButton:disabled { color: @muted; background: @surface; }
QPushButton:default, QPushButton[accent="true"] { color: white; border: none; font-weight: 600;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 @accent, stop:1 @accent2); }
QPushButton:default:disabled, QPushButton[accent="true"]:disabled { background: @surface2; color: @muted; }
QPushButton[flat="true"] { background: transparent; border: none; color: @accent; padding: 4px 6px; }
QPushButton[flat="true"]:hover { text-decoration: underline; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit {
    background: @surface; border: 1px solid @border; border-radius: 8px; padding: 6px 10px; selection-background-color: @accent; selection-color: white; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: @accent; }
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { color: @muted; }
QComboBox::drop-down { border: none; width: 26px; }
QComboBox QAbstractItemView { background: @surface; border: 1px solid @border; border-radius: 8px; padding: 4px; selection-background-color: @hover; selection-color: @text; outline: 0; }
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
QScrollBar::handle { background: @border; border-radius: 4px; min-height: 40px; min-width: 40px; }
QScrollBar::handle:hover { background: @muted; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QMenu { background: @surface; border: 1px solid @border; border-radius: 12px; padding: 6px; }
QMenu::item { padding: 7px 26px 7px 12px; border-radius: 8px; }
QMenu::item:selected { background: @hover; }
QMenu::item:disabled { color: @muted; }
QMenu::separator { height: 1px; background: @border; margin: 5px 8px; }
QMenu::icon { padding-left: 8px; }
QToolTip { background: @surface2; color: @text; border: 1px solid @border; border-radius: 6px; padding: 5px 8px; }
QGroupBox { background: @surface; border: 1px solid @border; border-radius: 14px; margin-top: 22px; padding: 16px 14px 12px 14px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 0 4px; color: @muted; font-weight: 600; }
QListWidget, QTreeWidget, QTableWidget, QListView, QTreeView, QTableView { background: @surface; border: 1px solid @border; border-radius: 10px; outline: 0; alternate-background-color: @surface2; }
QListWidget::item { padding: 6px; border-radius: 8px; }
QListWidget::item:selected, QTreeView::item:selected, QTableView::item:selected { background: @hover; color: @text; }
QListWidget#pageNav { background: @bg; border: none; padding: 6px; }
QListWidget#pageNav::item { padding: 10px 12px; margin: 1px 0; border-radius: 10px; }
QListWidget#pageNav::item:selected { background: @surface2; color: @text; }
QListWidget#pageNav::item:hover:!selected { background: @surface; }
QHeaderView::section { background: @surface2; color: @muted; border: none; padding: 6px 8px; }
QSlider::groove:horizontal { height: 6px; background: @border; border-radius: 3px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 3px; }
QSlider::handle:horizontal { background: white; width: 18px; height: 18px; margin: -7px 0; border-radius: 10px; border: 2px solid @accent; }
QProgressBar { background: @surface2; border: none; border-radius: 6px; min-height: 10px; max-height: 10px; text-align: center; color: transparent; }
QProgressBar::chunk { border-radius: 6px; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @accent, stop:1 @accent2); }
QTabWidget::pane { border: 1px solid @border; border-radius: 10px; top: -1px; }
QTabBar::tab { background: transparent; padding: 8px 14px; border-radius: 8px; color: @muted; }
QTabBar::tab:selected { background: @surface2; color: @text; }
#vmCard { background: @surface; border: 1px solid @border; border-radius: 18px; }
#vmCard:hover { background: @surface2; border-color: @borderHover; }
#vmCard[selected="true"] { border: 2px solid @accent; }
#vmCardName { font-size: 12pt; font-weight: 600; }
#mutedLabel, #vmCardSub, #hintLabel { color: @muted; }
#pageTitle { font-size: 20pt; font-weight: 700; }
#pageSubtitle { color: @muted; }
#emptyTitle { font-size: 18pt; font-weight: 700; }
#toast { background: @surface2; border: 1px solid @border; border-radius: 14px; }
QFrame#banner { background: @bannerBg; border: 1px solid @bannerBorder; border-radius: 12px; }
QFrame#card, QFrame#optionCard { background: @surface; border: 1px solid @border; border-radius: 8px; }
QFrame#optionCard:hover { border-color: @borderHover; }
QFrame#optionCard[checked="true"] { border: 2px solid @accent; }
QFrame#infoPage { background: @bg; }
QRadioButton::indicator, QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid @borderHover; background: @surface; }
QRadioButton::indicator { border-radius: 9px; }
QCheckBox::indicator { border-radius: 5px; }
QRadioButton::indicator:hover, QCheckBox::indicator:hover { border-color: @accent; }
QRadioButton::indicator:checked { border: 5px solid @accent; background: white; width: 8px; height: 8px; }
QCheckBox::indicator:checked { background: @accent; border-color: @accent; image: url(:/icons/check.svg); }
QRadioButton::indicator:disabled, QCheckBox::indicator:disabled { background: @surface2; border-color: @border; }
QCheckBox::indicator:checked:disabled { background: @border; }
QComboBox::down-arrow { image: url(@chevronDown); width: 12px; height: 12px; }
QSpinBox, QDoubleSpinBox { padding-right: 24px; }
QSpinBox::up-button, QDoubleSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::down-button {
    border: none; background: transparent; width: 20px; subcontrol-origin: border; }
QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-position: top right; margin-top: 3px; }
QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-position: bottom right; margin-bottom: 3px; }
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background: @hover; border-radius: 4px; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(@chevronUp); width: 10px; height: 10px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(@chevronDown); width: 10px; height: 10px; }
QToolButton::menu-indicator { image: url(@chevronDown); subcontrol-origin: padding; subcontrol-position: center right; right: 6px; width: 10px; height: 10px; }
QToolBar QToolButton[popupMode="2"] { padding-right: 24px; }
QToolBar QToolButton[popupMode="1"] { padding-right: 28px; }
QToolButton::menu-button { border: none; border-left: 1px solid @border; width: 20px; background: transparent;
    border-top-right-radius: 6px; border-bottom-right-radius: 6px; }
QToolButton::menu-button:hover { background: @surface2; }
QToolButton::menu-arrow { image: url(@chevronDown); width: 10px; height: 10px; }
QToolButton#hamburger { padding-right: 10px; }
QToolButton#hamburger::menu-indicator { image: none; width: 0px; }
QToolButton#cardButton::menu-indicator { image: none; width: 0px; }
)QSS";

QString buildQss(const Colors &c)
{
    QString s = QString::fromLatin1(kQss);
    s.replace(QLatin1String("@chevronDown"), c.dark ? QLatin1String(":/icons/chevron-down-light.svg")
                                                     : QLatin1String(":/icons/chevron-down-dark.svg"));
    s.replace(QLatin1String("@chevronUp"), c.dark ? QLatin1String(":/icons/chevron-up-light.svg")
                                                   : QLatin1String(":/icons/chevron-up-dark.svg"));
    const std::pair<const char *, const char *> vars[] = {
        {"@surface2", c.surface2}, {"@surface", c.surface}, {"@hover", c.hover}, {"@borderHover", c.borderHover},
        {"@border", c.border},     {"@text", c.text},       {"@muted", c.muted}, {"@accent2", c.accent2},
        {"@accent", c.accent},     {"@bannerBg", c.bannerBg}, {"@bannerBorder", c.bannerBorder}, {"@bg", c.bg},
    };
    for (const auto &[k, v] : vars)
        s.replace(QLatin1String(k), QLatin1String(v));
    return s;
}

bool iconThemeAvailable(const QString &name)
{
    for (const QString &dir : QIcon::themeSearchPaths())
        if (QFileInfo::exists(dir + QLatin1Char('/') + name + QStringLiteral("/index.theme")))
            return true;
    return false;
}

QString g_origIconTheme;

QPalette buildPalette(const Colors &c)
{
    QPalette p;
    const QColor bg(QLatin1String(c.bg)), surface(QLatin1String(c.surface)), surface2(QLatin1String(c.surface2)),
        border(QLatin1String(c.border)), text(QLatin1String(c.text)), muted(QLatin1String(c.muted)),
        accent(QLatin1String(c.accent));
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, surface);
    p.setColor(QPalette::AlternateBase, surface2);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, surface2);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, surface2);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, muted);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::Light, c.dark ? border.lighter(130) : Qt::white);
    p.setColor(QPalette::Midlight, border);
    p.setColor(QPalette::Mid, border);
    p.setColor(QPalette::Dark, c.dark ? bg.darker(130) : border.darker(120));
    p.setColor(QPalette::Shadow, Qt::black);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, muted);
    return p;
}

// Breeze's dark glyphs are hard to see on the green gradient. Checks the theme
// at paint time, so it follows live theme switches. With a button, only while
// that is the dialog's default (green) one, which moves with the focus.
class OnAccentIconEngine : public QIconEngine
{
public:
    explicit OnAccentIconEngine(const QIcon &icon, const QPushButton *button = nullptr)
        : m_icon(icon), m_button(button), m_followButton(button)
    {
    }
    QIconEngine *clone() const override
    {
        auto *e = new OnAccentIconEngine(m_icon, m_button);
        e->m_followButton = m_followButton;
        return e;
    }
    QString key() const override { return QStringLiteral("bento-on-accent"); }
    bool isNull() override { return m_icon.isNull(); }
    QSize actualSize(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return m_icon.actualSize(size, mode, state);
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pm = m_icon.pixmap(size, scale, mode, state);
        if (pm.isNull() || mode == QIcon::Disabled || !Theme::instance()->isCustom())
            return pm;
        if (m_followButton && !(m_button && (m_button->isDefault() || m_button->property("accent").toBool())))
            return pm;
        QPainter p(&pm);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(QRect(QPoint(), pm.size()), Qt::white);
        return pm;
    }
    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal dpr = p->device() ? p->device()->devicePixelRatio() : qApp->devicePixelRatio();
        p->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, dpr));
    }

private:
    QIcon m_icon;
    QPointer<const QPushButton> m_button;
    bool m_followButton = false;
};

// Icons on OK/Cancel/… buttons. Where the icon theme lacks them (GNOME's), Qt
// falls back to coloured bitmaps that clash with the Modern themes, so these
// use the theme's glyphs or none. QDialogButtonBox picks icons only when it
// creates its buttons; this runs when a box is shown and on theme switches.
void updateDialogButtonIcons(QDialogButtonBox *box, bool modern)
{
    struct Icon
    {
        QStyle::StandardPixmap pixmap;
        const char *name;
    };
    static const QHash<QDialogButtonBox::StandardButton, Icon> icons{
        {QDialogButtonBox::Ok, {QStyle::SP_DialogOkButton, "dialog-ok"}},
        {QDialogButtonBox::Cancel, {QStyle::SP_DialogCancelButton, "dialog-cancel"}},
        {QDialogButtonBox::Apply, {QStyle::SP_DialogApplyButton, "dialog-ok-apply"}},
        {QDialogButtonBox::Close, {QStyle::SP_DialogCloseButton, "dialog-close"}},
        {QDialogButtonBox::Yes, {QStyle::SP_DialogYesButton, "dialog-ok"}},
        {QDialogButtonBox::No, {QStyle::SP_DialogNoButton, "dialog-cancel"}},
        {QDialogButtonBox::Save, {QStyle::SP_DialogSaveButton, "document-save"}},
        {QDialogButtonBox::Discard, {QStyle::SP_DialogDiscardButton, "edit-delete"}},
        {QDialogButtonBox::Help, {QStyle::SP_DialogHelpButton, "help-contents"}},
        {QDialogButtonBox::Reset, {QStyle::SP_DialogResetButton, "edit-undo"}},
        {QDialogButtonBox::Open, {QStyle::SP_DialogOpenButton, "document-open"}},
        {QDialogButtonBox::RestoreDefaults, {QStyle::SP_RestoreDefaultsButton, "document-revert"}},
    };
    const bool styleIcons = box->style()->styleHint(QStyle::SH_DialogButtonBox_ButtonsHaveIcons, nullptr, box);
    for (QAbstractButton *b : box->buttons()) {
        const auto it = icons.constFind(box->standardButton(b));
        if (it == icons.cend())
            continue;
        if (modern) {
            const QIcon icon = Icons::fromTheme(QLatin1String(it->name));
            b->setIcon(icon.isNull() ? QIcon() : QIcon(new OnAccentIconEngine(icon, qobject_cast<QPushButton *>(b))));
        } else {
            b->setIcon(styleIcons ? box->style()->standardIcon(it->pixmap, nullptr, b) : QIcon());
        }
    }
}

} // namespace

bool Theme::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Show)
        if (auto *box = qobject_cast<QDialogButtonBox *>(watched))
            updateDialogButtonIcons(box, isCustom());
    return QObject::eventFilter(watched, event);
}

QIcon Theme::onAccent(const QIcon &icon)
{
    return QIcon(new OnAccentIconEngine(icon));
}

Theme *Theme::instance()
{
    static Theme *t = new Theme;
    return t;
}

QList<Theme::Info> Theme::available()
{
    const Theme *t = instance();
    const QString look = t->automaticLook();
    QString now;
    if (look == QLatin1String("system"))
        now = QStringLiteral("On this desktop: System (%1).").arg(t->desktopStyleName());
    else
        now = QStringLiteral("This desktop has no Qt style, so it uses %1.")
                  .arg(look == QLatin1String("modern-dark") ? QStringLiteral("Modern Dark") : QStringLiteral("Modern Light"));
    return {
        {QStringLiteral("auto"), QStringLiteral("Automatic"),
         QStringLiteral("Your desktop's own style where it provides one, as KDE Plasma does, otherwise the Modern "
                        "theme in your colour scheme. ") + now},
        {QStringLiteral("system"), QStringLiteral("System"),
         QStringLiteral("Always follows your desktop's Qt style and colour scheme, like any KDE application. Without "
                        "a desktop style, this is Qt's plain built-in look.")},
        {QStringLiteral("modern-dark"), QStringLiteral("Modern Dark"),
         QStringLiteral("Dark look with rounded cards and a green gradient accent.")},
        {QStringLiteral("modern-light"), QStringLiteral("Modern Light"),
         QStringLiteral("The same modern look in a light variant.")},
    };
}

void Theme::init()
{
    if (m_origPalette)
        return;
    m_origStyle = QApplication::style()->name();
    m_origPalette = new QPalette(QApplication::palette());
    g_origIconTheme = QIcon::themeName();
    qApp->installEventFilter(this);   // dialog button icons, see updateDialogButtonIcons()
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_selected == QLatin1String("auto") && automaticLook() != m_current)
            apply(m_selected);
    });
}

bool Theme::hasDesktopStyle() const
{
    // Qt's own styles are what it falls back to when the desktop (GNOME, Xfce, …)
    // doesn't provide one, or its style plugin fails to load.
    const QString style = m_origStyle.isEmpty() ? QApplication::style()->name() : m_origStyle;
    for (const char *builtIn : {"fusion", "windows"})
        if (style.compare(QLatin1String(builtIn), Qt::CaseInsensitive) == 0)
            return false;
    return !style.isEmpty();
}

QString Theme::desktopStyleName() const
{
    return m_origStyle.isEmpty() ? m_origStyle : m_origStyle.front().toUpper() + m_origStyle.mid(1);
}

QString Theme::automaticLook() const
{
    if (hasDesktopStyle())
        return QStringLiteral("system");
    bool dark = false;
    switch (QGuiApplication::styleHints()->colorScheme()) {
    case Qt::ColorScheme::Dark: dark = true; break;
    case Qt::ColorScheme::Light: dark = false; break;
    default: dark = m_origPalette && m_origPalette->color(QPalette::Window).lightness() < 128; break;
    }
    return dark ? QStringLiteral("modern-dark") : QStringLiteral("modern-light");
}

void Theme::apply(const QString &id)
{
    init();
    const bool known = id == QLatin1String("system") || id == QLatin1String("modern-dark")
                       || id == QLatin1String("modern-light");
    m_selected = known ? id : QStringLiteral("auto");
    const QString look = known ? id : automaticLook();
    const Colors *c = look == QLatin1String("modern-dark")    ? &kModernDark
                      : look == QLatin1String("modern-light") ? &kModernLight
                                                              : nullptr;
    if (!c) {
        qApp->setStyleSheet(QString());
        if (!m_origStyle.isEmpty() && QApplication::style()->name() != m_origStyle)
            QApplication::setStyle(QStyleFactory::create(m_origStyle));
        QApplication::setPalette(*m_origPalette);
        if (QIcon::themeName() != g_origIconTheme)
            QIcon::setThemeName(g_origIconTheme);
        m_current = QStringLiteral("system");
    } else {
        if (QApplication::style()->name().compare(QLatin1String("fusion"), Qt::CaseInsensitive) != 0)
            QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QApplication::setPalette(buildPalette(*c));
        // Breeze's light icons vanish on a dark background (and vice versa).
        // Other desktops keep their icons; see Icons::fromTheme().
        if (hasDesktopStyle() && iconThemeAvailable(QLatin1String(c->iconTheme)))
            QIcon::setThemeName(QLatin1String(c->iconTheme));
        qApp->setStyleSheet(buildQss(*c));
        m_current = look;
    }
    for (QWidget *w : QApplication::allWidgets())
        if (auto *box = qobject_cast<QDialogButtonBox *>(w))
            updateDialogButtonIcons(box, isCustom());
    qCDebug(lcTheme) << "theme" << m_selected << "shows" << m_current << "- desktop style" << m_origStyle
                     << "colour scheme" << QGuiApplication::styleHints()->colorScheme() << "icons"
                     << QIcon::themeName();
    Q_EMIT changed();
}

bool Theme::isDark() const
{
    if (m_current == QLatin1String("modern-dark"))
        return true;
    if (m_current == QLatin1String("modern-light"))
        return false;
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

QColor Theme::accent() const
{
    if (m_current == QLatin1String("modern-dark"))
        return QColor(QLatin1String(kModernDark.accent));
    if (m_current == QLatin1String("modern-light"))
        return QColor(QLatin1String(kModernLight.accent));
    return QApplication::palette().color(QPalette::Highlight);
}

QColor Theme::stateColor(const QString &state)
{
    if (state == QLatin1String("running"))
        return QColor(0x22, 0xc5, 0x5e);
    if (state == QLatin1String("paused"))
        return QColor(0xf5, 0x9e, 0x0b);
    if (state == QLatin1String("starting") || state == QLatin1String("stopping"))
        return QColor(0x3b, 0x82, 0xf6);
    if (state == QLatin1String("unavailable"))
        return QColor(0xef, 0x44, 0x44);
    return QColor(0x9c, 0xa3, 0xaf);
}
