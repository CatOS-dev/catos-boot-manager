#pragma once

#include "model/BootTransaction.h"
#include "model/BootTypes.h"

#include <QMainWindow>
#include <functional>

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
class BootScan;
class MethodsPage;
class SecurityPage;
class RecoveryPage;

class BootManagerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit BootManagerWindow(BootSession *session, QWidget *parent = nullptr,
                               std::function<BootSnapshot()> localInspect = {});
    // Shows boot information without administrator access. Detection runs as
    // the desktop user and changes can be prepared but not applied until the
    // user authorizes.
    void setReadOnly(bool readOnly);
    bool isReadOnly() const { return readOnly_; }

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    bool hasChanges() const;
    bool isBusy() const;
    void showSnapshot(const BootSnapshot &snapshot);
    void refresh();
    void rescan(bool preserveDrafts = false);
    void queue(const BootOperation &operation);
    void updateFooter();
    void applyTransaction();
    void runMokRequest(const QJsonObject &request, const QString &description);

    bool updatingSnapshot_ = false;
    bool readOnly_ = false;
    QString scanStage_;
    BootScan *localScan_ = nullptr;
    QFrame *readOnlyBanner_ = nullptr;
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
    RecoveryPage *recovery_ = nullptr;
    MenuPage *menu_ = nullptr;
    SettingsPage *settings_ = nullptr;
    FirmwarePage *firmware_ = nullptr;
    PendingPage *pendingPage_ = nullptr;
    QFrame *footer_ = nullptr;
    QLabel *pending_ = nullptr;
    QPushButton *apply_ = nullptr;
    QPushButton *discard_ = nullptr;
};
