#pragma once

#include "model/BootTransaction.h"
#include "model/BootTypes.h"

#include <QWidget>

class QPushButton;

class RepairPage : public QWidget {
    Q_OBJECT
public:
    explicit RepairPage(QWidget *parent = nullptr);
    void updateSnapshot(const BootSnapshot &snapshot);

signals:
    void operationRequested(const BootOperation &operation);

private:
    QStringList systemdEsp_;
    QPushButton *grub_ = nullptr;
    QPushButton *limine_ = nullptr;
    QPushButton *systemd_ = nullptr;
};
