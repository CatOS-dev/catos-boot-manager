#pragma once

#include "model/BootTransaction.h"
#include "model/BootTypes.h"

#include <QWidget>
#include <QSet>

class QTableWidget;
class QPushButton;
class QLabel;

class FirmwarePage : public QWidget {
    Q_OBJECT
public:
    explicit FirmwarePage(QWidget *parent = nullptr);
    void updateSnapshot(const BootSnapshot &snapshot);
    void updateTransaction(const BootTransaction &transaction);

signals:
    void operationRequested(const BootOperation &operation);

private:
    void redraw();
    void moveSelected(int delta);
    void queueOrder();
    void queueNext();
    void clearNext();
    void queueRename();
    void queueToggle();
    void queueRemoval();
    void queueCreation();
    QString selectedId() const;

    BootSnapshot snapshot_;
    QString pendingNext_;
    QHash<QString, bool> pendingEnabled_;
    QHash<QString, QString> pendingNames_;
    QSet<QString> pendingRemovals_;
    QPushButton *toggle_ = nullptr;
    QPushButton *remove_ = nullptr;
    QStringList displayOrder_;
    QTableWidget *table_ = nullptr;
    QLabel *detail_ = nullptr;
    QPushButton *clearNext_ = nullptr;
    QList<QPushButton *> actions_;
};
