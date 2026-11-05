#pragma once

#include "model/BootTransaction.h"
#include "model/BootTypes.h"
#include <QWidget>
#include <QMap>

class LoaderSelector;
class RepairPage;
class QStackedWidget;
class QComboBox;
class QLabel;
class QPushButton;

class MethodsPage : public QWidget {
    Q_OBJECT
public:
    explicit MethodsPage(QWidget *parent = nullptr);
    void updateSnapshot(const BootSnapshot &snapshot);
    void updateTransaction(const BootTransaction &transaction);
    void selectMethod(LoaderKind kind);
    LoaderSelector *selector() const { return selector_; }

signals:
    void methodChanged(LoaderKind kind);
    void operationRequested(const BootOperation &operation);
    void installationRequested(const BootOperation &operation);

private:
    void showInstance();
    void updateActions();
    void queueSelection(bool once);
    const FirmwareEntry *selectedEntry() const;

    struct Panel {
        QWidget *page = nullptr;
        QWidget *firmware = nullptr;
        RepairPage *repair = nullptr;
        QPushButton *installPackages = nullptr;
        QPushButton *addEntry = nullptr;
        QLabel *packages = nullptr;
        QLabel *details = nullptr;
        QLabel *status = nullptr;
        QComboBox *entries = nullptr;
        QPushButton *setDefault = nullptr;
        QPushButton *once = nullptr;
    };
    QMap<int, Panel> panels_;
    Panel *current_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    BootSnapshot snapshot_;
    BootTransaction transaction_;
    LoaderSelector *selector_ = nullptr;
};
