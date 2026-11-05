#pragma once

#include "model/BootTransaction.h"
#include "model/BootTypes.h"

#include <QWidget>
#include <QHash>

class QComboBox;
class QFormLayout;
class QLineEdit;
class QLabel;
class QPushButton;
class QVBoxLayout;

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);
    void updateSnapshot(const BootSnapshot &snapshot);
    void discardOperation(const BootOperation &operation);

signals:
    void operationRequested(const BootOperation &operation);

private:
    void showSettings();
    void stageChanges();

    BootSnapshot snapshot_;
    QList<BootInstallation> manageable_;
    QComboBox *selector_ = nullptr;
    QLabel *location_ = nullptr;
    QPushButton *queue_ = nullptr;
    QFormLayout *fields_ = nullptr;
    QList<QLineEdit *> edits_;
    QStringList baseline_;
    struct Draft {
        QStringList baseline;
        QStringList values;
    };
    QHash<QString, Draft> drafts_;
    QVBoxLayout *layout_ = nullptr;
};
