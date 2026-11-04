#pragma once

#include "model/BootTransaction.h"
#include "model/BootTypes.h"
#include "services/BootDevices.h"

#include <QWidget>

class QPushButton;
class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;

class RepairPage : public QWidget {
    Q_OBJECT
public:
    explicit RepairPage(QWidget *parent = nullptr);
    void updateSnapshot(const BootSnapshot &snapshot);

signals:
    void operationRequested(const BootOperation &operation);

private:
    void updateRepairChoices();
    void updateRepairAvailability();
    void queueInstallation();

    QStringList systemdEsp_;
    QList<BootDevice> devices_;
    QComboBox *rootTarget_ = nullptr;
    QComboBox *bootLoader_ = nullptr;
    QComboBox *firmwareMode_ = nullptr;
    QComboBox *espTarget_ = nullptr;
    QComboBox *diskTarget_ = nullptr;
    QComboBox *espMountPoint_ = nullptr;
    QLineEdit *rootSubvolume_ = nullptr;
    QCheckBox *registerFirmware_ = nullptr;
    QCheckBox *installFallback_ = nullptr;
    QLabel *repairInfo_ = nullptr;
    QPushButton *queueInstall_ = nullptr;
    QPushButton *grub_ = nullptr;
    QPushButton *limine_ = nullptr;
    QPushButton *systemd_ = nullptr;
};
