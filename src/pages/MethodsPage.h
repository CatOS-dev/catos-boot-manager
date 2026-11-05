#pragma once

#include "model/BootTransaction.h"
#include "model/BootTypes.h"
#include <QWidget>

class LoaderSelector;
class QComboBox;
class QLabel;
class QPushButton;
class QCheckBox;

class MethodsPage : public QWidget {
    Q_OBJECT
public:
    explicit MethodsPage(QWidget *parent = nullptr);
    void updateSnapshot(const BootSnapshot &snapshot);
    void updateTransaction(const BootTransaction &transaction);
    void selectMethod(LoaderKind kind);

signals:
    void methodChanged(LoaderKind kind);
    void operationRequested(const BootOperation &operation);

private:
    void showInstance();
    void updateActions();
    void queueSelection(bool once);
    void updatePackages();
    const FirmwareEntry *selectedEntry() const;

    BootSnapshot snapshot_;
    BootTransaction transaction_;
    LoaderSelector *selector_ = nullptr;
    QComboBox *entries_ = nullptr;
    QLabel *details_ = nullptr;
    QLabel *status_ = nullptr;
    QPushButton *default_ = nullptr;
    QPushButton *once_ = nullptr;
    QCheckBox *snapshots_ = nullptr;
    QCheckBox *secureBoot_ = nullptr;
    QLabel *packages_ = nullptr;
};
