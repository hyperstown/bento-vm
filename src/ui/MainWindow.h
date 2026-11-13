// SPDX-FileCopyrightText: 2026 hyperstown
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QHash>
#include <QMainWindow>
#include <QPointer>

class FlowLayout;
class QAction;
class QFrame;
class QLabel;
class QLineEdit;
class QMenu;
class QStackedWidget;
class QToolButton;
class Vm;
class VmCard;
class VmWindow;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void openVm(Vm *vm);
    void openWizard(const QString &source = {});
    void showSettings(bool storagePage = false);

protected:
    void closeEvent(QCloseEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    void buildActions();
    void buildMenus();
    void buildToolbar();
    void buildCentral();
    void applyThemeLayout();

    void addCard(Vm *vm);
    void removeCard(Vm *vm);
    void sortCards();
    void applyFilter();
    void select(VmCard *card);
    Vm *selectedVm() const;
    void updateActions();
    void updateEmptyState();

    void showVmMenu(VmCard *card, const QPoint &pos);
    void fillVmMenu(QMenu *menu, Vm *vm);
    bool startVm(Vm *vm);
    void deleteVm(Vm *vm);
    void showVmSettings(Vm *vm, const QString &page = QString());
    void importFolder();
    void showAbout();

    QHash<QString, VmCard *> m_cards;
    QHash<QString, QPointer<VmWindow>> m_windows;
    QString m_selectedId;

    QStackedWidget *m_stack = nullptr;
    QWidget *m_grid = nullptr;
    FlowLayout *m_flow = nullptr;
    QLabel *m_count = nullptr;
    QLineEdit *m_search = nullptr;
    QFrame *m_kvmBanner = nullptr;
    QToolButton *m_hamburger = nullptr;
    QAction *m_hamburgerAction = nullptr;
    QMenu *m_machineMenu = nullptr;

    QAction *m_actNew = nullptr;
    QAction *m_actImportFolder = nullptr;
    QAction *m_actStart = nullptr;
    QAction *m_actOpen = nullptr;
    QAction *m_actShutdown = nullptr;
    QAction *m_actForceOff = nullptr;
    QAction *m_actVmSettings = nullptr;
    QAction *m_actDelete = nullptr;
    QAction *m_actPrefs = nullptr;
    QAction *m_actQuit = nullptr;
    QAction *m_actAbout = nullptr;
};
