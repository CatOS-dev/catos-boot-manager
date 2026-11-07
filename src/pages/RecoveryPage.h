#pragma once

#include "model/BootTransaction.h"
#include "model/BootTypes.h"

#include <QMap>
#include <QWidget>

class LoaderSelector;
class RepairPage;
class QLabel;
class QPushButton;
class QTreeWidget;
class QStackedWidget;
class QVBoxLayout;

// Repairs installed boot loaders, including systems that are not running,
// and shows what detection found wrong with the selected installation.
class RecoveryPage : public QWidget {
    Q_OBJECT
public:
    explicit RecoveryPage(QWidget *parent = nullptr);
    void updateSnapshot(const BootSnapshot &snapshot);
    LoaderSelector *selector() const { return selector_; }

signals:
    void installationRequested(const BootOperation &operation);
    void operationRequested(const BootOperation &operation);

private:
    void showInstance();
    void showBackups();
    void showBackup();

    BootSnapshot snapshot_;
    LoaderSelector *selector_ = nullptr;
    QLabel *diagnostics_ = nullptr;
    QStackedWidget *forms_ = nullptr;
    QMap<int, RepairPage *> repairs_;
    QVBoxLayout *layout_ = nullptr;
    QTreeWidget *backups_ = nullptr;
    QLabel *backupDetails_ = nullptr;
    QPushButton *restore_ = nullptr;
};
