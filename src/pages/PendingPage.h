#pragma once

#include "model/BootTransaction.h"
#include <QWidget>

class QListWidget;
class QLabel;
class QPushButton;

class PendingPage : public QWidget {
    Q_OBJECT
public:
    explicit PendingPage(QWidget *parent = nullptr);
    void updateTransaction(const BootTransaction &transaction);

signals:
    void removalRequested(const QString &key);

private:
    QListWidget *items_ = nullptr;
    QLabel *details_ = nullptr;
    QPushButton *remove_ = nullptr;
};
