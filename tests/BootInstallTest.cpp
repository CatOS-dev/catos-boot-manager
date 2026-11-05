#define main bootApplyEntryForTest
#include "../src/privileged/BootApplyExecutor.cpp"
#undef main

#include <QScopeGuard>
#include "services/BootContent.h"
#include <QTemporaryDir>
#include <QtTest>

class BootInstallTest : public QObject {
    Q_OBJECT
private slots:
    void runSelectedPackageInstallation();
    void stopAfterPackageInstallationFails();
    void reportPartialFirmwareExecution();
    void applyMenuAndGlobalSettingsToSameFile();
    void createBlsAndSelectItInOneTransaction();
    void validateProjectedImageSettingsBeforeWriting();
};

void BootInstallTest::runSelectedPackageInstallation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString executable = directory.path() + "/pacman";
    QFile pacman(executable);
    QVERIFY(pacman.open(QIODevice::WriteOnly));
    const QByteArray script = "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$CATOS_INSTALL_TEST_ARGUMENTS\"\n";
    QCOMPARE(pacman.write(script), qint64(script.size()));
    pacman.close();
    QVERIFY(pacman.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    const QByteArray previousPath = qgetenv("PATH");
    const QByteArray previousArguments = qgetenv("CATOS_INSTALL_TEST_ARGUMENTS");
    const auto restore = qScopeGuard([&] {
        qputenv("PATH", previousPath);
        qputenv("CATOS_INSTALL_TEST_ARGUMENTS", previousArguments);
    });
    qputenv("PATH", directory.path().toUtf8());
    qputenv("CATOS_INSTALL_TEST_ARGUMENTS", (directory.path() + "/arguments").toUtf8());
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.packageStateAvailable = true;
    const BootOperation installation{"install-packages", "Limine", "limine,limine-tool,efibootmgr"};
    QString error;
    QVERIFY2(preflight({installation}, snapshot, &error), qPrintable(error));
    BootBackup backup;
    QVERIFY2(backup.prepare({installation}, snapshot, directory.path() + "/backup", &error), qPrintable(error));
    QJsonObject outcomes;
    QVERIFY(apply({installation}, backup, snapshot, &outcomes));
    QFile arguments(directory.path() + "/arguments");
    QVERIFY(arguments.open(QIODevice::ReadOnly));
    QCOMPARE(arguments.readAll(), QByteArray("-S\n--needed\n--noconfirm\n--\nlimine\nlimine-tool\nefibootmgr\n"));
    QFile report(backup.directory() + "/manifest.json");
    QVERIFY(report.open(QIODevice::ReadOnly));
    const auto results = QJsonDocument::fromJson(report.readAll()).object().value("results").toArray();
    QCOMPARE(results.size(), 1);
    QVERIFY(results[0].toObject().value("success").toBool());
}

void BootInstallTest::stopAfterPackageInstallationFails()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile pacman(directory.path() + "/pacman");
    QVERIFY(pacman.open(QIODevice::WriteOnly));
    QCOMPARE(pacman.write("#!/bin/sh\nexit 17\n"), qint64(18));
    pacman.close();
    QVERIFY(pacman.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    const QByteArray previousPath = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", previousPath); });
    qputenv("PATH", directory.path().toUtf8());
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.packageStateAvailable = true;
    const QString sentinel = directory.path() + "/sentinel";
    const auto request = QJsonObject{{"format", "bls"}, {"values", QJsonObject{{"title", "Linux"}, {"linux", "/kernel"}, {"initrd", QJsonArray{}}}}};
    const QList<BootOperation> operations{{"install-packages", "GRUB", "grub,efibootmgr"},
        {"menu-write", sentinel, QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)), BootMenus::fingerprint({})}};
    QString error;
    BootBackup backup;
    QVERIFY2(backup.prepare(operations, snapshot, directory.path() + "/backup", &error), qPrintable(error));
    QJsonObject outcomes;
    QVERIFY(!apply(operations, backup, snapshot, &outcomes));
    QCOMPARE(outcomes.value(operations[0].key()).toString(), QString("failed"));
    QCOMPARE(outcomes.value(operations[1].key()).toString(), QString("not-executed"));
    QVERIFY(!QFileInfo::exists(directory.path() + "/sentinel"));
}

