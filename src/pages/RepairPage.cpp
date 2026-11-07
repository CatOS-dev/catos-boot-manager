#include "RepairPage.h"
#include "services/BootRepair.h"
#include "services/BootDeploy.h"
#include "widgets/BootUi.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

RepairPage::RepairPage(LoaderKind kind, QWidget *parent, Mode mode) : QWidget(parent), kind_(kind), mode_(mode)
{
    loader_ = BootDeploy::loaderId(kind);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    auto *content = this;
    form_ = new QFormLayout;
    auto *form = form_;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setSpacing(10);
    rootTarget_ = new QComboBox(content);
    rootTarget_->setObjectName(QStringLiteral("repairRootTarget"));
    BootUi::enableCopy(rootTarget_);
    firmwareMode_ = new QComboBox(content);
    firmwareMode_->setObjectName(QStringLiteral("repairFirmwareMode"));
    BootUi::enableCopy(firmwareMode_);
    espTarget_ = new QComboBox(content);
    BootUi::enableCopy(espTarget_);
    espTarget_->setObjectName("installationDirectory");
    espTarget_->setEditable(true);
    diskTarget_ = new QComboBox(content);
    BootUi::enableCopy(diskTarget_);
    espMountPoint_ = new QComboBox(content);
    BootUi::enableCopy(espMountPoint_);
    rootSubvolume_ = new QLineEdit(QStringLiteral("@"), content);
    rootSubvolume_->setPlaceholderText(tr("Btrfs subvolume, e.g. @ (leave empty for top level)"));
    espMountPoint_->addItem(QStringLiteral("/boot/efi"));
    espMountPoint_->addItem(QStringLiteral("/efi"));
    espMountPoint_->addItem(QStringLiteral("/boot"));
    rootTarget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    espTarget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    diskTarget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    espMountPoint_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    firmwareMode_->addItem(QStringLiteral("UEFI"), QStringLiteral("uefi"));
    if (kind == LoaderKind::Grub) firmwareMode_->addItem(QStringLiteral("BIOS / Legacy"), QStringLiteral("bios"));
    form->addRow(tr("Target system:"), rootTarget_);
    form->addRow(tr("Firmware mode:"), firmwareMode_);
    form->addRow(tr("Installation directory:"), espTarget_);
    form->addRow(tr("BIOS disk:"), diskTarget_);
    form->addRow(tr("Btrfs root subvolume:"), rootSubvolume_);
    form->addRow(tr("EFI mount in target:"), espMountPoint_);
    imageLabel_ = new QLineEdit(loaderName(kind), content);
    imageLabel_->setObjectName("deploymentLabel");
    cmdline_ = new QLineEdit(content); cmdline_->setObjectName("deploymentParameters");
    installedKernels_ = BootUi::muted({}, content);
    installedKernels_->setObjectName("deploymentInstalledKernels");
    installedKernels_->setTextFormat(Qt::PlainText);
    form->addRow(tr("Boot entry name:"), imageLabel_);
    form->addRow(tr("Kernel parameters:"), cmdline_);
    form->addRow(tr("Installed kernels:"), installedKernels_);
    layout->addLayout(form);
    for (int row = 0; row < form->rowCount(); ++row) {
        auto *label = qobject_cast<QLabel *>(form->itemAt(row, QFormLayout::LabelRole)->widget());
        label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    }

    registerFirmware_ = new QCheckBox(tr("Register a UEFI firmware entry"), content);
    registerFirmware_->setChecked(false);
    registerFirmware_->setObjectName(QStringLiteral("registerFirmwareEntry"));
    if (kind == LoaderKind::Grub || kind == LoaderKind::SystemdBoot)
        registerFirmware_->setToolTip(tr("%1 places the entry it registers first in the UEFI boot order, making it the default.").arg(loaderName(kind)));
    else
        registerFirmware_->setToolTip(tr("The new entry is added to the end of the UEFI boot order; the default does not change."));
    connect(registerFirmware_, &QCheckBox::clicked, this, [this] { registerChosen_ = true; });
    layout->addWidget(registerFirmware_);
    installFallback_ = new QCheckBox(tr("Replace the removable EFI fallback loader (advanced)"), content);
    installFallback_->setChecked(false);
    layout->addWidget(installFallback_);
    actionHelp_ = BootUi::muted({}, content);
    actionHelp_->setObjectName(QStringLiteral("deploymentActionHelp"));
    actionHelp_->setTextFormat(Qt::PlainText);
    layout->addWidget(actionHelp_);
    auto *actions = new QHBoxLayout;
    // GRUB, Limine, rEFInd, UKI and EFI Stub run the same steps for a new and
    // an existing installation, so one action covers both. Only systemd-boot
    // has a distinct update command (bootctl update).
    deploy_ = new QPushButton(tr("Deploy boot loader"), content);
    deploy_->setObjectName(QStringLiteral("deployBootLoader"));
    actions->addWidget(deploy_);
    update_ = new QPushButton(tr("Update boot loader"), content);
    update_->setObjectName("updateBootLoader");
    update_->setVisible(kind == LoaderKind::SystemdBoot);
    actions->addWidget(update_);
    addEntry_ = new QPushButton(tr("Add boot entry"), content);
    addEntry_->setObjectName(QStringLiteral("methodAddBootEntry"));
    actions->addWidget(addEntry_);
    actions->addStretch();
    layout->addLayout(actions);
    connect(deploy_, &QPushButton::clicked, this, [this] {
        install(mode_ == Mode::Repair || !binary_.isEmpty() ? QStringLiteral("repair") : QStringLiteral("install"));
    });
    connect(update_, &QPushButton::clicked, this, [this] { install(QStringLiteral("update")); });
    connect(rootTarget_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RepairPage::updateRepairChoices);
    connect(firmwareMode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RepairPage::updateRepairChoices);
    connect(espTarget_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RepairPage::updateRepairAvailability);
    connect(espTarget_, &QComboBox::editTextChanged, this, &RepairPage::updateRepairAvailability);
    connect(diskTarget_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RepairPage::updateRepairAvailability);
}

