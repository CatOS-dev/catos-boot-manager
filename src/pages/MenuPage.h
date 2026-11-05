#pragma once

#include "model/BootTransaction.h"
#include "model/BootTypes.h"

#include <QWidget>

class LoaderSelector;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class MenuPage : public QWidget {
    Q_OBJECT
public:
    explicit MenuPage(QWidget *parent = nullptr);
    void updateSnapshot(const BootSnapshot &snapshot);
    void selectMethod(LoaderKind kind);

signals:
    void operationRequested(const BootOperation &operation);
    void methodChanged(LoaderKind kind);

private:
    void loadMenu();
    void addEfi();
    void addKernel();
    void addUki();
    void removeEntry();
    void setDefault();
    void setTimeout();
    void showSelected(QTreeWidgetItem *item);

    BootSnapshot snapshot_;
    QList<BootInstallation> manageable_;
    LoaderSelector *selector_ = nullptr;
    QTreeWidget *menu_ = nullptr;
    QPlainTextEdit *details_ = nullptr;
    QPushButton *add_ = nullptr;
    QPushButton *kernel_ = nullptr;
    QPushButton *uki_ = nullptr;
    QPushButton *remove_ = nullptr;
    QPushButton *default_ = nullptr;
    QPushButton *timeout_ = nullptr;
    QString menuHash_;
    QString menuTimeout_;
    QString menuDefault_;
};
