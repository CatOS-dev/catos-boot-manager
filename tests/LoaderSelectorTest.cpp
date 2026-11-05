#include "widgets/LoaderSelector.h"

#include <QComboBox>
#include <QTabBar>
#include <QtTest>

class LoaderSelectorTest : public QObject {
    Q_OBJECT
private slots:
    void preserveMethodAndInstanceAcrossPages();
    void doNotListGeneratorConfigurationAsAnotherDeployment();
};

void LoaderSelectorTest::preserveMethodAndInstanceAcrossPages()
{
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::Grub, "grub", ""},
        {LoaderKind::SystemdBoot, "first", "/first"},
        {LoaderKind::SystemdBoot, "second", "/second"}};
    LoaderSelector menu;
    LoaderSelector settings;
    connect(&menu, &LoaderSelector::installationChanged, &settings, &LoaderSelector::selectInstallation);
    connect(&settings, &LoaderSelector::installationChanged, &menu, &LoaderSelector::selectInstallation);
    menu.updateSnapshot(snapshot);
    settings.updateSnapshot(snapshot);
    auto *tabs = menu.findChild<QTabBar *>("loaderMethodTabs");
    auto *instances = menu.findChild<QComboBox *>("loaderInstallation");
    QVERIFY(tabs);
    QVERIFY(instances);
    QCOMPARE(tabs->count(), 6);
    QCOMPARE(tabs->tabText(0), QString("GRUB · Configuration only"));
    QCOMPARE(tabs->tabText(1), QString("Limine · Not detected"));
    QCOMPARE(tabs->tabText(2), QString("rEFInd · Not detected"));
    QCOMPARE(tabs->tabText(3), QString("systemd-boot · Configuration only"));
    QCOMPARE(tabs->tabText(4), QString("UKI · Not detected"));
    QCOMPARE(tabs->tabText(5), QString("EFI Stub · Not detected"));
    menu.selectMethod(LoaderKind::SystemdBoot);
    QCOMPARE(settings.method(), LoaderKind::SystemdBoot);
    QCOMPARE(instances->count(), 2);
    instances->setCurrentIndex(1);
    QCOMPARE(menu.currentId(), QString("second"));
    QCOMPARE(settings.currentId(), QString("second"));
    settings.selectMethod(LoaderKind::Uki);
    QCOMPARE(menu.method(), LoaderKind::Uki);
    QCOMPARE(menu.currentIndex(), -1);
    QVERIFY(!instances->isEnabled());
    menu.updateSnapshot(snapshot);
    QCOMPARE(menu.method(), LoaderKind::Uki);
    menu.selectMethod(LoaderKind::SystemdBoot);
    QCOMPARE(menu.currentId(), QString("second"));
    QCOMPARE(settings.currentId(), QString("second"));
    QCOMPARE(settings.method(), LoaderKind::SystemdBoot);
}

void LoaderSelectorTest::doNotListGeneratorConfigurationAsAnotherDeployment()
{
    BootSnapshot snapshot;
    snapshot.installations = {
        {LoaderKind::Grub, "program", "/boot/efi", "uuid", "/boot/efi/EFI/ARCH/grubx64.efi"},
        {LoaderKind::Grub, "configuration", {}, {}, {}, "/etc/default/grub", true}
    };
    LoaderSelector methods;
    LoaderSelector menu(nullptr, true);
    methods.updateSnapshot(snapshot);
    menu.updateSnapshot(snapshot);
    connect(&methods, &LoaderSelector::installationChanged, &menu, &LoaderSelector::selectInstallation);
    connect(&menu, &LoaderSelector::installationChanged, &methods, &LoaderSelector::selectInstallation);
    QCOMPARE(methods.installations().size(), 1);
    QCOMPARE(menu.installations().size(), 1);
    QCOMPARE(methods.currentId(), QString("program"));
    QCOMPARE(menu.currentId(), QString("configuration"));
    QCOMPARE(menu.installations().first().config, QString("/etc/default/grub"));
    methods.selectInstallation(LoaderKind::Grub, "configuration");
    QCOMPARE(methods.currentId(), QString("program"));
    menu.selectInstallation(LoaderKind::Grub, "program");
    QCOMPARE(menu.currentId(), QString("configuration"));
    BootInstallation other = snapshot.installations.first();
    other.id = "second-program";
    other.esp = "/efi";
    other.binary = "/efi/EFI/GRUB/grubx64.efi";
    snapshot.installations.append(other);
    methods.updateSnapshot(snapshot);
    menu.updateSnapshot(snapshot);
    QCOMPARE(methods.installations().size(), 2);
    QCOMPARE(menu.installations().size(), 1);
    methods.selectInstallation(LoaderKind::Grub, "second-program");
    QCOMPARE(methods.currentId(), QString("second-program"));
    QCOMPARE(menu.currentId(), QString("configuration"));
}

QTEST_MAIN(LoaderSelectorTest)
#include "LoaderSelectorTest.moc"
