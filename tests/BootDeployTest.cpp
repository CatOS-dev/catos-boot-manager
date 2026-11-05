#include "services/BootDeploy.h"
#include "services/BootContent.h"
#include "services/BootMenus.h"
#include "services/BootProbe.h"
#include <efivar/efiboot.h>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
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
QJsonObject image(const QString &esp, const QString &kernel, const QString &initrd, const QString &loader)
{
    return {{"loader", loader}, {"root", "/"}, {"esp", esp}, {"id", "linux"}, {"label", "CatOS Linux"},
        {"kernel", kernel}, {"initramfs", initrd}, {"cmdline", "root=UUID=test ro"}, {"register", false}, {"fallback", false}};
}
}
class BootDeployTest : public QObject {
    Q_OBJECT
private slots:
    void detectInstalledKernelsWithDeployedInitramfs();
    void deployRefindPreservesConfiguration();
    void deployStubCopiesSelectedInputsAndIndependentProfile();
    void buildUkiWithNativeUkify();
    void preserveFirmwareDevicePathAndAttributes();
    void followTargetSystemAfterRemount();
};

void BootDeployTest::detectInstalledKernelsWithDeployedInitramfs()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString root = dir.path() + "/root";
    const QString esp = root + "/boot/efi";
    const QString machine = "ed7e70cc03564c939a40799647b6f8a7";
    const QString deployment = esp + '/' + machine + "/linux-cachyos";
    QVERIFY(fixture(root + "/etc/machine-id", machine.toUtf8() + '\n'));
    QVERIFY(fixture(root + "/etc/kernel/cmdline", "root=UUID=test rw\n"));
    QVERIFY(fixture(root + "/usr/lib/modules/current/pkgbase", "linux-cachyos\n"));
    QVERIFY(fixture(root + "/usr/lib/modules/current/vmlinuz", "current kernel"));
    QVERIFY(fixture(root + "/usr/lib/modules/old/pkgbase", "linux-cachyos\n"));
    QVERIFY(fixture(root + "/usr/lib/modules/old/vmlinuz", "old kernel"));
    QVERIFY(fixture(deployment + "/vmlinuz", "current kernel"));
    QVERIFY(fixture(deployment + "/initramfs", "current initramfs"));
    const QString foreign = dir.path() + "/foreign";
    QVERIFY(fixture(foreign + '/' + machine + "/linux-cachyos/vmlinuz", "old kernel"));
    QVERIFY(fixture(foreign + '/' + machine + "/linux-cachyos/initramfs", "foreign initramfs"));
    BootSnapshot snapshot;
    snapshot.repairRoots = {root, root};
    snapshot.espMounts = {foreign, esp};
    BootDeploy::populate(snapshot);
    QCOMPARE(snapshot.kernels.size(), 2);
    const auto current = snapshot.kernels[0].toObject();
    QCOMPARE(current.value("root").toString(), root);
    QCOMPARE(current.value("kernel").toString(), QString("/usr/lib/modules/current/vmlinuz"));
    QCOMPARE(current.value("initramfs").toString(), "/boot/efi/" + machine + "/linux-cachyos/initramfs");
    QCOMPARE(current.value("cmdline").toString(), QString("root=UUID=test rw"));
    QCOMPARE(snapshot.kernels[1].toObject().value("version").toString(), QString("old"));
    QVERIFY(snapshot.kernels[1].toObject().value("initramfs").toString().isEmpty());
    BootDeploy::populate(snapshot);
    QCOMPARE(snapshot.kernels.size(), 2);
    QVERIFY(fixture(root + "/boot/initramfs-linux-cachyos.img", "normal initramfs"));
    BootDeploy::populate(snapshot);
    QCOMPARE(snapshot.kernels[0].toObject().value("initramfs").toString(), QString("/boot/initramfs-linux-cachyos.img"));
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

void BootDeployTest::deployStubCopiesSelectedInputsAndIndependentProfile()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString esp = dir.path() + "/esp";
    QVERIFY(QDir().mkpath(esp));
    const QString kernel = dir.path() + "/vmlinuz";
    const QString initrd = dir.path() + "/initrd";
    QVERIFY(fixture(kernel, "kernel one")); QVERIFY(fixture(initrd, "initramfs one"));
    auto data = image(esp, kernel, initrd, "efistub");
    QString error;
    QVERIFY2(BootDeploy::deploy(data, "/", &error), qPrintable(error));
    const auto binary = esp + "/EFI/CatOS/linux/vmlinuz.efi";
    QCOMPARE(read(binary), QByteArray("kernel one"));
    QCOMPARE(read(esp + "/EFI/CatOS/linux/initramfs.img"), QByteArray("initramfs one"));
    const auto profile = BootDeploy::profilePath(binary);
    auto saved = BootDeploy::profile(profile, &error); QVERIFY(error.isEmpty());
    QCOMPARE(saved.value("loader").toString(), QString("efistub"));
    QCOMPARE(saved.value("cmdline").toString(), QString("root=UUID=test ro"));
    const QByteArray original = read(profile);
    data.insert("id", "second"); data.insert("cmdline", "root=UUID=other ro");
    QVERIFY2(BootDeploy::deploy(data, "/", &error), qPrintable(error));
    QCOMPARE(read(profile), original);
    QVERIFY2(BootDeploy::writeSetting(LoaderKind::EfiStub, BootDeploy::profilePath(esp + "/EFI/CatOS/second/vmlinuz.efi"), "cmdline", "new parameters", &error), qPrintable(error));
    QCOMPARE(read(profile), original);
    data.insert("id", "linux"); data.insert("kernel", binary);
    QVERIFY(!BootDeploy::deploy(data, "/", &error)); QVERIFY(!error.isEmpty());
    QCOMPARE(read(binary), QByteArray("kernel one"));
    data.insert("kernel", kernel); data.insert("binary", esp + "/EFI/CatOS/link/vmlinuz.efi");
    QVERIFY(QFile::link(dir.path(), esp + "/EFI/CatOS/link"));
    QVERIFY(!BootDeploy::deploy(data, "/", &error)); QVERIFY(!error.isEmpty());
    QVERIFY(!QFileInfo::exists(dir.path() + "/vmlinuz.efi"));
    data.remove("binary"); data.insert("fallback", true);
    QVERIFY(!BootDeploy::deploy(data, "/", &error)); QVERIFY(!error.isEmpty());
}

