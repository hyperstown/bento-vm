// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QPointer>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QToolButton;
class Vm;

class VmCard : public QWidget
{
    Q_OBJECT
public:
    explicit VmCard(Vm *vm, QWidget *parent = nullptr);
    Vm *vm() const { return m_vm; }

    void setSelected(bool on);
    bool isSelected() const { return m_selected; }
    void refresh();

Q_SIGNALS:
    void clicked(VmCard *card);
    void activated(VmCard *card);                      // double click / primary button
    void contextMenuRequested(VmCard *card, const QPoint &globalPos);
    void filesDropped(VmCard *card, const QStringList &files);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    void fitToFont();
    QPixmap thumbnailPixmap() const;

    QPointer<Vm> m_vm;
    QLabel *m_thumb = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_sub = nullptr;
    QLabel *m_status = nullptr;
    QToolButton *m_primary = nullptr;
    QToolButton *m_menu = nullptr;
    QHBoxLayout *m_bottomRow = nullptr;
    QSize m_thumbSize{252, 142};
    bool m_selected = false;
    bool m_hover = false;
};
