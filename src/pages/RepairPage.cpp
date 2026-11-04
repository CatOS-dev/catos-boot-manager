#include "RepairPage.h"
#include "services/BootProbe.h"
#include "services/BootRepair.h"
#include "widgets/BootUi.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

RepairPage::RepairPage(QWidget *parent) : QWidget(parent)
{
    auto *layout = BootUi::page(this);
    auto *content = layout->parentWidget();
    BootUi::title(layout, content, tr("Rebuild and update"),
                  tr("Rebuild configuration or reinstall a boot loader in a chosen system and disk."));
    BootUi::section(layout, content, tr("Configuration generation"));
    grub_ = BootUi::action(layout, content, tr("Regenerate GRUB menu"),
            tr("Runs grub-mkconfig with the installed system configuration."),
            QStringLiteral("view-refresh"));
    limine_ = BootUi::action(layout, content, tr("Regenerate Limine kernel entries"),
            tr("Runs limine-mkinitcpio. Maintains UKI / initramfs and Limine entries using the existing hooks."),
            QStringLiteral("view-refresh"));
    BootUi::section(layout, content, tr("Loader program update"));
    systemd_ = BootUi::action(layout, content, tr("Update systemd-boot program"),
            tr("Runs bootctl update. This is separate from editing loader.conf."),
            QStringLiteral("system-software-update"));
    layout->addWidget(BootUi::muted(tr("Changes are reviewed and applied with the other queued operations."
                                         " Never use this page to guess or replace another operating system's loader."), content));

    BootUi::section(layout, content, tr("Install or repair a boot loader"));
    layout->addWidget(BootUi::muted(tr("Select the running system, an already mounted Linux installation,"
        " or a Linux partition to mount only after authorization."
        " For an already mounted target, also mount its EFI system partition."), content));
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setSpacing(10);
    rootTarget_ = new QComboBox(content);
    bootLoader_ = new QComboBox(content);
    firmwareMode_ = new QComboBox(content);
    espTarget_ = new QComboBox(content);
    diskTarget_ = new QComboBox(content);
    espMountPoint_ = new QComboBox(content);
    rootSubvolume_ = new QLineEdit(QStringLiteral("@"), content);
    rootSubvolume_->setPlaceholderText(tr("Btrfs subvolume, e.g. @ (leave empty for top level)"));
    espMountPoint_->addItem(QStringLiteral("/boot/efi"));
    espMountPoint_->addItem(QStringLiteral("/efi"));
    espMountPoint_->addItem(QStringLiteral("/boot"));
    rootTarget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    bootLoader_->addItem(QStringLiteral("GRUB"), QStringLiteral("grub"));
    bootLoader_->addItem(QStringLiteral("Limine"), QStringLiteral("limine"));
    bootLoader_->addItem(QStringLiteral("systemd-boot"), QStringLiteral("systemd-boot"));
    firmwareMode_->addItem(QStringLiteral("UEFI"), QStringLiteral("uefi"));
    firmwareMode_->addItem(QStringLiteral("BIOS / Legacy"), QStringLiteral("bios"));
    form->addRow(tr("Target system:"), rootTarget_);
    form->addRow(tr("Boot loader:"), bootLoader_);
    form->addRow(tr("Firmware mode:"), firmwareMode_);
    form->addRow(tr("EFI partition:"), espTarget_);
    form->addRow(tr("BIOS disk:"), diskTarget_);
    form->addRow(tr("Btrfs root subvolume:"), rootSubvolume_);
    form->addRow(tr("EFI mount in target:"), espMountPoint_);
    layout->addLayout(form);

    registerFirmware_ = new QCheckBox(tr("Register a UEFI firmware entry"), content);
    registerFirmware_->setChecked(false);
    layout->addWidget(registerFirmware_);
    installFallback_ = new QCheckBox(tr("Replace the removable EFI fallback loader (advanced)"), content);
    installFallback_->setChecked(false);
    layout->addWidget(installFallback_);
    repairInfo_ = BootUi::muted({}, content);
    layout->addWidget(repairInfo_);
    queueInstall_ = new QPushButton(tr("Queue boot loader installation / repair"), content);
    queueInstall_->setMinimumHeight(40);
    layout->addWidget(queueInstall_, 0, Qt::AlignRight);
    layout->addStretch();
    connect(grub_, &QPushButton::clicked, this, [this] {
        emit operationRequested({QStringLiteral("rebuild-grub"), QStringLiteral("grub"), {}, {},
                                 tr("Regenerate GRUB configuration")});
    });
    connect(limine_, &QPushButton::clicked, this, [this] {
        emit operationRequested({QStringLiteral("rebuild-limine"), QStringLiteral("limine"), {}, {},
                                 tr("Regenerate Limine kernel entries")});
    });
    connect(systemd_, &QPushButton::clicked, this, [this] {
        if (systemdEsp_.isEmpty()) return;
        QString esp = systemdEsp_.first();
        if (systemdEsp_.size() > 1) {
            bool ok = false;
            esp = QInputDialog::getItem(this, tr("Select EFI partition"),
                                      tr("Update the systemd-boot binary on:"), systemdEsp_,
                                      0, false, &ok);
            if (!ok) return;
        }
        emit operationRequested({QStringLiteral("update-systemd"), esp, {}, {},
                                 tr("Update systemd-boot on %1").arg(esp)});
    });
    connect(rootTarget_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RepairPage::updateRepairChoices);
    connect(bootLoader_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RepairPage::updateRepairChoices);
    connect(firmwareMode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RepairPage::updateRepairChoices);
    connect(espTarget_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RepairPage::updateRepairAvailability);
    connect(diskTarget_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RepairPage::updateRepairAvailability);
    connect(queueInstall_, &QPushButton::clicked, this, &RepairPage::queueInstallation);
}

