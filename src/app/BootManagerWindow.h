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
class PendingPage;
class BootSession;
class MethodsPage;
class SecurityPage;

class BootManagerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit BootManagerWindow(BootSession *session, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    bool hasChanges() const;
    void refresh();
    void rescan(bool preserveDrafts = false);
    void queue(const BootOperation &operation);
    void updateFooter();
    void applyTransaction();

    bool updatingSnapshot_ = false;
    bool preserveDrafts_ = false;
    BootSnapshot snapshot_;
    BootSession *session_ = nullptr;
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
    PendingPage *pendingPage_ = nullptr;
    QFrame *footer_ = nullptr;
    QLabel *pending_ = nullptr;
    QPushButton *apply_ = nullptr;
    QPushButton *discard_ = nullptr;
};
