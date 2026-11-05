#include "services/BootDeploy.h"
#include "services/BootContent.h"
#include "services/BootMenus.h"
#include "services/BootSettings.h"
#include <efivar/efiboot.h>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool fixture(const QString &path, const QByteArray &content)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}
QByteArray read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
QJsonObject image(const QString &esp, const QString &, const QString &, const QString &loader)
{
    return {{"loader", loader}, {"root", "/"}, {"esp", esp}, {"label", "CatOS Linux"},
        {"cmdline", "root=UUID=test ro"}, {"register", false}, {"fallback", false}};
}
}
class BootDeployTest : public QObject {
    Q_OBJECT
private slots:
    void detectInstalledKernelsWithoutInitramfs();
    void deployRefindPreservesConfiguration();
    void delegateGenerationAndSettingsToProvider_data();
    void delegateGenerationAndSettingsToProvider();
    void rejectFailedProviderWithoutTouchingExistingFiles();
    void preserveFirmwareDevicePathAndAttributes();
};

void BootDeployTest::detectInstalledKernelsWithoutInitramfs()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto root = dir.path() + "/root";
    QVERIFY(fixture(root + "/usr/lib/modules/current/pkgbase", "linux\n"));
    QVERIFY(fixture(root + "/usr/lib/modules/current/vmlinuz", "current kernel"));
    QVERIFY(fixture(root + "/usr/lib/modules/old/pkgbase", "linux\n"));
    QVERIFY(fixture(root + "/usr/lib/modules/old/vmlinuz", "old kernel"));
    QVERIFY(fixture(root + "/usr/lib/modules/custom/vmlinuz", "custom kernel"));
    BootSnapshot snapshot;
    snapshot.repairRoots = {root, root};
    BootDeploy::populate(snapshot);
    QCOMPARE(snapshot.kernels.size(), 3);
    QCOMPARE(snapshot.kernels[0].toObject().value("kernel").toString(), QString("/usr/lib/modules/current/vmlinuz"));
    QCOMPARE(snapshot.kernels[1].toObject().value("name").toString(), QString("custom"));
    QCOMPARE(snapshot.kernels[2].toObject().value("version").toString(), QString("old"));
    for (const auto &kernel : snapshot.kernels) QVERIFY(!kernel.toObject().contains("initramfs"));
    QVERIFY(!QFileInfo::exists(root + "/boot"));
    BootDeploy::populate(snapshot);
    QCOMPARE(snapshot.kernels.size(), 3);
}

void BootDeployTest::delegateGenerationAndSettingsToProvider_data()
{
    QTest::addColumn<QString>("loader");
    QTest::newRow("uki") << QString("uki");
    QTest::newRow("efistub") << QString("efistub");
}

