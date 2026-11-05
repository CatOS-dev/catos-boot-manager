#pragma once

#include "model/BootTransaction.h"
#include "model/BootTypes.h"

#include <QWidget>
#include <QHash>

class LoaderSelector;
class QFormLayout;
class QLineEdit;
class QLabel;
class QPushButton;
class QVBoxLayout;

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr, bool embedded = false);
    void updateSnapshot(const BootSnapshot &snapshot, bool preserveDrafts = false);
    void reflectOperation(const BootOperation &operation);
    void discardOperation(const BootOperation &operation);
    void selectMethod(LoaderKind kind);
    LoaderSelector *selector() const { return selector_; }

signals:
    void operationRequested(const BootOperation &operation);
    void methodChanged(LoaderKind kind);

private:
    void showSettings();
    void stageChanges();

    BootSnapshot snapshot_;
    QList<BootInstallation> manageable_;
    LoaderSelector *selector_ = nullptr;
    QLabel *location_ = nullptr;
    QPushButton *queue_ = nullptr;
    QFormLayout *fields_ = nullptr;
    QList<QWidget *> edits_;
    QStringList baseline_;
    struct Draft {
        QStringList baseline;
        QStringList values;
    };
    QHash<QString, Draft> drafts_;
    QVBoxLayout *layout_ = nullptr;
};
