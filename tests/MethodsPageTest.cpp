#include "pages/MethodsPage.h"
#include "pages/RepairPage.h"
#include "widgets/LoaderSelector.h"
#include "services/BootPackages.h"
#include "services/BootRepair.h"
#include "services/BootDeploy.h"
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QTranslator>
#include <QCheckBox>
#include "model/BootPlan.h"

#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QFile>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

class MethodsPageTest : public QObject {
    Q_OBJECT
private slots:
    void stageInstanceSelectionAndRejectChangedTarget();
    void keepInstallationFormsSeparateWhenSwitchingLoaders();
    void installPackagesAndDeployImmediately();
    void showInstalledKernelsWithoutManualImageInputs();
    void deployAllNewMethodsWithIndependentInputs();
    void updateOnlyForSystemdBoot();
    void proposeFirmwareRegistrationWhenMissing();
    void offerTemporaryMountsOnlyForRepair();
};

void MethodsPageTest::stageInstanceSelectionAndRejectChangedTarget()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile program(directory.path() + "/grubx64.efi");
    QVERIFY(program.open(QIODevice::WriteOnly));
    QCOMPARE(program.write("test PE image"), qint64(13));
    program.close();
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.efiArchitecture = "x64";
    snapshot.secureBoot = SecureBootState::Disabled;
    snapshot.bootOrder = {"0001", "0002"};
    snapshot.firmware = {{"0001", "Old", "\\EFI\\old.efi", "uuid", true, true},
                         {"0002", "New", "\\EFI\\grubx64.efi", "uuid", true, false}};
    BootInstallation installation;
    installation.kind = LoaderKind::Grub;
    installation.id = "grub:uuid:efi/grubx64.efi";
    installation.esp = directory.path();
    installation.espPartuuid = "uuid";
    installation.binary = program.fileName();
    installation.binaryPresent = true;
    installation.firmwareEntries = {"0002"};
    installation.imageType = "pe";
    installation.architecture = "x64";
    snapshot.installations = {installation};
    BootTransaction transaction;
    MethodsPage page;
    page.updateSnapshot(snapshot);
    connect(&page, &MethodsPage::operationRequested, &page, [&](const BootOperation &op) {
        transaction.stage(op);
        page.updateTransaction(transaction);
    });
    auto *pages = page.findChild<QStackedWidget *>("methodPages");
    QVERIFY(pages);
    auto *setDefault = pages->currentWidget()->findChild<QPushButton *>("methodSetDefault");
    auto *once = pages->currentWidget()->findChild<QPushButton *>("methodBootOnce");
    QVERIFY(setDefault);
    QVERIFY(once);
    QVERIFY(setDefault->isEnabled());
    setDefault->click();
    QCOMPARE(transaction.count(), 1);
    QCOMPARE(transaction.operations().first().value, QString("0002,0001"));
    QCOMPARE(transaction.operations().first().expectedEntry, snapshot.firmware[1].identity());
    QVERIFY(!setDefault->isEnabled());
    once->click();
    QCOMPARE(transaction.count(), 2);
    QVERIFY(!once->isEnabled());
    QString error;
    const auto decoded = BootTransaction::decode(transaction.encoded(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(decoded.size(), 2);
    QCOMPARE(decoded[1].expectedEntry, snapshot.firmware[1].identity());
    QVERIFY(BootPlan::firmwareConflict(decoded, snapshot).isEmpty());
    BootSnapshot changed = snapshot;
    changed.firmware[1].partitionUuid = "another-esp";
    QVERIFY(!BootPlan::firmwareConflict(decoded, changed).isEmpty());
    changed = snapshot;
    changed.installations[0].architecture = "aa64";
    QVERIFY(!BootPlan::firmwareConflict(decoded, changed).isEmpty());
    changed.installations[0].architecture.clear();
    QVERIFY(!BootPlan::firmwareConflict(decoded, changed).isEmpty());
    changed = snapshot;
    changed.installations[0].imageType = "unknown";
    QVERIFY(!BootPlan::firmwareConflict(decoded, changed).isEmpty());
    changed = snapshot;
    changed.secureBoot = SecureBootState::Enabled;
    QVERIFY(!BootPlan::firmwareConflict(decoded, changed).isEmpty());
    page.updateSnapshot(changed);
    QVERIFY(!setDefault->isEnabled());
    QVERIFY(!once->isEnabled());
    QVERIFY(program.remove());
    QVERIFY(!BootPlan::firmwareConflict(decoded, snapshot).isEmpty());
    page.updateTransaction({});
    page.updateSnapshot(snapshot);
    QVERIFY(!setDefault->isEnabled());
    QVERIFY(!once->isEnabled());
}

void MethodsPageTest::keepInstallationFormsSeparateWhenSwitchingLoaders()
{
    QTranslator translator;
    QVERIFY(translator.load(QCoreApplication::applicationDirPath() + "/catos-boot-manager_zh_CN.qm"));
    QApplication::installTranslator(&translator);
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.packageStateAvailable = true;
    snapshot.installedPackages = {"grub", "limine", "limine-tool", "efibootmgr"};
    snapshot.installations = {{LoaderKind::Grub, "grub", "/boot/efi", "grub-uuid", "/boot/efi/EFI/GRUB/grubx64.efi"},
                             {LoaderKind::Limine, "limine", "/boot/efi", "limine-uuid", "/boot/efi/EFI/limine/limine.efi"}};
    snapshot.installations[0].binaryPresent = true;
    snapshot.installations[1].binaryPresent = true;
    MethodsPage page;
    page.resize(1040, 700);
    page.updateSnapshot(snapshot);
    page.show();
    QVERIFY(QTest::qWaitForWindowActive(&page));
    auto *stack = page.findChild<QStackedWidget *>("methodPages");
    QVERIFY(stack);
    QCOMPARE(stack->count(), 6);
    QVERIFY(page.findChildren<QTabWidget *>().isEmpty());
    auto *grub = stack->widget(0);
    auto *limine = stack->widget(1);
    auto *grubForm = grub->findChild<RepairPage *>();
    auto *limineForm = limine->findChild<RepairPage *>();
    QVERIFY(grubForm);
    QVERIFY(limineForm);
    QVERIFY(grubForm->isVisible());
    QVERIFY(!limineForm->isVisible());
    QVERIFY(!page.findChild<QToolButton *>("installBootLoader"));
    auto *grubMode = grubForm->findChild<QComboBox *>("repairFirmwareMode");
    auto *limineMode = limineForm->findChild<QComboBox *>("repairFirmwareMode");
    QVERIFY(grubMode);
    QVERIFY(limineMode);
    grubMode->setCurrentIndex(1);
    QCOMPARE(grubMode->currentData().toString(), QString("bios"));
    QVERIFY(page.grab().save("/tmp/catos-method-grub.png"));
    page.selectMethod(LoaderKind::Limine);
    QCOMPARE(stack->currentWidget(), limine);
    QVERIFY(!grubForm->isVisible());
    QVERIFY(limineForm->isVisible());
    QVERIFY(!grubForm->isVisible());
    QCOMPARE(limineMode->count(), 1);
    QCOMPARE(limineMode->currentData().toString(), QString("uefi"));
    QVERIFY(page.grab().save("/tmp/catos-method-limine.png"));
    page.resize(760, 520);
    QCoreApplication::processEvents();
    QVERIFY(page.grab().save("/tmp/catos-method-limine-small.png"));
    page.selectMethod(LoaderKind::Grub);
    QVERIFY(grubForm->isVisible());
    QVERIFY(!limineForm->isVisible());
    QCOMPARE(grubMode->currentData().toString(), QString("bios"));
    page.updateSnapshot(snapshot);
    QVERIFY(grubForm->isVisible());
    QVERIFY(!limineForm->isVisible());
    page.selectMethod(LoaderKind::Uki);
    QVERIFY(!grubForm->isVisible());
    QVERIFY(!limineForm->isVisible());
    QCOMPARE(stack->currentWidget()->findChildren<RepairPage *>().size(), 1);
    QVERIFY(page.grab().save("/tmp/catos-method-uki.png"));
    QApplication::removeTranslator(&translator);
}

void MethodsPageTest::installPackagesAndDeployImmediately()
{
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.packageStateAvailable = true;
    BootInstallation installation;
    installation.kind = LoaderKind::Limine;
    installation.id = "limine-program";
    installation.esp = "/boot/efi";
    installation.binary = "/boot/efi/EFI/limine/limine_x64.efi";
    installation.binaryPresent = true;
    snapshot.installations = {installation};
    MethodsPage page;
    page.updateSnapshot(snapshot);
    page.selectMethod(LoaderKind::Limine);
    auto *stack = page.findChild<QStackedWidget *>("methodPages");
    QVERIFY(stack);
    auto *panel = stack->currentWidget();
    auto *packages = panel->findChild<QPushButton *>("installSoftwarePackages");
    auto *form = panel->findChild<RepairPage *>();
    QVERIFY(packages);
    QVERIFY(form);
    auto *deploy = form->findChild<QPushButton *>("deployBootLoader");
    auto *add = panel->findChild<QPushButton *>("methodAddBootEntry");
    QVERIFY(deploy);
    QVERIFY(!form->findChild<QPushButton *>("repairBootLoader"));
    QVERIFY(!form->findChild<QPushButton *>("updateBootLoader")->isVisibleTo(form));
    QVERIFY(add);
    QCOMPARE(add->parentWidget(), deploy->parentWidget());
    QList<BootOperation> immediate;
    BootTransaction queued;
    connect(&page, &MethodsPage::installationRequested, &page, [&](const BootOperation &op) { immediate.append(op); });
    connect(&page, &MethodsPage::operationRequested, &page, [&](const BootOperation &op) { queued.stage(op); });
    QVERIFY(packages->isEnabled());
    QVERIFY(!deploy->isEnabled());
    packages->click();
    QCOMPARE(immediate.size(), 1);
    QCOMPARE(immediate[0].kind, QString("install-packages"));
    QCOMPARE(immediate[0].value, QString("limine,limine-tool,efibootmgr"));
    QCOMPARE(queued.count(), 0);
    snapshot.installedPackages = {"limine", "limine-tool", "efibootmgr"};
    snapshot.repairRoots = {"/"};
    page.updateSnapshot(snapshot);
    QVERIFY(!packages->isEnabled());
    QVERIFY(deploy->isEnabled());
    deploy->click();
    QCOMPARE(immediate.size(), 2);
    QCOMPARE(immediate[1].kind, QString("repair-install"));
    QCOMPARE(BootRepair::arguments(immediate[1]).value("loader").toString(), QString("limine"));
    QCOMPARE(BootRepair::arguments(immediate[1]).value("root").toString(), QString("/"));
    QCOMPARE(BootRepair::arguments(immediate[1]).value("action").toString(), QString("repair"));
    QCOMPARE(deploy->text(), QString("Reinstall boot loader"));
    add->click();
    QCOMPARE(immediate.size(), 3);
    QCOMPARE(immediate[2].kind, QString("firmware-add"));
    QCOMPARE(immediate[2].target, installation.binary);
    QCOMPARE(queued.count(), 0);
}

void MethodsPageTest::showInstalledKernelsWithoutManualImageInputs()
{
    BootSnapshot snapshot;
    snapshot.uefi = true; snapshot.repairRoots = {"/", "/target"};
    snapshot.kernels = {
        QJsonObject{{"root", "/"}, {"name", "linux"}, {"version", "current"}, {"kernel", "/usr/lib/modules/current/vmlinuz"}},
        QJsonObject{{"root", "/target"}, {"name", "linux-lts"}, {"version", "lts"}, {"kernel", "/usr/lib/modules/lts/vmlinuz"}}
    };
    for (const auto kind : {LoaderKind::Uki, LoaderKind::EfiStub}) {
        RepairPage page(kind); page.updateTargets(snapshot);
        auto *kernels = page.findChild<QLabel *>("deploymentInstalledKernels"); QVERIFY(kernels);
        auto *root = page.findChild<QComboBox *>("repairRootTarget"); QVERIFY(root);
        auto *destination = page.findChild<QComboBox *>("installationDirectory"); QVERIFY(destination);
        QVERIFY(destination->isEditable());
        QCOMPARE(kernels->text(), QString("linux · current"));
        QVERIFY(!page.findChild<QLineEdit *>("deploymentInitramfs"));
        QVERIFY(!page.findChild<QLineEdit *>("deploymentKernel"));
        destination->setEditText("/ordinary/custom-output");
        page.updateTargets(snapshot);
        QCOMPARE(destination->currentText(), QString("/ordinary/custom-output"));
        root->setCurrentIndex(root->findData("/target"));
        QCOMPARE(kernels->text(), QString("linux-lts · lts"));
        QCOMPARE(destination->currentText(), QString("/target/boot/efi"));
    }
}

void MethodsPageTest::deployAllNewMethodsWithIndependentInputs()
{
    QTranslator translator;
    QVERIFY(translator.load(QCoreApplication::applicationDirPath() + "/catos-boot-manager_zh_CN.qm"));
    QApplication::installTranslator(&translator);
    BootSnapshot snapshot;
    snapshot.uefi = true; snapshot.packageStateAvailable = true;
    snapshot.installedPackages = {"refind", "catos-firmware-boot", "systemd-ukify", "efibootmgr"};
    snapshot.repairRoots = {"/"}; snapshot.espMounts = {"/test-esp"};
    snapshot.kernels = {QJsonObject{{"root", "/"}, {"name", "linux"}, {"version", "6.test"},
        {"kernel", "/usr/lib/modules/6.test/vmlinuz"}, {"initramfs", "/boot/initramfs-linux.img"}, {"cmdline", "root=UUID=test ro"}}};
    for (const auto kind : {LoaderKind::Refind, LoaderKind::Uki, LoaderKind::EfiStub}) {
        BootInstallation loader; loader.kind = kind; loader.id = loaderName(kind); loader.esp = "/test-esp";
        loader.binary = "/test-esp/EFI/test/" + loaderName(kind) + ".efi"; loader.binaryPresent = true;
        snapshot.installations.append(loader);
    }
    MethodsPage page;
    page.updateSnapshot(snapshot);
    QList<BootOperation> immediate;
    BootTransaction queued;
    connect(&page, &MethodsPage::installationRequested, &page, [&](const BootOperation &op) { immediate.append(op); });
    connect(&page, &MethodsPage::operationRequested, &page, [&](const BootOperation &op) { queued.stage(op); });
    auto *stack = page.findChild<QStackedWidget *>("methodPages"); QVERIFY(stack);
    for (const auto kind : {LoaderKind::Refind, LoaderKind::Uki, LoaderKind::EfiStub}) {
        page.selectMethod(kind);
        auto *form = stack->currentWidget()->findChild<RepairPage *>(); QVERIFY(form);
        auto *destination = form->findChild<QComboBox *>("installationDirectory"); QVERIFY(destination);
        destination->setEditText("/ordinary/custom-output");
        page.updateSnapshot(snapshot);
        QCOMPARE(destination->currentText(), QString("/ordinary/custom-output"));
        destination->setEditText("/test-esp");
        auto *label = form->findChild<QLineEdit *>("deploymentLabel"); QVERIFY(label); label->setText(loaderName(kind));
        auto *parameters = form->findChild<QLineEdit *>("deploymentParameters"); QVERIFY(parameters);
        parameters->setText(loaderName(kind) + " parameters");
        auto *deploy = form->findChild<QPushButton *>("deployBootLoader");
        auto *update = form->findChild<QPushButton *>("updateBootLoader");
        QVERIFY(deploy); QVERIFY(update); QVERIFY(deploy->isEnabled());
        QVERIFY(!form->findChild<QPushButton *>("repairBootLoader"));
        QVERIFY(!update->isVisibleTo(form));
        QVERIFY(!form->findChild<QLabel *>("deploymentActionHelp")->text().isEmpty());
        const int count = immediate.size();
        deploy->click();
        QCOMPARE(immediate.size(), count + 1); QCOMPARE(queued.count(), 0);
        const auto first = BootRepair::arguments(immediate[count]);
        QCOMPARE(first.value("loader").toString(), BootDeploy::loaderId(kind));
        QCOMPARE(first.value("esp").toString(), QString("/test-esp"));
        QCOMPARE(first.value("action").toString(), QString("repair"));
        QVERIFY(!first.contains("initramfs"));
        QVERIFY(!first.contains("binary"));
        QVERIFY(!first.contains("kernel"));
        page.resize(1000, 760); page.show(); QVERIFY(QTest::qWaitForWindowActive(&page));
        QVERIFY(page.grab().save("/tmp/catos-method-" + BootDeploy::loaderId(kind) + "-complete.png"));
    }
    QCOMPARE(immediate.size(), 3);
}

void MethodsPageTest::updateOnlyForSystemdBoot()
{
    BootSnapshot snapshot;
    snapshot.uefi = true; snapshot.packageStateAvailable = true;
    snapshot.installedPackages = {"catos-systemd-boot-config", "efibootmgr"};
    snapshot.repairRoots = {"/"}; snapshot.espMounts = {"/test-esp"};
    RepairPage page(LoaderKind::SystemdBoot);
    page.updateTargets(snapshot);
    page.setPackagesReady(true);
    auto *deploy = page.findChild<QPushButton *>("deployBootLoader");
    auto *update = page.findChild<QPushButton *>("updateBootLoader");
    auto *destination = page.findChild<QComboBox *>("installationDirectory");
    destination->setCurrentIndex(destination->findData("/test-esp"));
    QVERIFY(update->isVisibleTo(&page));
    QVERIFY(deploy->isEnabled());
    QVERIFY(!update->isEnabled());
    BootInstallation installed; installed.kind = LoaderKind::SystemdBoot; installed.id = "sd";
    installed.esp = "/test-esp"; installed.binary = "/test-esp/EFI/systemd/systemd-bootx64.efi"; installed.binaryPresent = true;
    page.selectInstallation(&installed);
    QVERIFY(update->isEnabled());
    QList<BootOperation> requested;
    connect(&page, &RepairPage::installationRequested, &page, [&](const BootOperation &op) { requested.append(op); });
    deploy->click(); update->click();
    QCOMPARE(requested.size(), 2);
    QCOMPARE(BootRepair::arguments(requested[0]).value("action").toString(), QString("repair"));
    QCOMPARE(BootRepair::arguments(requested[1]).value("action").toString(), QString("update"));
}

void MethodsPageTest::proposeFirmwareRegistrationWhenMissing()
{
    BootSnapshot snapshot;
    snapshot.uefi = true; snapshot.repairRoots = {"/"}; snapshot.espMounts = {"/test-esp"};
    RepairPage page(LoaderKind::Grub);
    page.updateTargets(snapshot);
    page.selectInstallation(nullptr);
    auto *registration = page.findChild<QCheckBox *>("registerFirmwareEntry");
    QVERIFY(registration);
    QVERIFY(registration->isChecked());
    BootInstallation registered; registered.kind = LoaderKind::Grub; registered.id = "grub";
    registered.esp = "/test-esp"; registered.binary = "/test-esp/EFI/GRUB/grubx64.efi"; registered.binaryPresent = true;
    registered.firmwareRegistered = true;
    page.selectInstallation(&registered);
    QVERIFY(!registration->isChecked());
    registration->click();
    QVERIFY(registration->isChecked());
    page.updateTargets(snapshot);
    page.selectInstallation(&registered);
    QVERIFY(registration->isChecked());
    auto *mode = page.findChild<QComboBox *>("repairFirmwareMode");
    mode->setCurrentIndex(mode->findData("bios"));
    QVERIFY(!registration->isChecked());
    snapshot.uefi = false;
    RepairPage legacy(LoaderKind::Refind);
    legacy.updateTargets(snapshot);
    legacy.selectInstallation(nullptr);
    QVERIFY(!legacy.findChild<QCheckBox *>("registerFirmwareEntry")->isChecked());
    QVERIFY(!legacy.findChild<QCheckBox *>("registerFirmwareEntry")->isEnabled());
}

void MethodsPageTest::offerTemporaryMountsOnlyForRepair()
{
    BootSnapshot snapshot;
    snapshot.uefi = true; snapshot.repairRoots = {"/"};
    snapshot.devices = {{"/dev/sda2", "part", "ext4", "100G", "uuid", {}}};
    RepairPage deploy(LoaderKind::Grub);
    RepairPage repair(LoaderKind::Grub, nullptr, RepairPage::Mode::Repair);
    deploy.updateTargets(snapshot); repair.updateTargets(snapshot);
    deploy.selectInstallation(nullptr); repair.selectInstallation(nullptr);
    QCOMPARE(deploy.findChild<QComboBox *>("repairRootTarget")->findData("auto:/dev/sda2"), -1);
    QVERIFY(repair.findChild<QComboBox *>("repairRootTarget")->findData("auto:/dev/sda2") >= 0);
    QCOMPARE(deploy.findChild<QPushButton *>("deployBootLoader")->text(), QString("Deploy boot loader"));
    QCOMPARE(repair.findChild<QPushButton *>("deployBootLoader")->text(), QString("Repair boot loader"));
}

QTEST_MAIN(MethodsPageTest)
#include "MethodsPageTest.moc"