void BootDeployTest::buildUkiWithNativeUkify()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString esp = dir.path() + "/esp";
    QVERIFY(QDir().mkpath(esp));
    const QString kernel = dir.path() + "/kernel";
    const QString initrd = dir.path() + "/initrd";
    QVERIFY(fixture(kernel, "synthetic kernel payload")); QVERIFY(fixture(initrd, "synthetic initramfs payload"));
    auto data = image(esp, kernel, initrd, "uki"); data.insert("version", "test-1");
    QString error;
    QVERIFY2(BootDeploy::deploy(data, "/", &error), qPrintable(error));
    const auto binary = esp + "/EFI/Linux/catos-linux.efi";
    QCOMPARE(BootProbe::runReadOnly("bootctl", {"kernel-identify", binary}).trimmed(), QString("uki"));
    QProcess objcopy;
    objcopy.start("objcopy", {"--dump-section", ".linux=" + dir.path() + "/linux-section", "--dump-section", ".initrd=" + dir.path() + "/initrd-section",
        "--dump-section", ".cmdline=" + dir.path() + "/cmdline-section", binary, dir.path() + "/copy.efi"});
    QVERIFY(objcopy.waitForFinished()); QCOMPARE(objcopy.exitCode(), 0);
    QCOMPARE(read(dir.path() + "/linux-section"), read(kernel));
    QCOMPARE(read(dir.path() + "/initrd-section"), read(initrd));
    QVERIFY(read(dir.path() + "/cmdline-section").startsWith("root=UUID=test ro"));
    const auto profile = BootDeploy::profile(BootDeploy::profilePath(binary), &error); QVERIFY(error.isEmpty());
    QCOMPARE(profile.value("kernel").toString(), kernel); QCOMPARE(profile.value("version").toString(), QString("test-1"));
    QVERIFY(!QFileInfo::exists(binary + ".new"));
    data.insert("fallback", true);
    QVERIFY2(BootDeploy::deploy(data, "/", &error), qPrintable(error));
    QCOMPARE(read(esp + "/EFI/BOOT/BOOTX64.EFI"), read(binary));
    const auto before = read(binary);
    data.insert("kernel", "/missing/kernel");
    QVERIFY(!BootDeploy::deploy(data, "/", &error)); QVERIFY(!error.isEmpty());
    QCOMPARE(read(binary), before);
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
void BootDeployTest::followTargetSystemAfterRemount()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto target = dir.path() + "/remounted";
    QVERIFY(QDir().mkpath(target));
    const auto executable = dir.path() + "/findmnt";
    const QByteArray script = "#!/bin/sh\n[ \"$3\" = \"$CATOS_TEST_CURRENT_ROOT\" ] || exit 1\ncase \"$5\" in UUID) printf root-uuid;; FSROOT) printf /@;; SOURCE) printf /dev/test;; esac\n";
    QVERIFY(fixture(executable, script));
    QVERIFY(QFile::setPermissions(executable, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    const auto oldPath = qgetenv("PATH"); const auto oldRoot = qgetenv("CATOS_TEST_CURRENT_ROOT");
    const auto restore = qScopeGuard([&] { qputenv("PATH", oldPath); qputenv("CATOS_TEST_CURRENT_ROOT", oldRoot); });
    qputenv("PATH", dir.path().toUtf8()); qputenv("CATOS_TEST_CURRENT_ROOT", target.toUtf8());
    QJsonObject profile{{"loader", "efistub"}, {"root", "/previous-mount"}, {"rootUuid", "root-uuid"}, {"rootSource", "/dev/test"}, {"rootFsRoot", "/@"}};
    QString error;
    QCOMPARE(BootDeploy::targetRoot(profile, {"/", target}, &error), target); QVERIFY(error.isEmpty());
    const auto binary = dir.path() + "/esp/EFI/CatOS/linux/vmlinuz.efi";
    QVERIFY(fixture(BootDeploy::profilePath(binary), QJsonDocument(profile).toJson()));
    BootSnapshot snapshot;
    snapshot.repairRoots = {"/", target};
    snapshot.installations = {{LoaderKind::EfiStub, "image", dir.path() + "/esp", {}, binary}};
    BootContent::populate(snapshot);
    QCOMPARE(snapshot.installations[0].settingValues.value("root"), target);
    profile.insert("rootFsRoot", "/other");
    QVERIFY(BootDeploy::targetRoot(profile, {"/", target}, &error).isEmpty()); QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(BootDeployTest)
#include "BootDeployTest.moc"