void RepairPage::updateTargets(const BootSnapshot &snapshot)
{
    kernels_ = snapshot.kernels;
    uefi_ = snapshot.uefi;
    devices_ = snapshot.devices;
    espMounts_ = snapshot.espMounts;
    mountSources_ = snapshot.mountSources;
    firmwareMode_->setCurrentIndex(!snapshot.uefi && loader_ == QLatin1String("grub") ? 1 : 0);
    const QString previousRoot = rootTarget_->currentData().toString();
    rootTarget_->blockSignals(true);
    rootTarget_->clear();
    for (const auto &root : snapshot.repairRoots)
        rootTarget_->addItem(root == QLatin1String("/") ? tr("Current system (/)" ) : root, root);
    for (const auto &device : devices_) {
        if (mode_ == Mode::Repair && device.type == QLatin1String("part")
            && QStringList{QStringLiteral("btrfs"), QStringLiteral("ext4"),
                           QStringLiteral("xfs"), QStringLiteral("f2fs")}.contains(device.filesystem))
            rootTarget_->addItem(tr("Mount for repair: %1").arg(device.label()),
                                 QStringLiteral("auto:") + device.path);
    }
    const int previousIndex = rootTarget_->findData(previousRoot);
    rootTarget_->setCurrentIndex(previousIndex >= 0 ? previousIndex : 0);
    rootTarget_->blockSignals(false);
    updateRepairChoices();
}

void RepairPage::setPackagesReady(bool ready)
{
    packagesReady_ = ready;
    updateRepairAvailability();
}

void RepairPage::selectDestination(const QString &esp)
{
    const int destination = espTarget_->findData(esp);
    if (destination >= 0) espTarget_->setCurrentIndex(destination);
    else if (!esp.isEmpty()) espTarget_->setEditText(esp);
}

