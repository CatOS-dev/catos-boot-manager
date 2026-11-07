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
    // Deploy installs on a running or mounted system; Repair additionally
    // offers temporarily mounting a system that is not running.
    enum class Mode { Deploy, Repair };
    explicit RepairPage(LoaderKind kind, QWidget *parent = nullptr, Mode mode = Mode::Deploy);
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
    void updateActionText();
    void updateRegistrationDefault();
    LoaderKind kind_;
    Mode mode_ = Mode::Deploy;
    QJsonArray kernels_;
    QString binary_;
    QString architecture_;
    QString bootloaderId_;
    QString installationId_;
    QString destinationRoot_;
    QMap<QString, QString> destinations_;
    QLabel *installedKernels_ = nullptr;
    QLineEdit *imageLabel_ = nullptr;
    QLineEdit *cmdline_ = nullptr;
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
    bool uefi_ = false;
    bool registered_ = false;
    bool registerChosen_ = false;
    QPushButton *deploy_ = nullptr;
    QLabel *actionHelp_ = nullptr;
    QPushButton *addEntry_ = nullptr;
};
