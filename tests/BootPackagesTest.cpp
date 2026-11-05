#include "services/BootPackages.h"

#include <QtTest>

class BootPackagesTest : public QObject {
    Q_OBJECT
private slots:
    void shareBackendWithoutRequiringUkifyForEfiStub();
    void addOnlySelectedFeaturesAndPreserveInstalledKernel();
    void distinguishUnknownPackageStateAndUnsupportedFeatures();
    void installOnlyTheChosenLoadersPackages();
};

void BootPackagesTest::shareBackendWithoutRequiringUkifyForEfiStub()
{
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.packageStateAvailable = true;
    snapshot.installedPackages = {"catos-firmware-boot", "efibootmgr", "linux-lts"};
    const auto uki = BootPackages::plan(LoaderKind::Uki, false, false, snapshot);
    QVERIFY(uki.error.isEmpty());
    QCOMPARE(uki.missing, QStringList({"systemd-ukify"}));
    const auto stub = BootPackages::plan(LoaderKind::EfiStub, false, false, snapshot);
    QVERIFY(stub.error.isEmpty());
    QVERIFY(stub.missing.isEmpty());
    QVERIFY(!stub.required.contains("systemd-ukify"));
    QVERIFY(!uki.required.contains("linux"));
    QVERIFY(!uki.required.contains("linux-headers"));
    QCOMPARE(snapshot.installedPackages, QSet<QString>({"catos-firmware-boot", "efibootmgr", "linux-lts"}));
}

void BootPackagesTest::addOnlySelectedFeaturesAndPreserveInstalledKernel()
{
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.packageStateAvailable = true;
    snapshot.installedPackages = {"limine", "limine-tool", "efibootmgr", "linux-lts", "linux-lts-headers"};
    const auto base = BootPackages::plan(LoaderKind::Limine, false, false, snapshot);
    QVERIFY(base.missing.isEmpty());
    const auto extras = BootPackages::plan(LoaderKind::Limine, true, true, snapshot);
    QCOMPARE(extras.missing, QStringList({"limine-btrfs", "catos-secureboot"}));
    QVERIFY(!extras.required.contains("linux"));
    QVERIFY(!extras.required.contains("linux-headers"));
    QVERIFY(!extras.required.contains("snapper"));
    QVERIFY(!extras.required.contains("sbctl"));
    const auto systemd = BootPackages::plan(LoaderKind::SystemdBoot, true, false, snapshot);
    QVERIFY(systemd.required.contains("catos-systemd-boot-config"));
    QVERIFY(systemd.required.contains("sdboot-btrfs"));
    QVERIFY(!systemd.required.contains("systemd-boot"));
}

void BootPackagesTest::distinguishUnknownPackageStateAndUnsupportedFeatures()
{
    BootSnapshot snapshot;
    snapshot.uefi = true;
    const auto unknown = BootPackages::plan(LoaderKind::Grub, false, false, snapshot);
    QVERIFY(!unknown.error.isEmpty());
    QVERIFY(unknown.missing.isEmpty());
    snapshot.packageStateAvailable = true;
    QVERIFY(!BootPackages::plan(LoaderKind::Refind, false, true, snapshot).error.isEmpty());
    QVERIFY(!BootPackages::plan(LoaderKind::Uki, true, false, snapshot).error.isEmpty());
    snapshot.uefi = false;
    QVERIFY(!BootPackages::plan(LoaderKind::SystemdBoot, false, false, snapshot).error.isEmpty());
    const auto biosGrub = BootPackages::plan(LoaderKind::Grub, false, false, snapshot);
    QVERIFY(biosGrub.error.isEmpty());
    QCOMPARE(biosGrub.required, QStringList({"grub"}));
}

void BootPackagesTest::installOnlyTheChosenLoadersPackages()
{
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.packageStateAvailable = true;
    const BootOperation grub{"install-packages", "GRUB", "grub,efibootmgr"};
    const BootOperation limine{"install-packages", "Limine", "limine,limine-tool,efibootmgr"};
    QString error;
    QCOMPARE(BootPackages::installArguments({grub, limine}, snapshot, &error),
        QStringList({"-S", "--needed", "--noconfirm", "--", "grub", "efibootmgr", "limine", "limine-tool"}));
    QVERIFY(error.isEmpty());
    auto invalid = grub;
    invalid.value = "grub,--root=/tmp/unrelated";
    QVERIFY(BootPackages::installArguments({invalid}, snapshot, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    invalid = grub;
    invalid.target = "unrelated";
    QVERIFY(BootPackages::installArguments({invalid}, snapshot, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    snapshot.packageStateAvailable = false;
    QVERIFY(BootPackages::installArguments({grub}, snapshot, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(BootPackagesTest)
#include "BootPackagesTest.moc"