void RepairPage::updateRepairChoices()
{
    const QString root = rootTarget_->currentData().toString();
    const bool autoRoot = root.startsWith(QLatin1String("auto:"));
    const QString loader = loader_;
    const bool bios = firmwareMode_->currentData() == QLatin1String("bios");
    const bool limine = loader == QLatin1String("limine");
    if (!destinationRoot_.isEmpty()) destinations_.insert(destinationRoot_, espTarget_->currentText());
    const QString previousEsp = destinations_.value(root);
    destinationRoot_ = root;
    espTarget_->blockSignals(true);
    espTarget_->clear();
    const bool directory = BootDeploy::directImage(kind_) || kind_ == LoaderKind::Refind;
    espTarget_->setEditable(directory && !autoRoot);
    if (!bios && autoRoot) {
        espTarget_->addItem(tr("Select a target EFI system partition"), QString());
        static const QString espPartitionType = QStringLiteral("c12a7328-f81f-11d2-ba4b-00a0c93ec93b");
        for (const auto &device : devices_) {
            if (device.type == QLatin1String("part")
                && device.filesystem.compare(QLatin1String("vfat"), Qt::CaseInsensitive) == 0
                && device.partuuid.compare(espPartitionType, Qt::CaseInsensitive) == 0)
                espTarget_->addItem(device.label(), device.path);
        }
    } else if (directory && !bios) {
        espTarget_->addItem(root == QLatin1String("/") ? QStringLiteral("/boot/efi") : root + QStringLiteral("/boot/efi"));
        for (const auto &mount : espMounts_)
            if ((root == QLatin1String("/") || mount.startsWith(root + '/')) && espTarget_->findText(mount) < 0) espTarget_->addItem(mount);
    } else if (limine && !bios) {
        espTarget_->addItem(tr("Current system EFI partition"), QString());
    } else if (!limine && !bios) {
        espTarget_->addItem(tr("Select a mounted EFI partition"), QString());
        for (const auto &esp : espMounts_) {
            if (root != QLatin1String("/") && !esp.startsWith(root + QLatin1Char('/'))) continue;
            espTarget_->addItem(esp, esp);
        }
    }
    const int espIndex = espTarget_->findText(previousEsp);
    espTarget_->setCurrentIndex(espIndex >= 0 ? espIndex : 0);
    if (directory && !autoRoot && previousEsp.startsWith(root == QLatin1String("/") ? QStringLiteral("/") : root + '/')) espTarget_->setEditText(previousEsp);
    espTarget_->blockSignals(false);

    const QString previousDisk = diskTarget_->currentData().toString();
    diskTarget_->blockSignals(true);
    diskTarget_->clear();
    diskTarget_->addItem(tr("Select a whole physical disk"), QString());
    for (const auto &device : devices_) {
        if (device.type == QLatin1String("disk") && !device.path.startsWith(QLatin1String("/dev/zram"))
            && !device.path.startsWith(QLatin1String("/dev/loop")))
            diskTarget_->addItem(device.label(), device.path);
    }
    const int diskIndex = diskTarget_->findData(previousDisk);
    diskTarget_->setCurrentIndex(diskIndex >= 0 ? diskIndex : 0);
    diskTarget_->blockSignals(false);

    form_->setRowVisible(firmwareMode_, kind_ == LoaderKind::Grub);
    form_->setRowVisible(espTarget_, !bios);
    form_->setRowVisible(diskTarget_, bios);
    form_->setRowVisible(espMountPoint_, !bios && autoRoot);
    espTarget_->setEnabled(autoRoot || !limine);
    diskTarget_->setEnabled(bios);
    espMountPoint_->setEnabled(!bios && autoRoot);
    const auto *rootDevice = autoRoot ? BootDevices::byPath(devices_, root.mid(5)) : nullptr;
    const bool btrfs = rootDevice && rootDevice->filesystem == QLatin1String("btrfs");
    form_->setRowVisible(rootSubvolume_, btrfs);
    rootSubvolume_->setEnabled(btrfs);
    registerFirmware_->setVisible(!bios);
    registerFirmware_->setEnabled(!bios && uefi_);
    updateRegistrationDefault();
    installFallback_->setVisible(!bios && loader != QLatin1String("systemd-boot"));
    installFallback_->setEnabled(!bios && loader != QLatin1String("systemd-boot"));
    if (!installFallback_->isEnabled()) installFallback_->setChecked(false);
    const bool image = BootDeploy::directImage(kind_);
    const bool refind = kind_ == LoaderKind::Refind;
    form_->setRowVisible(imageLabel_, refind);
    form_->setRowVisible(cmdline_, refind);
    form_->setRowVisible(installedKernels_, image && !autoRoot);
    if (kind_ == LoaderKind::EfiStub) { installFallback_->hide(); installFallback_->setChecked(false); }
    updateKernels();
    updateRepairAvailability();
}

