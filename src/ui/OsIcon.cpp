#include "OsIcon.h"
#include "core/OsDb.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace OsIcon {

static QIcon themeLogo(const QString &osId)
{
    QString name = osId;
    if (osId == QLatin1String("windows") || osId == QLatin1String("windows7"))
        name = QStringLiteral("windows");
    else if (osId == QLatin1String("popos"))
        name = QStringLiteral("pop-os");
    else if (osId == QLatin1String("linux") || osId == QLatin1String("other"))
        return {};
    return QIcon::fromTheme(QStringLiteral("distributor-logo-") + name);
}

QPixmap badge(const QString &osId, int size, qreal dpr)
{
    const OsProfile &p = OsDb::profile(osId);
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);

    const QIcon logo = themeLogo(osId);
    if (!logo.isNull()) {
        logo.paint(&g, QRect(0, 0, size, size));
        return pm;
    }

    QLinearGradient grad(0, 0, size, size);
    grad.setColorAt(0, p.color.lighter(115));
    grad.setColorAt(1, p.color.darker(125));
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, size, size), size * 0.24, size * 0.24);
    g.fillPath(path, grad);

    QFont f;
    f.setBold(true);
    f.setPixelSize(int(size * (p.badge.size() > 1 ? 0.38 : 0.52)));
    g.setFont(f);
    g.setPen(Qt::white);
    g.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, p.badge);
    return pm;
}

QIcon icon(const QString &osId)
{
    const QIcon logo = themeLogo(osId);
    if (!logo.isNull())
        return logo;
    QIcon i;
    for (int s : {16, 22, 32, 48, 64})
        i.addPixmap(badge(osId, s, 1.0));
    return i;
}

QPixmap placeholder(const QString &osId, const QSize &size, qreal dpr)
{
    const OsProfile &p = OsDb::profile(osId);
    QPixmap pm(size * dpr);
    pm.setDevicePixelRatio(dpr);
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);
    QLinearGradient grad(0, 0, size.width(), size.height());
    grad.setColorAt(0, p.color.darker(160));
    grad.setColorAt(1, p.color.darker(320));
    g.fillRect(QRect(QPoint(0, 0), size), grad);
    const int b = qMin(size.width(), size.height()) * 45 / 100;
    g.drawPixmap(QPoint((size.width() - b) / 2, (size.height() - b) / 2), badge(osId, b, dpr));
    return pm;
}

} // namespace OsIcon
