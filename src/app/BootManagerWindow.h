#pragma once

#include "model/BootTransaction.h"
#include "model/BootTypes.h"

#include <QMainWindow>

class QFrame;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class OverviewPage;
class MenuPage;
class SettingsPage;
class FirmwarePage;
class RepairPage;
class PendingPage;
class BootScan;
class MethodsPage;
class SecurityPage;

class BootManagerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit BootManagerWindow(QWidget *parent = nullptr);

private:
    void refresh();
    void queue(const BootOperation &operation);
    void updateFooter();
    void applyTransaction();

    BootSnapshot snapshot_;
    BootScan *scan_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
    BootTransaction transaction_;
    QListWidget *navigation_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    OverviewPage *overview_ = nullptr;
    MethodsPage *methods_ = nullptr;
    SecurityPage *security_ = nullptr;
    MenuPage *menu_ = nullptr;
    SettingsPage *settings_ = nullptr;
    FirmwarePage *firmware_ = nullptr;
    RepairPage *repair_ = nullptr;
    PendingPage *pendingPage_ = nullptr;
    QFrame *footer_ = nullptr;
    QLabel *pending_ = nullptr;
    QPushButton *apply_ = nullptr;
    QPushButton *discard_ = nullptr;
};