void RepairPage::updateRepairAvailability()
{
    const QString root = rootTarget_->currentData().toString();
    const bool bios = firmwareMode_->currentData() == QLatin1String("bios");
    const bool limine = loader_ == QLatin1String("limine");
    const bool automatic = root.startsWith(QLatin1String("auto:"));
    const bool supported = !bios || loader_ == QLatin1String("grub");
    const bool chosen = bios ? !diskTarget_->currentData().toString().isEmpty()
        : (limine && !automatic) || (espTarget_->isEditable() ? espTarget_->currentText().startsWith('/') : !espTarget_->currentData().toString().isEmpty());
    const bool ready = !root.isEmpty() && supported && chosen && (root != QLatin1String("/") || packagesReady_);
    const QString reason = root.isEmpty() ? tr("Choose a target system.")
        : !supported ? tr("Only GRUB can be installed for BIOS firmware.")
        : !chosen ? (bios ? tr("Choose the disk to install GRUB on.") : automatic ? tr("Choose the EFI system partition of the target system.")
                          : tr("Choose an installation directory."))
        : tr("Install the required software packages for this method first.");
    BootUi::setAvailable(deploy_, ready, reason);
    BootUi::setAvailable(update_, ready && !binary_.isEmpty(), ready ? tr("systemd-boot is not installed at this location yet.") : reason,
        tr("Replace the installed systemd-boot program with the packaged version (bootctl update)."));
    updateActionText();
}

void RepairPage::updateActionText()
{
    const bool existing = !binary_.isEmpty();
    deploy_->setText(mode_ == Mode::Repair ? tr("Repair boot loader")
        : existing ? tr("Reinstall boot loader") : tr("Deploy boot loader"));
    QString help;
    switch (kind_) {
    case LoaderKind::Grub:
        help = tr("Runs grub-install and regenerates grub.cfg. /etc/default/grub is kept.");
        break;
    case LoaderKind::Limine:
        help = tr("Runs limine-install and regenerates the Limine menu. Existing menu settings are kept.");
        break;
    case LoaderKind::Refind:
        help = tr("Copies the packaged rEFInd program, drivers and icons. An existing refind.conf is kept.");
        break;
    case LoaderKind::SystemdBoot:
        help = existing ? tr("Reinstall writes the program and loader.conf defaults again; Update only replaces the program.")
                        : tr("Runs bootctl install. Boot entries are maintained by kernel-install.");
        break;
    case LoaderKind::Uki:
        help = tr("Builds a unified kernel image for every installed kernel with catos-firmware-boot.");
        break;
    case LoaderKind::EfiStub:
        help = tr("Deploys every installed kernel with its initramfs using catos-firmware-boot.");
        break;
    }
    actionHelp_->setText(help);
}