void BootDeployTest::delegateGenerationAndSettingsToProvider()
{
    QFETCH(QString, loader);
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto root = dir.path() + "/root";
    const auto esp = root + "/arbitrary output/new directory";
    const auto provider = root + "/usr/bin/catos-firmware-boot-update";
    const auto chroot = dir.path() + "/arch-chroot";
    QVERIFY(fixture(provider, "#!/bin/sh\nexit 0\n"));
    QVERIFY(QFile::setPermissions(provider, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    QVERIFY(fixture(chroot, "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$CATOS_TEST_ARGUMENTS\"\ncase \"$*\" in *--describe*) printf '%s' \"$CATOS_TEST_CONFIGURATION\";; esac\n"));
    QVERIFY(QFile::setPermissions(chroot, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const auto oldPath = qgetenv("PATH");
    const auto oldArgs = qgetenv("CATOS_TEST_ARGUMENTS");
    const auto oldConfig = qgetenv("CATOS_TEST_CONFIGURATION");
    const auto restore = qScopeGuard([&] { qputenv("PATH", oldPath); qputenv("CATOS_TEST_ARGUMENTS", oldArgs); qputenv("CATOS_TEST_CONFIGURATION", oldConfig); });
    qputenv("PATH", dir.path().toUtf8());
    qputenv("CATOS_TEST_ARGUMENTS", (dir.path() + "/arguments").toUtf8());
    qputenv("CATOS_TEST_CONFIGURATION", QJsonDocument(QJsonObject{{"loader", loader}, {"esp", "/arbitrary output/new directory"},
        {"cmdline", "root=UUID=test ro"}, {"label_prefix", "CatOS"}, {"default_kernel", "linux"}}).toJson(QJsonDocument::Compact));
    auto data = image(esp, {}, {}, loader);
    QString error;
    QVERIFY2(BootDeploy::deploy(data, root, &error), qPrintable(error));
    QCOMPARE(read(dir.path() + "/arguments"), (root + "\ncatos-firmware-boot-update\n--config\n/etc/catos/firmware-boot.conf\n--method\n"
        + loader + "\n--output-root\n/arbitrary output/new directory\n--install-fallback\nno\n--save-config\n--efi-disk\n\n--efi-partition\n0\n--firmware-directory\n/\n--no-variables\n").toUtf8());
    QVERIFY(!QFileInfo::exists(esp));
    QVERIFY(fixture(dir.path() + "/findmnt", "#!/bin/sh\ncase \"$*\" in *SOURCE) printf /dev/mockp2;; *TARGET) printf '%s' \"$CATOS_TEST_MOUNT\";; *) exit 23;; esac\n"));
    QVERIFY(fixture(dir.path() + "/lsblk", "#!/bin/sh\n[ \"$*\" = '-dn -o PKNAME,PARTN /dev/mockp2' ] || exit 23\nprintf 'mock 2'\n"));
    QVERIFY(QFile::setPermissions(dir.path() + "/findmnt", QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    QVERIFY(QFile::setPermissions(dir.path() + "/lsblk", QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const auto oldMount = qgetenv("CATOS_TEST_MOUNT");
    const auto restoreMount = qScopeGuard([&] { qputenv("CATOS_TEST_MOUNT", oldMount); });
    qputenv("CATOS_TEST_MOUNT", root.toUtf8());
    data.insert("esp", root + "/nested/output"); data.insert("register", true);
    QVERIFY2(BootDeploy::deploy(data, root, &error), qPrintable(error));
    QVERIFY(read(dir.path() + "/arguments").contains("--efi-disk\n/dev/mock\n--efi-partition\n2\n--firmware-directory\n/nested/output\n"));
    QVERIFY(!read(dir.path() + "/arguments").contains("--no-variables"));
    const auto path = BootDeploy::configurationPath(root);
    QVERIFY(fixture(path, "[boot]\nmethod=" + loader.toUtf8() + "\n"));
    auto configuration = BootDeploy::profile(path, &error); QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(configuration.value("esp").toString(), esp);
    QCOMPARE(configuration.value("root").toString(), root);
    const auto kind = loader == QLatin1String("uki") ? LoaderKind::Uki : LoaderKind::EfiStub;
    QVERIFY2(BootDeploy::writeSetting(kind, path, "cmdline", "root=UUID=test quiet", &error), qPrintable(error));
    QVERIFY(read(dir.path() + "/arguments").contains("--configure-only\n--cmdline=root=UUID=test quiet\n"));
    QVERIFY2(BootDeploy::rebuild(kind, path, &error), qPrintable(error));
    QCOMPARE(read(dir.path() + "/arguments"), (root + "\ncatos-firmware-boot-update\n--config\n/etc/catos/firmware-boot.conf\n--method\n" + loader + "\n").toUtf8());
    BootSnapshot snapshot; snapshot.repairRoots = {root};
    BootContent::populate(snapshot);
    QCOMPARE(snapshot.installations.size(), 2);
    QVERIFY(snapshot.installations[0].settingsEditable);
    QCOMPARE(snapshot.installations[0].config, path);
    BootContent::populate(snapshot);
    QCOMPARE(snapshot.installations.size(), 2);
    QVERIFY(BootDeploy::validateRebuild(kind, path, {}, snapshot, &error));
    QVERIFY(!BootDeploy::writeSetting(kind, path, "initramfs", "/bogus", &error)); QVERIFY(!error.isEmpty());
}

void BootDeployTest::rejectFailedProviderWithoutTouchingExistingFiles()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto root = dir.path() + "/root";
    const auto provider = root + "/usr/bin/catos-firmware-boot-update";
    QVERIFY(fixture(provider, "#!/bin/sh\nexit 0\n"));
    QVERIFY(QFile::setPermissions(provider, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    QVERIFY(fixture(dir.path() + "/arch-chroot", "#!/bin/sh\nexit 17\n"));
    QVERIFY(QFile::setPermissions(dir.path() + "/arch-chroot", QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const auto oldPath = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", oldPath); });
    qputenv("PATH", dir.path().toUtf8());
    const auto destination = root + "/output";
    QVERIFY(fixture(destination + "/EFI/Linux/existing.efi", "old image"));
    QString error;
    QVERIFY(!BootDeploy::deploy(image(destination, {}, {}, "uki"), root, &error)); QVERIFY(!error.isEmpty());
    QCOMPARE(read(destination + "/EFI/Linux/existing.efi"), QByteArray("old image"));
    QVERIFY(!QFileInfo::exists(BootDeploy::configurationPath(root)));
    QVERIFY(!BootDeploy::profile(BootDeploy::configurationPath(root), &error).size()); QVERIFY(!error.isEmpty());
}

void BootDeployTest::deployRefindPreservesConfiguration()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString root = dir.path() + "/root";
    const QString esp = root + "/boot/efi";
    QVERIFY(QDir().mkpath(esp));
    QVERIFY(fixture(root + "/usr/share/refind/refind_x64.efi", "refind program"));
    QVERIFY(fixture(root + "/usr/share/refind/drivers_x64/ext4_x64.efi", "driver"));
    QVERIFY(fixture(root + "/usr/share/refind/icons/os_linux.png", "icon"));
    const QByteArray config = "timeout 7\ninclude theme.conf\nmenuentry CatOS {\n loader /vmlinuz\n}\n";
    QVERIFY(fixture(esp + "/EFI/refind/refind.conf", config));
    QVERIFY(fixture(esp + "/EFI/refind/theme.conf", "banner themes/background.png\n"));
    QVERIFY(fixture(esp + "/EFI/refind/themes/background.png", "custom background"));
    QVERIFY(fixture(root + "/boot/refind_linux.conf", "existing options\n"));
    auto data = image(esp, {}, {}, "refind");
    QString error;
    QVERIFY2(BootDeploy::deploy(data, root, &error), qPrintable(error));
    QCOMPARE(read(esp + "/EFI/refind/refind_x64.efi"), QByteArray("refind program"));
    QCOMPARE(read(esp + "/EFI/refind/drivers_x64/ext4_x64.efi"), QByteArray("driver"));
    QCOMPARE(read(esp + "/EFI/refind/icons/os_linux.png"), QByteArray("icon"));
    QCOMPARE(read(esp + "/EFI/refind/refind.conf"), config);
    QCOMPARE(read(root + "/boot/refind_linux.conf"), QByteArray("existing options\n"));
    QVERIFY(!QFileInfo::exists(esp + "/EFI/BOOT/BOOTX64.EFI"));
    data.insert("fallback", true);
    QVERIFY2(BootDeploy::deploy(data, root, &error), qPrintable(error));
    QCOMPARE(read(esp + "/EFI/BOOT/BOOTX64.EFI"), QByteArray("refind program"));
    QCOMPARE(read(esp + "/EFI/BOOT/refind.conf"), config);
    QCOMPARE(read(esp + "/EFI/BOOT/theme.conf"), QByteArray("banner themes/background.png\n"));
    QCOMPARE(read(esp + "/EFI/BOOT/themes/background.png"), QByteArray("custom background"));
    QVERIFY(fixture(esp + "/EFI/refind/refind_x64.efi", "unchanged"));
    data.insert("cmdline", "invalid\nparameters");
    QVERIFY(!BootDeploy::deploy(data, root, &error)); QVERIFY(!error.isEmpty());
    QCOMPARE(read(esp + "/EFI/refind/refind_x64.efi"), QByteArray("unchanged"));
}

void BootDeployTest::preserveFirmwareDevicePathAndAttributes()
{
    // End-of-device-path node is sufficient to exercise the native load-option codec.
    QByteArray path = QByteArray::fromHex("7fff0400");
    auto *devicePath = reinterpret_cast<efidp>(path.data());
    unsigned char title[] = "Old name";
    const auto size = efi_loadopt_create(nullptr, 0, 1, devicePath, path.size(), title, nullptr, 0);
    QVERIFY(size > 0);
    QByteArray original(size, '\0');
    QCOMPARE(efi_loadopt_create(reinterpret_cast<uint8_t *>(original.data()), size, 1, devicePath, path.size(), title, nullptr, 0), size);
    QString error;
    const auto changed = BootDeploy::firmwareData(original, "CatOS Linux", "initrd=\\EFI\\CatOS\\linux\\initramfs.img root=UUID=test ro", &error);
    QVERIFY2(error.isEmpty(), qPrintable(error)); QVERIFY(!changed.isEmpty());
    auto *option = reinterpret_cast<efi_load_option *>(const_cast<char *>(changed.constData()));
    QCOMPARE(efi_loadopt_is_valid(option, changed.size()), 1);
    QCOMPARE(efi_loadopt_attrs(option), uint32_t(1));
    QCOMPARE(efi_loadopt_pathlen(option, changed.size()), uint16_t(path.size()));
    QCOMPARE(QByteArray(reinterpret_cast<char *>(efi_loadopt_path(option, changed.size())), path.size()), path);
    unsigned char *optional = nullptr; size_t length = 0;
    QCOMPARE(efi_loadopt_optional_data(option, changed.size(), &optional, &length), 0);
    const auto parameters = QString::fromUtf16(reinterpret_cast<char16_t *>(optional), length / 2 - 1);
    QCOMPARE(parameters, QString("initrd=\\EFI\\CatOS\\linux\\initramfs.img root=UUID=test ro"));
    QVERIFY(BootDeploy::firmwareData("invalid", "name", "params", &error).isEmpty()); QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(BootDeployTest)
#include "BootDeployTest.moc"
