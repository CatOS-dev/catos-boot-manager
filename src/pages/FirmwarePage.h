#pragma once

#include "model/BootTransaction.h"
#include "model/BootTypes.h"

#include <QWidget>

class QTableWidget;
class QPushButton;
class QLabel;

class FirmwarePage : public QWidget {
    Q_OBJECT
public:
    explicit FirmwarePage(QWidget *parent = nullptr);
    void updateSnapshot(const BootSnapshot &snapshot);

signals:
    void operationRequested(const BootOperation &operation);

private:
    void redraw();
    void moveSelected(int delta);
    void queueOrder();
    void queueNext();
    void queueToggle();
    void queueRemoval();
    void queueCreation();
    QString selectedId() const;

    BootSnapshot snapshot_;
    QStringList displayOrder_;
    QTableWidget *table_ = nullptr;
    QLabel *detail_ = nullptr;
    QList<QPushButton *> actions_;
};
