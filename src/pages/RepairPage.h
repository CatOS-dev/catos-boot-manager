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
class QFormLayout;

class RepairPage : public QWidget {
    Q_OBJECT
public:
    explicit RepairPage(LoaderKind kind, QWidget *parent = nullptr);
    QPushButton *bootEntryButton() const { return addEntry_; }
    void setPackagesReady(bool ready);
    void selectDestination(const QString &esp);
    void selectInstallation(const BootInstallation *installation);
    void addImageBootEntry();
    void updateTargets(const BootSnapshot &snapshot);

signals:
    void installationRequested(const BootOperation &operation);

private:
    void updateRepairChoices();
    void updateRepairAvailability();
    void install(const QString &action);
    void updateKernels();
    LoaderKind kind_;
    QJsonArray kernels_;
    QString kernelRoot_;
    QString binary_;
    QComboBox *kernelChoice_ = nullptr;
    QLineEdit *imageId_ = nullptr;
    QLineEdit *imageLabel_ = nullptr;
    QLineEdit *kernel_ = nullptr;
    QLineEdit *initramfs_ = nullptr;
    QLineEdit *version_ = nullptr;
    QLineEdit *cmdline_ = nullptr;
    QLineEdit *signingKey_ = nullptr;
    QLineEdit *certificate_ = nullptr;
    QPushButton *update_ = nullptr;

    QList<BootDevice> devices_;
    QStringList espMounts_;
    QMap<QString, QString> mountSources_;
    QComboBox *rootTarget_ = nullptr;
    QString loader_;
    QFormLayout *form_ = nullptr;
    QComboBox *firmwareMode_ = nullptr;
    QComboBox *espTarget_ = nullptr;
    QComboBox *diskTarget_ = nullptr;
    QComboBox *espMountPoint_ = nullptr;
    QLineEdit *rootSubvolume_ = nullptr;
    QCheckBox *registerFirmware_ = nullptr;
    QCheckBox *installFallback_ = nullptr;
    bool packagesReady_ = false;
    QPushButton *deploy_ = nullptr;
    QPushButton *repair_ = nullptr;
    QPushButton *addEntry_ = nullptr;
};
