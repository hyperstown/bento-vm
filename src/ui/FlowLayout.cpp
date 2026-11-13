#include "FlowLayout.h"

#include <QWidget>

FlowLayout::FlowLayout(QWidget *parent, int spacing)
    : QLayout(parent)
    , m_spacing(spacing)
{
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem *item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem *item)
{
    m_items.append(item);
}

QLayoutItem *FlowLayout::takeAt(int index)
{
    if (index < 0 || index >= m_items.size())
        return nullptr;
    return m_items.takeAt(index);
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem *item : m_items)
        size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
    const QMargins m = contentsMargins();
    const QRect area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = area.x();
    int y = area.y();
    int lineHeight = 0;
    for (QLayoutItem *item : m_items) {
        if (item->widget() && item->widget()->isHidden())
            continue;
        const QSize hint = item->sizeHint();
        int nextX = x + hint.width() + m_spacing;
        if (nextX - m_spacing > area.right() + 1 && lineHeight > 0) {
            x = area.x();
            y += lineHeight + m_spacing;
            nextX = x + hint.width() + m_spacing;
            lineHeight = 0;
        }
        if (!testOnly)
            item->setGeometry(QRect(QPoint(x, y), hint));
        x = nextX;
        lineHeight = qMax(lineHeight, hint.height());
    }
    return y + lineHeight - rect.y() + m.bottom();
}