void RepairPage::install(const QString &action)
{
    const QString root = rootTarget_->currentData().toString();
    const bool autoRoot = root.startsWith(QLatin1String("auto:"));
    const QString loader = loader_;
    const QString mode = firmwareMode_->currentData().toString();
    const bool bios = mode == QLatin1String("bios");
    const QString selectedEsp = bios ? QString() : (espTarget_->isEditable() ? espTarget_->currentText().trimmed() : espTarget_->currentData().toString());
    const QString esp = autoRoot ? QString() : selectedEsp;
    const QString rootDevice = autoRoot ? root.mid(5) : QString();
    const QString espDevice = autoRoot ? selectedEsp : QString();
    const QString disk = bios ? diskTarget_->currentData().toString() : QString();
    const bool registerEntry = !bios && registerFirmware_->isChecked();
    const bool fallback = !bios && installFallback_->isChecked();
    QJsonObject data{{QStringLiteral("root"), root}, {QStringLiteral("loader"), loader},
        {QStringLiteral("mode"), mode}, {QStringLiteral("esp"), esp},
        {QStringLiteral("disk"), disk}, {QStringLiteral("register"), registerEntry},
        {QStringLiteral("fallback"), fallback},
        {QStringLiteral("rootDevice"), rootDevice},
        {QStringLiteral("rootSubvolume"), autoRoot && rootSubvolume_->isEnabled()
            ? rootSubvolume_->text().trimmed() : QString()},
        {QStringLiteral("espDevice"), espDevice},
        {QStringLiteral("espRelative"), autoRoot && !bios ? espMountPoint_->currentText() : QString()},
        {QStringLiteral("rootSource"), autoRoot ? QString() : mountSources_.value(root)},
        {QStringLiteral("espSource"), mountSources_.value(esp)},
        {QStringLiteral("architecture"), architecture_},
        {QStringLiteral("bootloaderId"), bootloaderId_}};
    data.insert("action", action);
    // Human-readable description of the device a high-risk write affects.
    const QString device = bios ? disk : autoRoot ? espDevice : mountSources_.value(esp);
    if (const auto *known = BootDevices::byPath(devices_, device)) data.insert("targetLabel", known->label());
    else if (!device.isEmpty()) data.insert("targetLabel", device);
    if (kind_ == LoaderKind::Refind) {
        data.insert("label", imageLabel_->text().trimmed());
        data.insert("cmdline", cmdline_->text().trimmed());
    }
    const QString target = bios ? disk : (autoRoot ? espDevice + QStringLiteral(" → ")
        + espMountPoint_->currentText() : (esp.isEmpty() ? tr("configured ESP") : esp));
    const QString details = (action == QLatin1String("repair") ? tr("Repair %1 (%2) in %3, destination: %4%5%6") : action == QLatin1String("update") ? tr("Update %1 (%2) in %3, destination: %4%5%6") : tr("Deploy %1 (%2) in %3, destination: %4%5%6"))
        .arg(loader, mode.toUpper(), root, target,
             registerEntry ? tr(" · register UEFI entry") : QString(),
             fallback ? tr(" · overwrite EFI fallback") : QString());
    const BootOperation operation{QStringLiteral("repair-install"),
        loader + QLatin1Char(':') + mode + QLatin1Char(':') + root + QLatin1Char(':') + target,
        QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)),
        BootDevices::fingerprint(devices_), details};
    emit installationRequested(operation);
}

void RepairPage::updateKernels()
{
    QStringList kernels;
    const auto root = rootTarget_->currentData().toString();
    for (const auto &value : kernels_) {
        const auto kernel = value.toObject();
        if (kernel.value("root").toString() == root)
            kernels.append(kernel.value("name").toString() + " · " + kernel.value("version").toString());
    }
    installedKernels_->setText(kernels.isEmpty() ? tr("No installed kernel found") : kernels.join('\n'));
}

void RepairPage::selectInstallation(const BootInstallation *installation)
{
    const auto id = installation ? installation->id : QString();
    const bool changed = id != installationId_;
    installationId_ = id;
    binary_ = installation ? installation->binary : QString();
    architecture_ = installation ? installation->architecture : QString();
    bootloaderId_.clear();
    if (installation && !installation->binary.isEmpty()) {
        const auto parts = installation->binary.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (int i = 0; i + 1 < parts.size(); ++i)
            if (parts.at(i).compare(QLatin1String("EFI"), Qt::CaseInsensitive) == 0) { bootloaderId_ = parts.at(i + 1); break; }
    }
    registered_ = installation && installation->firmwareRegistered;
    if (changed) {
        registerChosen_ = false;
        updateRegistrationDefault();
    }
    if (installation && changed) {
        const auto root = installation->settingValues.value("root");
        const int index = rootTarget_->findData(root);
        if (index >= 0 && index != rootTarget_->currentIndex()) rootTarget_->setCurrentIndex(index);
        selectDestination(installation->esp);
    }
    updateRepairAvailability();
}

void RepairPage::updateRegistrationDefault()
{
    // Without a firmware entry a newly deployed loader cannot be started,
    // so registration is proposed unless the selected instance already has
    // one. A choice made by the user is kept until the selection changes.
    const bool bios = firmwareMode_->currentData() == QLatin1String("bios");
    if (bios || !uefi_) { registerFirmware_->setChecked(false); return; }
    if (!registerChosen_) registerFirmware_->setChecked(!registered_);
}

void RepairPage::addImageBootEntry()
{
    const bool checked = registerFirmware_->isChecked();
    registerFirmware_->setChecked(true);
    install(binary_.isEmpty() ? QStringLiteral("install") : QStringLiteral("repair"));
    registerFirmware_->setChecked(checked);
}
