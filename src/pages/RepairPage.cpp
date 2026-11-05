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

RepairPage::RepairPage(LoaderKind kind, QWidget *parent) : QWidget(parent), kind_(kind)
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
    form->addRow(tr("EFI partition:"), espTarget_);
    form->addRow(tr("BIOS disk:"), diskTarget_);
    form->addRow(tr("Btrfs root subvolume:"), rootSubvolume_);
    form->addRow(tr("EFI mount in target:"), espMountPoint_);
    imageId_ = new QLineEdit(QStringLiteral("catos"), content);
    imageId_->setObjectName("deploymentName");
    imageLabel_ = new QLineEdit(loaderName(kind), content);
    imageLabel_->setObjectName("deploymentLabel");
    kernelChoice_ = new QComboBox(content);
    kernelChoice_->setObjectName("deploymentKernelChoice");
    BootUi::enableCopy(kernelChoice_);
    kernel_ = new QLineEdit(content); kernel_->setObjectName("deploymentKernel");
    initramfs_ = new QLineEdit(content); initramfs_->setObjectName("deploymentInitramfs");
    version_ = new QLineEdit(content); version_->setObjectName("deploymentVersion");
    cmdline_ = new QLineEdit(content); cmdline_->setObjectName("deploymentParameters");
    signingKey_ = new QLineEdit(content); signingKey_->setObjectName("deploymentSigningKey");
    certificate_ = new QLineEdit(content); certificate_->setObjectName("deploymentCertificate");
    form->addRow(tr("Image name:"), imageId_);
    form->addRow(tr("Boot entry name:"), imageLabel_);
    form->addRow(tr("Installed kernel:"), kernelChoice_);
    form->addRow(tr("Kernel image:"), kernel_);
    form->addRow(tr("Initramfs image:"), initramfs_);
    form->addRow(tr("Kernel parameters:"), cmdline_);
    form->addRow(tr("Kernel version:"), version_);
    form->addRow(tr("Signing key:"), signingKey_);
    form->addRow(tr("Signing certificate:"), certificate_);
    layout->addLayout(form);
    for (int row = 0; row < form->rowCount(); ++row) {
        auto *label = qobject_cast<QLabel *>(form->itemAt(row, QFormLayout::LabelRole)->widget());
        label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    }

    registerFirmware_ = new QCheckBox(tr("Register a UEFI firmware entry"), content);
    registerFirmware_->setChecked(false);
    layout->addWidget(registerFirmware_);
    installFallback_ = new QCheckBox(tr("Replace the removable EFI fallback loader (advanced)"), content);
    installFallback_->setChecked(false);
    layout->addWidget(installFallback_);
    auto *actions = new QHBoxLayout;
    deploy_ = new QPushButton(tr("Deploy boot loader"), content);
    deploy_->setObjectName(QStringLiteral("deployBootLoader"));
    repair_ = new QPushButton(tr("Repair boot loader"), content);
    repair_->setObjectName(QStringLiteral("repairBootLoader"));
    actions->addWidget(deploy_);
    actions->addWidget(repair_);
    update_ = new QPushButton(tr("Update boot loader"), content);
    update_->setObjectName("updateBootLoader");
    actions->addWidget(update_);
    addEntry_ = new QPushButton(tr("Add boot entry"), content);
    addEntry_->setObjectName(QStringLiteral("methodAddBootEntry"));
    actions->addWidget(addEntry_);
    actions->addStretch();
    layout->addLayout(actions);
    connect(deploy_, &QPushButton::clicked, this, [this] { install(QStringLiteral("install")); });
    connect(repair_, &QPushButton::clicked, this, [this] { install(QStringLiteral("repair")); });
    connect(update_, &QPushButton::clicked, this, [this] { install(QStringLiteral("update")); });
    connect(kernelChoice_, &QComboBox::currentIndexChanged, this, [this] {
        const auto data = kernelChoice_->currentData().toJsonObject();
        if (data.isEmpty()) return;
        kernel_->setText(data.value("kernel").toString());
        initramfs_->setText(data.value("initramfs").toString());
        cmdline_->setText(data.value("cmdline").toString());
        version_->setText(data.value("version").toString());
    });
    connect(rootTarget_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RepairPage::updateRepairChoices);
    connect(firmwareMode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RepairPage::updateRepairChoices);
    connect(espTarget_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RepairPage::updateRepairAvailability);
    connect(diskTarget_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RepairPage::updateRepairAvailability);
}