void RepairPage::updateSnapshot(const BootSnapshot &snapshot)
{
    bool grub = false;
    bool limine = false;
    bool systemd = false;
    systemdEsp_.clear();
    for (const auto &loader : snapshot.installations) {
        grub |= loader.kind == LoaderKind::Grub;
        limine |= loader.kind == LoaderKind::Limine;
        systemd |= loader.kind == LoaderKind::SystemdBoot;
        if (loader.kind == LoaderKind::SystemdBoot && !loader.esp.isEmpty()
            && !systemdEsp_.contains(loader.esp)) systemdEsp_.append(loader.esp);
    }
    grub_->setEnabled(grub);
    limine_->setEnabled(limine);
    systemd_->setEnabled(systemd && snapshot.uefi && !systemdEsp_.isEmpty());

    devices_ = BootDevices::discover();
    const QString previousRoot = rootTarget_->currentData().toString();
    rootTarget_->blockSignals(true);
    rootTarget_->clear();
    for (const auto &root : BootRepair::mountedTargets())
        rootTarget_->addItem(root == QLatin1String("/") ? tr("Current system (/)" ) : root, root);
    for (const auto &device : devices_) {
        if (device.type == QLatin1String("part")
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

void RepairPage::updateRepairChoices()
{
    const QString root = rootTarget_->currentData().toString();
    const bool autoRoot = root.startsWith(QLatin1String("auto:"));
    const QString loader = bootLoader_->currentData().toString();
    const bool bios = firmwareMode_->currentData() == QLatin1String("bios");
    const bool limine = loader == QLatin1String("limine");
    const QString previousEsp = espTarget_->currentData().toString();
    espTarget_->blockSignals(true);
    espTarget_->clear();
    if (!bios && autoRoot) {
        espTarget_->addItem(tr("Select a target EFI system partition"), QString());
        for (const auto &device : devices_) {
            if (device.type == QLatin1String("part") && device.filesystem == QLatin1String("vfat"))
                espTarget_->addItem(device.label(), device.path);
        }
    } else if (limine && !bios) {
        espTarget_->addItem(tr("Use the ESP configured in limine-tool"), QString());
    } else if (!limine && !bios) {
        espTarget_->addItem(tr("Select a mounted EFI partition"), QString());
        for (const auto &esp : BootProbe::espMounts()) {
            if (root != QLatin1String("/") && !esp.startsWith(root + QLatin1Char('/'))) continue;
            espTarget_->addItem(esp, esp);
        }
    }
    const int espIndex = espTarget_->findData(previousEsp);
    espTarget_->setCurrentIndex(espIndex >= 0 ? espIndex : 0);
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

    espTarget_->setEnabled(!bios && (autoRoot || !limine));
    diskTarget_->setEnabled(bios);
    espMountPoint_->setEnabled(!bios && autoRoot);
    const auto *rootDevice = autoRoot ? BootDevices::byPath(devices_, root.mid(5)) : nullptr;
    rootSubvolume_->setEnabled(rootDevice && rootDevice->filesystem == QLatin1String("btrfs"));
    registerFirmware_->setEnabled(!bios);
    if (bios) registerFirmware_->setChecked(false);
    installFallback_->setEnabled(!bios && loader != QLatin1String("systemd-boot"));
    if (!installFallback_->isEnabled()) installFallback_->setChecked(false);
    const bool modeSupported = !bios || loader == QLatin1String("grub");
    updateRepairAvailability();
    repairInfo_->setText(!modeSupported
        ? tr("Only GRUB supports BIOS installation here. Switch the loader or choose UEFI.")
        : bios ? tr("BIOS installation writes boot code to the selected whole disk. Check the target carefully.")
        : limine ? tr("Limine selects the target ESP through its own configuration and hooks.")
        : autoRoot ? tr("The target filesystem and ESP will be mounted only after authorization and unmounted after repair.")
        : tr("UEFI installation writes boot files to the selected ESP. Firmware registration is optional."));
}

void RepairPage::updateRepairAvailability()
{
    const QString root = rootTarget_->currentData().toString();
    const bool bios = firmwareMode_->currentData() == QLatin1String("bios");
    const bool limine = bootLoader_->currentData() == QLatin1String("limine");
    const bool automatic = root.startsWith(QLatin1String("auto:"));
    const bool supported = !bios || bootLoader_->currentData() == QLatin1String("grub");
    const bool chosen = bios ? !diskTarget_->currentData().toString().isEmpty()
        : (limine && !automatic) || !espTarget_->currentData().toString().isEmpty();
    queueInstall_->setEnabled(!root.isEmpty() && supported && chosen);
}

void RepairPage::queueInstallation()
{
    const QString root = rootTarget_->currentData().toString();
    const bool autoRoot = root.startsWith(QLatin1String("auto:"));
    const QString loader = bootLoader_->currentData().toString();
    const QString mode = firmwareMode_->currentData().toString();
    const bool bios = mode == QLatin1String("bios");
    const QString selectedEsp = bios ? QString() : espTarget_->currentData().toString();
    const QString esp = autoRoot ? QString() : selectedEsp;
    const QString rootDevice = autoRoot ? root.mid(5) : QString();
    const QString espDevice = autoRoot ? selectedEsp : QString();
    const QString disk = bios ? diskTarget_->currentData().toString() : QString();
    const bool registerEntry = !bios && registerFirmware_->isChecked();
    const bool fallback = !bios && installFallback_->isChecked();
    const QJsonObject data{{QStringLiteral("root"), root}, {QStringLiteral("loader"), loader},
        {QStringLiteral("mode"), mode}, {QStringLiteral("esp"), esp},
        {QStringLiteral("disk"), disk}, {QStringLiteral("register"), registerEntry},
        {QStringLiteral("fallback"), fallback},
        {QStringLiteral("rootDevice"), rootDevice},
        {QStringLiteral("rootSubvolume"), autoRoot && rootSubvolume_->isEnabled()
            ? rootSubvolume_->text().trimmed() : QString()},
        {QStringLiteral("espDevice"), espDevice},
        {QStringLiteral("espRelative"), autoRoot && !bios ? espMountPoint_->currentText() : QString()},
        {QStringLiteral("rootSource"), autoRoot ? QString() : BootProbe::runReadOnly(QStringLiteral("findmnt"),
            {QStringLiteral("-rn"), QStringLiteral("-M"), root,
             QStringLiteral("-o"), QStringLiteral("SOURCE")}).trimmed()},
        {QStringLiteral("espSource"), esp.isEmpty() ? QString() : BootProbe::runReadOnly(
            QStringLiteral("findmnt"), {QStringLiteral("-rn"), QStringLiteral("-M"), esp,
                                        QStringLiteral("-o"), QStringLiteral("SOURCE")}).trimmed()}};
    const QString target = bios ? disk : (autoRoot ? espDevice + QStringLiteral(" → ")
        + espMountPoint_->currentText() : (esp.isEmpty() ? tr("configured ESP") : esp));
    const QString details = tr("Install / repair %1 (%2) in %3, destination: %4%5%6")
        .arg(loader, mode.toUpper(), root, target,
             registerEntry ? tr(" · register UEFI entry") : QString(),
             fallback ? tr(" · overwrite EFI fallback") : QString());
    const BootOperation operation{QStringLiteral("repair-install"),
        loader + QLatin1Char(':') + mode + QLatin1Char(':') + root + QLatin1Char(':') + target,
        QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)),
        BootDevices::fingerprint(devices_), details};
    QString error;
    if (!BootRepair::validate(operation, &error)) {
        QMessageBox::warning(this, tr("Cannot queue boot repair"), error);
        return;
    }
    if (QMessageBox::question(this, tr("Queue installation / repair"),
        tr("This will modify the target disk's boot files and may replace an existing boot loader.\n\n%1\n\n"
           "You can review the exact command sequence before applying.").arg(details))
        != QMessageBox::Yes) return;
    emit operationRequested(operation);
}
