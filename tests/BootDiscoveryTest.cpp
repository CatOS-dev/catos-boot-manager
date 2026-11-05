#include "services/BootDiscovery.h"
#include "services/BootProbe.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool fixture(const QString &path, const QByteArray &contents = "fixture")
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

BootInstallation byBinary(const BootSnapshot &snapshot, const QString &binary)
{
    for (const auto &installation : snapshot.installations)
        if (installation.binary == binary) return installation;
    return {};
}
}

class BootDiscoveryTest : public QObject {
    Q_OBJECT
private slots:
    void preserveInstancesAndBindFirmwareByPartitionAndPath();
    void identifyRealUkiWithBootctl();
    void retainMissingRegisteredProgram();
};

void BootDiscoveryTest::preserveInstancesAndBindFirmwareByPartitionAndPath()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = directory.path() + "/first";
    const QString second = directory.path() + "/second";
    const QString root = directory.path() + "/root";
    const QString grub = first + "/EFI/a/grubx64.efi";
    const QString otherGrub = first + "/EFI/b/grubx64.efi";
    const QString armGrub = second + "/EFI/a/grubaa64.efi";
    const QString uki = first + "/EFI/Linux/catos-linux.efi";
    const QString stub = first + "/EFI/CatOS/linux/vmlinuz.efi";
    QVERIFY(fixture(grub));
    QVERIFY(fixture(otherGrub));
    QVERIFY(fixture(armGrub));
    QVERIFY(fixture(first + "/EFI/limine/limine_x64.efi"));
    QVERIFY(fixture(first + "/EFI/refind/refind_x64.efi"));
    QVERIFY(fixture(first + "/EFI/refind/refind.conf"));
    QVERIFY(fixture(first + "/EFI/systemd/systemd-bootaa64.efi"));
    QVERIFY(fixture(first + "/loader/loader.conf"));
    QVERIFY(fixture(uki));
    QVERIFY(fixture(stub));
    QVERIFY(fixture(first + "/EFI/CatOS/linux/initramfs.img"));
    QVERIFY(fixture(root + "/etc/default/grub"));
    BootSnapshot snapshot;
    snapshot.bootOrder = {"0001", "0002", "0003"};
    snapshot.bootNext = "0003";
    snapshot.firmware = {
        {"0001", "Disabled", "\\EFI\\a\\grubx64.efi", "first-uuid", false, false},
        {"0002", "Same label", "\\EFI\\a\\grubx64.efi", "FIRST-UUID", true, true},
        {"0003", "Same label", "\\EFI\\a\\grubaa64.efi", "second-uuid", true, false}
    };
    const auto identify = [uki, stub](const QString &path) {
        return path == uki ? QString("uki") : path == stub ? QString("pe") : QString("unknown");
    };
    BootDiscovery::populate(snapshot, {{first, "first-uuid"}, {second, "second-uuid"}}, root, identify);
    QCOMPARE(snapshot.installations.size(), 9);
    QCOMPARE(snapshot.manageableInstallations().size(), 9);
    QSet<LoaderKind> kinds;
    QSet<QString> identities;
    for (const auto &installation : snapshot.installations) {
        kinds.insert(installation.kind);
        identities.insert(installation.id);
    }
    QCOMPARE(kinds.size(), 6);
    QCOMPARE(identities.size(), 9);
    const auto firstGrub = byBinary(snapshot, grub);
    QVERIFY(firstGrub.current);
    QVERIFY(firstGrub.defaultEntry);
    QVERIFY(!firstGrub.nextEntry);
    QCOMPARE(firstGrub.firmwareEntries, QStringList({"0001", "0002"}));
    QVERIFY(firstGrub.config.isEmpty());
    QVERIFY(!byBinary(snapshot, otherGrub).firmwareRegistered);
    QVERIFY(byBinary(snapshot, armGrub).nextEntry);
    QVERIFY(!byBinary(snapshot, uki).firmwareRegistered);
    QCOMPARE(byBinary(snapshot, stub).kind, LoaderKind::EfiStub);
    const QString moved = directory.path() + "/remounted";
    QVERIFY(QDir().rename(first, moved));
    BootDiscovery::populate(snapshot, {{moved, "first-uuid"}, {second, "second-uuid"}}, root,
        [](const QString &) { return QString("unknown"); });
    const auto remounted = byBinary(snapshot, moved + "/EFI/a/grubx64.efi");
    QCOMPARE(remounted.id, firstGrub.id);
    QVERIFY(remounted.current);
}

void BootDiscoveryTest::identifyRealUkiWithBootctl()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString esp = directory.path() + "/esp";
    const QString uki = esp + "/EFI/Linux/test.efi";
    QVERIFY(QDir().mkpath(QFileInfo(uki).absolutePath()));
    const QString kernelPayload = directory.path() + "/linux";
    const QString osrel = directory.path() + "/os-release";
    QVERIFY(fixture(kernelPayload, "test kernel payload"));
    QVERIFY(fixture(osrel, "ID=catos\nPRETTY_NAME=CatOS\n"));
    QProcess objcopy;
    objcopy.start("objcopy", {"--add-section", ".linux=" + kernelPayload, "--add-section", ".osrel=" + osrel,
        "/usr/lib/systemd/boot/efi/linuxx64.efi.stub", uki});
    QVERIFY(objcopy.waitForFinished());
    QCOMPARE(objcopy.exitCode(), 0);
    QCOMPARE(BootProbe::runReadOnly("bootctl", {"kernel-identify", uki}).trimmed(), QString("uki"));
    BootSnapshot snapshot;
    BootDiscovery::populate(snapshot, {{esp, "test-uuid"}}, directory.path(), [](const QString &path) {
        return BootProbe::runReadOnly("bootctl", {"kernel-identify", path});
    });
    QCOMPARE(snapshot.installations.size(), 1);
    QCOMPARE(snapshot.installations.first().kind, LoaderKind::Uki);
    QVERIFY(!snapshot.installations.first().firmwareRegistered);
}

void BootDiscoveryTest::retainMissingRegisteredProgram()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    BootSnapshot snapshot;
    snapshot.firmware = {{"0002", "GRUB", "\\EFI\\missing\\grubx64.efi", "esp-uuid", true, false},
                         {"0003", "GRUB", "\\EFI\\other\\unknown.efi", "esp-uuid", true, false}};
    BootDiscovery::populate(snapshot, {{directory.path(), "esp-uuid"}}, directory.path(),
        [](const QString &) { return QString("unknown"); });
    QCOMPARE(snapshot.installations.size(), 1);
    QVERIFY(!snapshot.installations.first().binaryPresent);
    QVERIFY(snapshot.installations.first().firmwareRegistered);
    QCOMPARE(snapshot.installations.first().kind, LoaderKind::Grub);
}

QTEST_GUILESS_MAIN(BootDiscoveryTest)
#include "BootDiscoveryTest.moc"
