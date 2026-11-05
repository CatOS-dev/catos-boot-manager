#include "widgets/LoaderSelector.h"

#include <QComboBox>
#include <QTabBar>
#include <QtTest>

class LoaderSelectorTest : public QObject {
    Q_OBJECT
private slots:
    void preserveMethodAndInstanceAcrossPages();
};

void LoaderSelectorTest::preserveMethodAndInstanceAcrossPages()
{
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::Grub, "grub", ""},
        {LoaderKind::SystemdBoot, "first", "/first"},
        {LoaderKind::SystemdBoot, "second", "/second"}};
    LoaderSelector menu;
    LoaderSelector settings;
    connect(&menu, &LoaderSelector::methodChanged, &settings, &LoaderSelector::selectMethod);
    connect(&settings, &LoaderSelector::methodChanged, &menu, &LoaderSelector::selectMethod);
    menu.updateSnapshot(snapshot);
    settings.updateSnapshot(snapshot);
    auto *tabs = menu.findChild<QTabBar *>("loaderMethodTabs");
    auto *instances = menu.findChild<QComboBox *>("loaderInstallation");
    QVERIFY(tabs);
    QVERIFY(instances);
    QCOMPARE(tabs->count(), 6);
    QCOMPARE(tabs->tabText(0), QString("GRUB"));
    QCOMPARE(tabs->tabText(1), QString("Limine"));
    QCOMPARE(tabs->tabText(2), QString("rEFInd"));
    QCOMPARE(tabs->tabText(3), QString("systemd-boot"));
    QCOMPARE(tabs->tabText(4), QString("UKI"));
    QCOMPARE(tabs->tabText(5), QString("EFI Stub"));
    menu.selectMethod(LoaderKind::SystemdBoot);
    QCOMPARE(settings.method(), LoaderKind::SystemdBoot);
    QCOMPARE(instances->count(), 2);
    instances->setCurrentIndex(1);
    QCOMPARE(menu.currentId(), QString("second"));
    settings.selectMethod(LoaderKind::Uki);
    QCOMPARE(menu.method(), LoaderKind::Uki);
    QCOMPARE(menu.currentIndex(), -1);
    QVERIFY(!instances->isEnabled());
    menu.updateSnapshot(snapshot);
    QCOMPARE(menu.method(), LoaderKind::Uki);
    menu.selectMethod(LoaderKind::SystemdBoot);
    QCOMPARE(menu.currentId(), QString("second"));
    QCOMPARE(settings.method(), LoaderKind::SystemdBoot);
}

QTEST_MAIN(LoaderSelectorTest)
#include "LoaderSelectorTest.moc"