void BootInstallTest::reportPartialFirmwareExecution()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QFile executable(directory.filePath("efibootmgr")); QVERIFY(executable.open(QIODevice::WriteOnly));
    const QByteArray script = "#!/bin/sh\ncase \"$*\" in *0002*) exit 1;; *) exit 0;; esac\n";
    QCOMPARE(executable.write(script), qint64(script.size())); executable.close();
    QVERIFY(executable.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    const auto oldPath = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", oldPath); });
    qputenv("PATH", directory.path().toUtf8());
    const QList<BootOperation> operations{{"firmware-toggle", "0001", "0", "1", "Disable first"},
        {"firmware-toggle", "0002", "0", "1", "Disable second"},
        {"firmware-toggle", "0003", "0", "1", "Disable third"}};
    BootSnapshot snapshot;
    BootBackup backup;
    QString error;
    QVERIFY2(backup.prepare(operations, snapshot, directory.path() + "/backup", &error), qPrintable(error));
    QJsonObject outcomes;
    QVERIFY(!apply(operations, backup, snapshot, &outcomes));
    QCOMPARE(outcomes.value(operations[0].key()).toString(), QString("succeeded"));
    QCOMPARE(outcomes.value(operations[1].key()).toString(), QString("failed"));
    QCOMPARE(outcomes.value(operations[2].key()).toString(), QString("not-executed"));
}

namespace {
bool writeMenuFixture(const QString &path, const QByteArray &value)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(value) == value.size();
}
BootOperation menuWrite(const QString &path, const QString &content, const QJsonObject &data)
{
    return {"menu-write", path, QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)), BootMenus::fingerprint(content)};
}
}
void BootInstallTest::applyMenuAndGlobalSettingsToSameFile()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.path() + "/esp/limine.conf";
    const QString original = "timeout: 5\n/Linux\nprotocol: linux\npath: boot():/vmlinuz\nunknown: keep\n";
    QVERIFY(writeMenuFixture(path, original.toUtf8()));
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::Limine, "limine", directory.path() + "/esp", {}, {}, path, true}};
    BootContent::populate(snapshot);
    auto entries = QJsonDocument::fromJson(snapshot.installations[0].menuContent.toUtf8()).object().value("entries").toArray();
    auto entry = entries[0].toObject(); entry.insert("name", "New Linux"); entries[0] = entry;
    const QList<BootOperation> operations{{"limine-setting", path + "|timeout", "8", "5"}, menuWrite(path, original, {{"format", "limine"}, {"entries", entries}})};
    QString error;
    QVERIFY2(preflight(operations, snapshot, &error), qPrintable(error));
    BootBackup backup;
    QVERIFY2(backup.prepare(operations, snapshot, directory.path() + "/backup", &error), qPrintable(error));
    QJsonObject outcomes;
    QVERIFY(apply(operations, backup, snapshot, &outcomes));
    QCOMPARE(outcomes.value(operations[0].key()).toString(), QString("succeeded"));
    QCOMPARE(outcomes.value(operations[1].key()).toString(), QString("succeeded"));
    const auto changed = BootMenus::readText(path, &error); QVERIFY(error.isEmpty());
    QVERIFY(changed.contains("/New Linux")); QVERIFY(changed.contains("timeout: 8")); QVERIFY(changed.contains("unknown: keep"));
    QFile report(backup.directory() + "/manifest.json"); QVERIFY(report.open(QIODevice::ReadOnly));
    const auto files = QJsonDocument::fromJson(report.readAll()).object().value("files").toArray();
    QCOMPARE(files.size(), 1); QCOMPARE(files[0].toObject().value("path").toString(), path);
    QCOMPARE(BootMenus::readText(backup.directory() + '/' + files[0].toObject().value("backup").toString(), &error), original); QVERIFY(error.isEmpty());
    BootContent::populate(snapshot);
    QVERIFY(!preflight(operations, snapshot, &error)); QVERIFY(!error.isEmpty());
}

