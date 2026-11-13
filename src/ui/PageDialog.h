#pragma once

#include <QDialog>

class QDialogButtonBox;
class QFormLayout;
class QListWidget;
class QStackedWidget;
class QVBoxLayout;

// Sidebar + pages dialog in the style of KDE's KPageDialog.
class PageDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PageDialog(QWidget *parent = nullptr);

    // Returns the page body layout to fill.
    QVBoxLayout *addPage(const QString &title, const QIcon &icon, const QString &subtitle = {});
    void setCurrentPage(int index);
    QDialogButtonBox *buttonBox() const { return m_buttons; }
    QVBoxLayout *headerArea() const { return m_header; }

    static QFormLayout *makeForm();
    static QWidget *hint(const QString &text, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *e) override;

    QListWidget *m_nav;
    QStackedWidget *m_stack;
    QDialogButtonBox *m_buttons;
    QVBoxLayout *m_header;
};
