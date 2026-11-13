#include "VmCard.h"
#include "OsIcon.h"
#include "Theme.h"
#include "core/OsDb.h"
#include "core/Vm.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOption>
#include <QToolButton>
#include <QVBoxLayout>

static constexpr int kCardWidth = 284;
static constexpr int kThumbW = 252;
static constexpr int kThumbH = 142;

VmCard::VmCard(Vm *vm, QWidget *parent)
    : QWidget(parent)
    , m_vm(vm)
{
    setObjectName(QStringLiteral("vmCard"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_Hover, true);
    setFixedSize(kCardWidth, 292);
    setAcceptDrops(true);
    setCursor(Qt::PointingHandCursor);

    m_thumb = new QLabel(this);
    m_thumb->setFixedSize(kThumbW, kThumbH);
    m_name = new QLabel(this);
    m_name->setObjectName(QStringLiteral("vmCardName"));
    QFont nf = m_name->font();
    nf.setPointSizeF(nf.pointSizeF() * 1.12);
    nf.setBold(true);
    m_name->setFont(nf);
    m_sub = new QLabel(this);
    m_sub->setObjectName(QStringLiteral("vmCardSub"));
    m_sub->setWordWrap(true);
    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::RichText);

    m_primary = new QToolButton(this);
    m_primary->setObjectName(QStringLiteral("cardPrimary"));
    m_primary->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_primary->setCursor(Qt::PointingHandCursor);
    m_menu = new QToolButton(this);
    m_menu->setObjectName(QStringLiteral("cardButton"));
    m_menu->setIcon(QIcon::fromTheme(QStringLiteral("overflow-menu"), QIcon::fromTheme(QStringLiteral("application-menu"))));
    m_menu->setToolTip(QStringLiteral("More actions"));
    m_menu->setCursor(Qt::PointingHandCursor);

    auto *bottom = new QHBoxLayout;
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->addWidget(m_status, 1);
    bottom->addWidget(m_primary);
    bottom->addWidget(m_menu);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 16, 16, 14);
    lay->setSpacing(6);
    lay->addWidget(m_thumb, 0, Qt::AlignHCenter);
    lay->addSpacing(6);
    lay->addWidget(m_name);
    lay->addWidget(m_sub);
    lay->addStretch();
    lay->addLayout(bottom);

    connect(m_primary, &QToolButton::clicked, this, [this] { Q_EMIT activated(this); });
    connect(m_menu, &QToolButton::clicked, this, [this] {
        Q_EMIT contextMenuRequested(this, m_menu->mapToGlobal(QPoint(0, m_menu->height())));
    });
    connect(vm, &Vm::stateChanged, this, &VmCard::refresh);
    connect(vm, &Vm::configChanged, this, &VmCard::refresh);
    connect(vm, &Vm::availabilityChanged, this, &VmCard::refresh);
    connect(vm, &Vm::thumbnailChanged, this, &VmCard::refresh);
    connect(Theme::instance(), &Theme::changed, this, &VmCard::refresh);
    refresh();
}

QPixmap VmCard::thumbnailPixmap() const
{
    const qreal dpr = devicePixelRatioF();
    QPixmap base;
    if (!m_vm->thumbnail().isNull()) {
        const QImage img = m_vm->thumbnail().scaled(QSize(kThumbW, kThumbH) * dpr, Qt::KeepAspectRatioByExpanding,
                                                    Qt::SmoothTransformation);
        base = QPixmap::fromImage(img.copy((img.width() - int(kThumbW * dpr)) / 2, (img.height() - int(kThumbH * dpr)) / 2,
                                           int(kThumbW * dpr), int(kThumbH * dpr)));
        base.setDevicePixelRatio(dpr);
    } else {
        base = OsIcon::placeholder(m_vm->config().osId, QSize(kThumbW, kThumbH), dpr);
    }

    QPixmap out(QSize(kThumbW, kThumbH) * dpr);
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, kThumbW, kThumbH), 12, 12);
    p.setClipPath(clip);
    p.drawPixmap(0, 0, base);

    if (!m_vm->isAvailable()) {
        // Dim + desaturate and show a "drive missing" glyph.
        p.fillRect(QRect(0, 0, kThumbW, kThumbH), QColor(10, 12, 18, 170));
        const QIcon icon = QIcon::fromTheme(QStringLiteral("drive-removable-media"),
                                            QIcon::fromTheme(QStringLiteral("dialog-warning")));
        icon.paint(&p, QRect(kThumbW / 2 - 24, kThumbH / 2 - 34, 48, 48));
        p.setPen(Qt::white);
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(0, kThumbH / 2 + 18, kThumbW, 24), Qt::AlignCenter, QStringLiteral("Temporarily unavailable"));
    } else if (m_vm->state() == Vm::State::Stopped && !m_vm->thumbnail().isNull()) {
        p.fillRect(QRect(0, 0, kThumbW, kThumbH), QColor(0, 0, 0, 70));
    }
    return out;
}

