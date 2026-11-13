#include "PageDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

PageDialog::PageDialog(QWidget *parent)
    : QDialog(parent)
{
    m_nav = new QListWidget(this);
    m_nav->setObjectName(QStringLiteral("pageNav"));
    m_nav->setIconSize(QSize(22, 22));
    m_nav->setFixedWidth(210);
    m_nav->setFrameShape(QFrame::NoFrame);
    m_stack = new QStackedWidget(this);
    m_buttons = new QDialogButtonBox(this);
    m_header = new QVBoxLayout;
    m_header->setContentsMargins(0, 0, 0, 0);

    auto *right = new QVBoxLayout;
    right->addLayout(m_header);
    right->addWidget(m_stack, 1);

    auto *body = new QHBoxLayout;
    body->addWidget(m_nav);
    body->addLayout(right, 1);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(body, 1);
    lay->addWidget(m_buttons);

    connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    resize(900, 640);
}

QVBoxLayout *PageDialog::addPage(const QString &title, const QIcon &icon, const QString &subtitle)
{
    new QListWidgetItem(icon, title, m_nav);

    auto *scroll = new QScrollArea(m_stack);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *lay = new QVBoxLayout(content);
    lay->setContentsMargins(18, 8, 18, 12);
    auto *t = new QLabel(title, content);
    t->setObjectName(QStringLiteral("pageTitle"));
    QFont f = t->font();
    f.setPointSizeF(f.pointSizeF() * 1.45);
    f.setBold(true);
    t->setFont(f);
    lay->addWidget(t);
    if (!subtitle.isEmpty())
        lay->addWidget(hint(subtitle, content));
    lay->addSpacing(6);
    auto *body = new QVBoxLayout;
    body->setSpacing(10);
    lay->addLayout(body);
    lay->addStretch();
    scroll->setWidget(content);
    m_stack->addWidget(scroll);
    if (m_nav->count() == 1)
        m_nav->setCurrentRow(0);
    return body;
}

void PageDialog::setCurrentPage(int index)
{
    m_nav->setCurrentRow(index);
}

QFormLayout *PageDialog::makeForm()
{
    auto *f = new QFormLayout;
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    f->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    f->setHorizontalSpacing(14);
    f->setVerticalSpacing(10);
    return f;
}

QWidget *PageDialog::hint(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("hintLabel"));
    l->setWordWrap(true);
    l->setTextFormat(Qt::AutoText);
    l->setOpenExternalLinks(true);
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, p.color(QPalette::Disabled, QPalette::WindowText));
    l->setPalette(p);
    return l;
}