void BootInstallTest::createBlsAndSelectItInOneTransaction()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto esp = directory.path() + "/esp";
    QVERIFY(writeMenuFixture(esp + "/loader/loader.conf", "default old.conf\ntimeout 3\n"));
    QVERIFY(writeMenuFixture(esp + "/kernel", "kernel")); QVERIFY(writeMenuFixture(esp + "/initrd", "initramfs"));
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::SystemdBoot, "systemd", esp, {}, {}, esp + "/loader/loader.conf", true}};
    BootContent::populate(snapshot, [](const QString &, const QStringList &) { return QString("[]"); });
    const auto path = esp + "/loader/entries/new.conf";
    const auto create = menuWrite(path, {}, {{"format", "bls"}, {"values", QJsonObject{{"title", "Linux"}, {"linux", "/kernel"}, {"initrd", QJsonArray{"/initrd"}}}}});
    const BootOperation selected{"systemd-setting", esp + "|default", "new.conf", "old.conf", {}, {}, BootBls::fingerprint("[]")};
    QString error;
    QVERIFY2(preflight({selected, create}, snapshot, &error), qPrintable(error));
    BootBackup backup;
    QVERIFY2(backup.prepare({selected, create}, snapshot, directory.path() + "/backup", &error), qPrintable(error));
    QJsonObject outcomes;
    QVERIFY(apply({selected, create}, backup, snapshot, &outcomes));
    QVERIFY(QFileInfo::exists(path));
    QCOMPARE(BootSettings::read(LoaderKind::SystemdBoot, esp, "default", &error), QString("new.conf")); QVERIFY(error.isEmpty());
    snapshot.installations[0].settingValues["default"] = "new.conf";
    const auto text = BootMenus::readText(path, &error); QVERIFY(error.isEmpty());
    const auto remove = menuWrite(path, text, {{"format", "bls"}, {"remove", true}});
    QVERIFY(!preflight({remove}, snapshot, &error)); QVERIFY(!error.isEmpty());
    QVERIFY2(preflight({remove, {"systemd-setting", esp + "|default", "", "new.conf"}}, snapshot, &error), qPrintable(error));
}

void BootInstallTest::validateProjectedImageSettingsBeforeWriting()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto root = directory.path() + "/root";
    const auto esp = root + "/ordinary-output";
    const auto path = BootDeploy::configurationPath(root);
    const auto binary = esp + "/EFI/CatOS/linux/hash/vmlinuz.efi";
    QVERIFY(writeMenuFixture(binary, "existing kernel"));
    QVERIFY(writeMenuFixture(path, "[boot]\nmethod=efistub\n"));
    QVERIFY(writeMenuFixture(root + "/usr/bin/catos-firmware-boot-update", "#!/bin/sh\nexit 0\n"));
    QVERIFY(QFile::setPermissions(root + "/usr/bin/catos-firmware-boot-update", QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    QVERIFY(writeMenuFixture(directory.path() + "/arch-chroot", "#!/bin/sh\nprintf '%s' '{\"loader\":\"efistub\",\"esp\":\"/ordinary-output\",\"cmdline\":\"old\",\"default_kernel\":\"linux\",\"label_prefix\":\"CatOS\"}'\n"));
    QVERIFY(QFile::setPermissions(directory.path() + "/arch-chroot", QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const auto oldPath = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", oldPath); });
    qputenv("PATH", directory.path().toUtf8());
    BootSnapshot snapshot;
    snapshot.repairRoots = {root};
    snapshot.installations = {{LoaderKind::EfiStub, "image", esp, {}, binary, path, true}};
    BootContent::populate(snapshot);
    QString error;
    QVERIFY2(preflight({{"efistub-setting", path + "|cmdline", "new", "old"}}, snapshot, &error), qPrintable(error));
    const auto before = BootMenus::readText(path, &error); QVERIFY(error.isEmpty());
    QVERIFY(!preflight({{"efistub-setting", path + "|initramfs", "/missing/initramfs", ""}}, snapshot, &error)); QVERIFY(!error.isEmpty());
    QCOMPARE(BootMenus::readText(path, &error), before); QVERIFY(error.isEmpty());
    const auto hash = QJsonDocument::fromJson(snapshot.installations[0].menuContent.toUtf8()).object().value("fingerprint").toString();
    QVERIFY2(preflight({{"rebuild-image", path, "efistub", hash}}, snapshot, &error), qPrintable(error));
    QVERIFY(!preflight({{"rebuild-image", path, "efistub", "stale"}}, snapshot, &error)); QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(BootInstallTest)
#include "BootInstallTest.moc"