void RepairPage::updateTargets(const BootSnapshot &snapshot)
{
    kernels_ = snapshot.kernels;
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

void RepairPage::setPackagesReady(bool ready)
{
    packagesReady_ = ready;
    updateRepairAvailability();
}

void RepairPage::selectDestination(const QString &esp)
{
    const int destination = espTarget_->findData(esp);
    if (destination >= 0) espTarget_->setCurrentIndex(destination);
}

void RepairPage::updateRepairChoices()
{
    const QString root = rootTarget_->currentData().toString();
    const bool autoRoot = root.startsWith(QLatin1String("auto:"));
    const QString loader = loader_;
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
        espTarget_->addItem(tr("Current system EFI partition"), QString());
    } else if (!limine && !bios) {
        espTarget_->addItem(tr("Select a mounted EFI partition"), QString());
        for (const auto &esp : espMounts_) {
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
    registerFirmware_->setEnabled(!bios);
    if (bios) registerFirmware_->setChecked(false);
    installFallback_->setVisible(!bios && loader != QLatin1String("systemd-boot"));
    installFallback_->setEnabled(!bios && loader != QLatin1String("systemd-boot"));
    if (!installFallback_->isEnabled()) installFallback_->setChecked(false);
    const bool image = BootDeploy::directImage(kind_);
    const bool refind = kind_ == LoaderKind::Refind;
    form_->setRowVisible(imageId_, image);
    form_->setRowVisible(imageLabel_, image || refind);
    for (auto *field : {kernel_, initramfs_}) form_->setRowVisible(field, image);
    form_->setRowVisible(kernelChoice_, image);
    form_->setRowVisible(cmdline_, image || refind);
    form_->setRowVisible(version_, kind_ == LoaderKind::Uki);
    form_->setRowVisible(signingKey_, kind_ == LoaderKind::Uki);
    form_->setRowVisible(certificate_, kind_ == LoaderKind::Uki);
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
        : (limine && !automatic) || !espTarget_->currentData().toString().isEmpty();
    const bool ready = !root.isEmpty() && supported && chosen && (root != QLatin1String("/") || packagesReady_);
    deploy_->setEnabled(ready);
    repair_->setEnabled(ready);
    update_->setEnabled(ready && !binary_.isEmpty());
}

void RepairPage::install(const QString &action)
{
    const QString root = rootTarget_->currentData().toString();
    const bool autoRoot = root.startsWith(QLatin1String("auto:"));
    const QString loader = loader_;
    const QString mode = firmwareMode_->currentData().toString();
    const bool bios = mode == QLatin1String("bios");
    const QString selectedEsp = bios ? QString() : espTarget_->currentData().toString();
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
        {QStringLiteral("espSource"), mountSources_.value(esp)}};
    data.insert("action", action);
    if (BootDeploy::directImage(kind_) || kind_ == LoaderKind::Refind) {
        data.insert("id", imageId_->text().trimmed());
        data.insert("label", imageLabel_->text().trimmed());
        data.insert("kernel", kernel_->text().trimmed());
        data.insert("initramfs", initramfs_->text().trimmed());
        data.insert("cmdline", cmdline_->text().trimmed());
        if (kind_ == LoaderKind::Uki) {
            data.insert("version", version_->text().trimmed());
            data.insert("private-key", signingKey_->text().trimmed());
            data.insert("certificate", certificate_->text().trimmed());
        }
        if (action != QLatin1String("install") && !binary_.isEmpty() && binary_.startsWith(esp + '/')) data.insert("binary", binary_);
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
    const auto previous = kernelChoice_->currentData().toJsonObject();
    const QString root = rootTarget_->currentData().toString();
    const QSignalBlocker blocker(kernelChoice_);
    kernelChoice_->clear();
    kernelChoice_->addItem(tr("Choose a kernel"), QJsonObject());
    int selected = 0;
    for (const auto &value : kernels_) {
        const auto kernel = value.toObject();
        if (kernel.value("root").toString() != root) continue;
        kernelChoice_->addItem(kernel.value("name").toString() + " · " + kernel.value("version").toString(), kernel);
        if (previous.value("root").toString() == root && previous.value("kernel") == kernel.value("kernel")) selected = kernelChoice_->count() - 1;
    }
    kernelChoice_->setCurrentIndex(selected);
    if (!kernelRoot_.isEmpty() && kernelRoot_ != root) {
        kernel_->clear(); initramfs_->clear(); cmdline_->clear(); version_->clear();
    }
    kernelRoot_ = root;
}

void RepairPage::selectInstallation(const BootInstallation *installation)
{
    binary_ = installation ? installation->binary : QString();
    updateRepairAvailability();
    if (!installation) return;
    selectDestination(installation->esp);
    if (!BootDeploy::directImage(kind_)) return;
    const auto &values = installation->settingValues;
    const int root = rootTarget_->findData(values.value("root"));
    if (!values.value("root").isEmpty() && root != rootTarget_->currentIndex()) rootTarget_->setCurrentIndex(root);
    for (const auto &pair : {qMakePair(QStringLiteral("id"), imageId_), qMakePair(QStringLiteral("label"), imageLabel_),
        qMakePair(QStringLiteral("kernel"), kernel_), qMakePair(QStringLiteral("initramfs"), initramfs_),
        qMakePair(QStringLiteral("cmdline"), cmdline_), qMakePair(QStringLiteral("private-key"), signingKey_),
        qMakePair(QStringLiteral("certificate"), certificate_), qMakePair(QStringLiteral("version"), version_)})
        pair.second->setText(values.value(pair.first, pair.first == QLatin1String("id") ? QFileInfo(installation->binary).baseName()
            : pair.first == QLatin1String("label") ? loaderName(kind_) : QString()));
    const QSignalBlocker blocker(kernelChoice_);
    kernelChoice_->setCurrentIndex(0);
    for (int i = 1; i < kernelChoice_->count(); ++i)
        if (kernelChoice_->itemData(i).toJsonObject().value("kernel").toString() == values.value("kernel")) { kernelChoice_->setCurrentIndex(i); break; }
    selectDestination(installation->esp);
}

void RepairPage::addImageBootEntry()
{
    const bool checked = registerFirmware_->isChecked();
    registerFirmware_->setChecked(true);
    install(QStringLiteral("repair"));
    registerFirmware_->setChecked(checked);
}
