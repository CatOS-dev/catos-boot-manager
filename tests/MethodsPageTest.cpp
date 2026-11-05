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
    void preserveKernelChoiceAndClearFieldsAcrossSystems();
    void deployAllNewMethodsWithIndependentInputs();
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
    auto *repair = form->findChild<QPushButton *>("repairBootLoader");
    auto *add = panel->findChild<QPushButton *>("methodAddBootEntry");
    QVERIFY(deploy);
    QVERIFY(repair);
    QVERIFY(add);
    QCOMPARE(add->parentWidget(), deploy->parentWidget());
    QCOMPARE(add->parentWidget(), repair->parentWidget());
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
    repair->click();
    QCOMPARE(immediate.size(), 3);
    QCOMPARE(immediate[2].kind, QString("repair-install"));
    add->click();
    QCOMPARE(immediate.size(), 4);
    QCOMPARE(immediate[3].kind, QString("firmware-add"));
    QCOMPARE(immediate[3].target, installation.binary);
    QCOMPARE(queued.count(), 0);
}

void MethodsPageTest::preserveKernelChoiceAndClearFieldsAcrossSystems()
{
    BootSnapshot snapshot;
    snapshot.uefi = true; snapshot.repairRoots = {"/", "/target"};
    snapshot.espMounts = {"/boot/efi", "/target/boot/efi"};
    snapshot.kernels = {
        QJsonObject{{"root", "/"}, {"name", "linux"}, {"version", "current"}, {"kernel", "/usr/lib/modules/current/vmlinuz"}, {"initramfs", "/boot/efi/machine/linux/initramfs"}},
        QJsonObject{{"root", "/target"}, {"name", "linux-lts"}, {"version", "lts"}, {"kernel", "/usr/lib/modules/lts/vmlinuz"}, {"initramfs", "/boot/initramfs-linux-lts.img"}}
    };
    for (const auto kind : {LoaderKind::Uki, LoaderKind::EfiStub}) {
        RepairPage page(kind); page.updateTargets(snapshot);
        auto *choice = page.findChild<QComboBox *>("deploymentKernelChoice"); QVERIFY(choice);
        auto *root = page.findChild<QComboBox *>("repairRootTarget"); QVERIFY(root);
        auto *kernel = page.findChild<QLineEdit *>("deploymentKernel"); QVERIFY(kernel);
        auto *initramfs = page.findChild<QLineEdit *>("deploymentInitramfs"); QVERIFY(initramfs);
        QCOMPARE(choice->count(), 2); choice->setCurrentIndex(1);
        QCOMPARE(kernel->text(), QString("/usr/lib/modules/current/vmlinuz"));
        QCOMPARE(initramfs->text(), QString("/boot/efi/machine/linux/initramfs"));
        page.updateTargets(snapshot);
        QCOMPARE(choice->currentIndex(), 1);
        QCOMPARE(kernel->text(), QString("/usr/lib/modules/current/vmlinuz"));
        root->setCurrentIndex(root->findData("/target"));
        QCOMPARE(choice->count(), 2); QCOMPARE(choice->currentIndex(), 0);
        QVERIFY(kernel->text().isEmpty()); QVERIFY(initramfs->text().isEmpty());
        choice->setCurrentIndex(1);
        QCOMPARE(kernel->text(), QString("/usr/lib/modules/lts/vmlinuz"));
        QCOMPARE(initramfs->text(), QString("/boot/initramfs-linux-lts.img"));
        root->setCurrentIndex(root->findData("/"));
        QVERIFY(kernel->text().isEmpty());
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
        auto *id = form->findChild<QLineEdit *>("deploymentName"); QVERIFY(id); id->setText(loaderName(kind));
        auto *label = form->findChild<QLineEdit *>("deploymentLabel"); QVERIFY(label); label->setText(loaderName(kind));
        auto *kernels = form->findChild<QComboBox *>("deploymentKernelChoice"); QVERIFY(kernels); QCOMPARE(kernels->count(), 2); kernels->setCurrentIndex(1);
        auto *parameters = form->findChild<QLineEdit *>("deploymentParameters"); QVERIFY(parameters); QCOMPARE(parameters->text(), QString("root=UUID=test ro"));
        parameters->setText(loaderName(kind) + " parameters");
        auto *deploy = form->findChild<QPushButton *>("deployBootLoader");
        auto *repair = form->findChild<QPushButton *>("repairBootLoader");
        auto *update = form->findChild<QPushButton *>("updateBootLoader");
        QVERIFY(deploy); QVERIFY(repair); QVERIFY(update); QVERIFY(deploy->isEnabled()); QVERIFY(update->isEnabled());
        const int count = immediate.size();
        deploy->click(); repair->click(); update->click();
        QCOMPARE(immediate.size(), count + 3); QCOMPARE(queued.count(), 0);
        const auto first = BootRepair::arguments(immediate[count]);
        const auto second = BootRepair::arguments(immediate[count + 1]);
        const auto third = BootRepair::arguments(immediate[count + 2]);
        QCOMPARE(first.value("loader").toString(), BootDeploy::loaderId(kind));
        QCOMPARE(first.value("esp").toString(), QString("/test-esp"));
        QCOMPARE(first.value("action").toString(), QString("install"));
        QCOMPARE(second.value("action").toString(), QString("repair"));
        QCOMPARE(third.value("action").toString(), QString("update"));
        QCOMPARE(first.value("cmdline").toString(), loaderName(kind) + " parameters");
        QCOMPARE(third.value("binary").toString(), QString("/test-esp/EFI/test/") + loaderName(kind) + ".efi");
        page.resize(1000, 760); page.show(); QVERIFY(QTest::qWaitForWindowActive(&page));
        QVERIFY(page.grab().save("/tmp/catos-method-" + BootDeploy::loaderId(kind) + "-complete.png"));
    }
    QCOMPARE(immediate.size(), 9);
}

QTEST_MAIN(MethodsPageTest)
#include "MethodsPageTest.moc"