void VmCard::refresh()
{
    if (!m_vm)
        return;
    const VmConfig &c = m_vm->config();
    const QFontMetrics fm(m_name->font());
    m_name->setText(fm.elidedText(c.name, Qt::ElideRight, kThumbW));
    m_name->setToolTip(c.name);
    m_thumb->setPixmap(thumbnailPixmap());

    QString stateKey;
    QString stateText;
    if (!m_vm->isAvailable()) {
        stateKey = QStringLiteral("unavailable");
        stateText = QStringLiteral("Unavailable");
        m_sub->setText(m_vm->unavailableShort());
        setToolTip(m_vm->unavailableReason());
    } else {
        const QString os = c.osLabel.isEmpty() ? OsDb::profile(c.osId).name : c.osLabel;
        m_sub->setText(QStringLiteral("%1\n%2 vCPU · %3 GiB · %4")
                           .arg(os)
                           .arg(c.vcpus())
                           .arg(c.memoryMiB / 1024.0, 0, 'f', c.memoryMiB % 1024 ? 1 : 0)
                           .arg(c.graphics == QLatin1String("venus") ? QStringLiteral("Venus")
                                : c.graphics == QLatin1String("virgl") ? QStringLiteral("VirGL")
                                                                       : QStringLiteral("2D")));
        setToolTip(QString());
        switch (m_vm->state()) {
        case Vm::State::Running: stateKey = QStringLiteral("running"); break;
        case Vm::State::Paused: stateKey = QStringLiteral("paused"); break;
        case Vm::State::Starting: stateKey = QStringLiteral("starting"); break;
        case Vm::State::Stopping: stateKey = QStringLiteral("stopping"); break;
        default: stateKey = QStringLiteral("stopped"); break;
        }
        stateText = Vm::stateLabel(m_vm->state());
    }
    m_status->setText(QStringLiteral("<span style='color:%1; font-size:14pt;'>●</span>&nbsp;%2")
                          .arg(Theme::stateColor(stateKey).name(), stateText.toHtmlEscaped()));

    const bool active = m_vm->isActive();
    m_primary->setEnabled(m_vm->isAvailable() || active);
    if (active) {
        m_primary->setText(QStringLiteral("Open"));
        m_primary->setIcon(QIcon::fromTheme(QStringLiteral("window-new"), QIcon::fromTheme(QStringLiteral("view-fullscreen"))));
    } else {
        m_primary->setText(QStringLiteral("Start"));
        m_primary->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
    }
    m_primary->setToolTip(m_vm->isAvailable() ? QString() : m_vm->unavailableReason());
    update();
}

void VmCard::setSelected(bool on)
{
    if (m_selected == on)
        return;
    m_selected = on;
    setProperty("selected", on);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void VmCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if (Theme::instance()->isCustom()) {
        QStyleOption opt;
        opt.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
        return;
    }
    // Native look: a soft rounded card in palette colours.
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette &pal = palette();
    QColor fill = pal.color(QPalette::Base);
    if (m_hover)
        fill = pal.color(QPalette::AlternateBase);
    QColor border = pal.color(QPalette::Mid);
    border.setAlpha(110);
    QPen pen(m_selected ? pal.color(QPalette::Highlight) : border, m_selected ? 2.0 : 1.0);
    p.setPen(pen);
    p.setBrush(fill);
    p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 12, 12);
}

void VmCard::mousePressEvent(QMouseEvent *e)
{
    Q_EMIT clicked(this);
    QWidget::mousePressEvent(e);
}

void VmCard::mouseDoubleClickEvent(QMouseEvent *)
{
    Q_EMIT activated(this);
}

void VmCard::contextMenuEvent(QContextMenuEvent *e)
{
    Q_EMIT clicked(this);
    Q_EMIT contextMenuRequested(this, e->globalPos());
}

void VmCard::enterEvent(QEnterEvent *e)
{
    m_hover = true;
    update();
    QWidget::enterEvent(e);
}

void VmCard::leaveEvent(QEvent *e)
{
    m_hover = false;
    update();
    QWidget::leaveEvent(e);
}

void VmCard::dragEnterEvent(QDragEnterEvent *e)
{
    if (m_vm && m_vm->state() == Vm::State::Running && e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void VmCard::dropEvent(QDropEvent *e)
{
    QStringList files;
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile())
            files << u.toLocalFile();
    if (!files.isEmpty())
        Q_EMIT filesDropped(this, files);
}
