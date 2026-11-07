#include "pages/SettingsPage.h"
#include "services/BootSettings.h"
#include "services/BootContent.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

class SettingsDraftTest : public QObject {
    Q_OBJECT
private slots:
    void chooseInstalledKernelWithoutImagePaths();
    void detectDraftAndAcknowledgeApplied();
    void booleanChoices_data();
    void booleanChoices();
    void chooseSwitchesAndPreserveDrafts();
    void preserveDraftAndOriginalValue();
    void rejectUnboundGeneratorConfiguration();
    void editOnlySelectedRefindConfiguration();
    void editRefindIncludesAndManualBlocks();
    void readGrubQuotedValuesWithComments();
};

void SettingsDraftTest::chooseInstalledKernelWithoutImagePaths()
{
    BootInstallation loader;
    loader.kind = LoaderKind::Uki; loader.id = "uki:/test"; loader.config = "/target/etc/catos/firmware-boot.conf";
    loader.settingsEditable = true; loader.configPresent = true;
    loader.settingValues = {{"root", "/target"}, {"default_kernel", "linux-6.1"}};
    BootSnapshot snapshot; snapshot.installations = {loader};
    snapshot.kernels = {
        QJsonObject{{"root", "/target"}, {"name", "linux"}, {"version", "6.1"}},
        QJsonObject{{"root", "/target"}, {"name", "linux"}, {"version", "6.2"}},
        QJsonObject{{"root", "/other"}, {"name", "foreign"}, {"version", "7.0"}}
    };
    SettingsPage page; page.updateSnapshot(snapshot);
    auto *choice = page.findChild<QComboBox *>("default_kernel"); QVERIFY(choice);
    QVERIFY(!choice->isEditable()); QCOMPARE(choice->count(), 3);
    QCOMPARE(choice->currentData().toString(), QString("linux-6.1"));
    QCOMPARE(choice->itemData(2).toString(), QString("linux-6.2"));
    QVERIFY(!page.findChild<QLineEdit *>("kernel"));
    QVERIFY(!page.findChild<QLineEdit *>("initramfs"));
    BootTransaction transaction;
    connect(&page, &SettingsPage::operationRequested, &page, [&](const BootOperation &op) { transaction.stage(op); });
    choice->setCurrentIndex(2);
    QCOMPARE(transaction.count(), 1);
    QCOMPARE(transaction.operations()[0].target, loader.config + "|default_kernel");
    QCOMPARE(transaction.operations()[0].value, QString("linux-6.2"));
}

void SettingsDraftTest::readGrubQuotedValuesWithComments()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("grub");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("GRUB_THEME='a\\'b' # keep this comment\\n") > 0);
    file.close();
    QString error;
    QCOMPARE(BootSettings::read(LoaderKind::Grub, path, "GRUB_THEME", &error), QString("a'b"));
    QVERIFY(error.isEmpty());
}

void SettingsDraftTest::detectDraftAndAcknowledgeApplied()
{
    BootInstallation loader;
    loader.kind = LoaderKind::Grub; loader.id = "grub";
    loader.configPresent = true; loader.settingsEditable = true;
    loader.config = "/etc/default/grub";
    loader.settingValues = {{"GRUB_TIMEOUT", "3"}};
    BootSnapshot snapshot; snapshot.installations = {loader};
    SettingsPage page; page.updateSnapshot(snapshot);
    QVERIFY(!page.hasDraftChanges());
    auto *timeout = page.findChild<QLineEdit *>("GRUB_TIMEOUT"); QVERIFY(timeout);
    timeout->setText("5");
    QVERIFY(page.hasDraftChanges());
    BootTransaction transaction;
    transaction.stage({"grub-setting", "GRUB_TIMEOUT", "5", "3"});
    page.acknowledgeApplied(transaction);
    QVERIFY(!page.hasDraftChanges());
    timeout->setText("7");
    QVERIFY(page.hasDraftChanges());
    page.updateSnapshot(snapshot);
    QVERIFY(!page.hasDraftChanges());
}

void SettingsDraftTest::booleanChoices_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("key");
    for (const auto &key : {"GRUB_DISABLE_OS_PROBER", "GRUB_DISABLE_LINUX_UUID"})
        QTest::newRow(key) << int(LoaderKind::Grub) << QString(key);
    for (const auto &key : {"quiet", "graphics", "remember_last_entry"})
        QTest::newRow(key) << int(LoaderKind::Limine) << QString(key);
    for (const auto &key : {"editor", "auto-entries", "auto-firmware", "beep"})
        QTest::newRow(key) << int(LoaderKind::SystemdBoot) << QString(key);
}

void SettingsDraftTest::booleanChoices()
{
    QFETCH(int, kind); QFETCH(QString, key);
    BootInstallation loader;
    loader.kind = LoaderKind(kind); loader.id = "test"; loader.settingsEditable = true;
    loader.configPresent = true;
    loader.config = loader.kind == LoaderKind::Grub ? "/etc/default/grub" : "/test/loader.conf";
    loader.esp = loader.kind == LoaderKind::Grub ? QString() : QString("/test");
    BootSnapshot snapshot; snapshot.installations = {loader};
    SettingsPage page; page.updateSnapshot(snapshot);
    auto *choice = page.findChild<QComboBox *>(key); QVERIFY(choice);
    QVERIFY(!choice->isEditable()); QCOMPARE(choice->count(), 3);
    QCOMPARE(choice->currentData().toString(), QString());
    BootTransaction transaction;
    connect(&page, &SettingsPage::operationRequested, &page, [&](const BootOperation &op) { transaction.stage(op); });
    choice->setCurrentIndex(1);
    QCOMPARE(transaction.count(), 1);
    const QString enabled = loader.kind == LoaderKind::Grub ? "true" : "yes";
    const QString disabled = loader.kind == LoaderKind::Grub ? "false" : "no";
    QCOMPARE(transaction.operations()[0].value, enabled);
    choice->setCurrentIndex(2);
    QCOMPARE(transaction.operations()[0].value, disabled);
    QVERIFY(BootSettings::allowed(loader.kind, key, QString()));
    choice->setCurrentIndex(0);
    QCOMPARE(transaction.count(), 0);
}

void SettingsDraftTest::chooseSwitchesAndPreserveDrafts()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString config = dir.path() + "/limine.conf";
    QFile file(config); QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("quiet: no\ngraphics: yes\nwallpaper_style: tiled\n/Linux\nprotocol: linux\npath: boot():/vmlinuz\n") > 0);
    file.close();
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::Limine, "limine", dir.path(), {}, {}, config, true}};
    BootContent::populate(snapshot);
    SettingsPage page; page.updateSnapshot(snapshot);
    BootTransaction transaction;
    connect(&page, &SettingsPage::operationRequested, &page, [&](const BootOperation &op) { transaction.stage(op); });

    const auto field = [&](const QString &key) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return page.findChild<QComboBox *>(key);
    };
    auto *quiet = field("quiet"); QVERIFY(quiet); QVERIFY(!quiet->isEditable());
    QCOMPARE(quiet->currentData().toString(), QString("no"));
    quiet->setCurrentIndex(quiet->findData("yes"));
    auto *remember = field("remember_last_entry"); QVERIFY(remember);
    QCOMPARE(remember->currentData().toString(), QString());
    auto *style = field("wallpaper_style"); QVERIFY(style);
    QCOMPARE(style->currentData().toString(), QString("tiled"));
    style->setCurrentIndex(style->findData("centered"));
    page.selectMethod(LoaderKind::Uki); page.selectMethod(LoaderKind::Limine);
    quiet = field("quiet"); QVERIFY(quiet);
    QCOMPARE(quiet->currentData().toString(), QString("yes"));
    style = field("wallpaper_style"); QVERIFY(style);
    QCOMPARE(style->currentData().toString(), QString("centered"));

    QCOMPARE(transaction.count(), 2);
    QCOMPARE(transaction.operations()[0].target, config + "|quiet");
    QCOMPARE(transaction.operations()[0].value, QString("yes"));
    const auto quietOperation = transaction.operations()[0];
    transaction.remove(quietOperation.key());
    page.discardOperation(quietOperation);
    QCOMPARE(quiet->currentData().toString(), QString("no"));
    page.reflectOperation({"limine-setting", config + "|remember_last_entry", "yes"});
    remember = field("remember_last_entry"); QVERIFY(remember);
    QCOMPARE(remember->currentData().toString(), QString("yes"));
    page.discardOperation({"limine-setting", config + "|remember_last_entry", "yes"});
    QCOMPARE(remember->currentData().toString(), QString());
    quiet->setCurrentIndex(quiet->findData(QString()));
    QCOMPARE(transaction.operations().last().value, QString());
}

void SettingsDraftTest::preserveDraftAndOriginalValue()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstEsp = directory.path() + "/first";
    const QString secondEsp = directory.path() + "/second";
    QVERIFY(QDir().mkpath(firstEsp + "/loader"));
    QVERIFY(QDir().mkpath(secondEsp + "/loader"));
    QFile first(firstEsp + "/loader/loader.conf");
    QVERIFY(first.open(QIODevice::WriteOnly));
    QCOMPARE(first.write("timeout 3\neditor yes\n"), qint64(21));
    first.close();
    QFile second(secondEsp + "/loader/loader.conf");
    QVERIFY(second.open(QIODevice::WriteOnly));
    QCOMPARE(second.write("timeout 7\neditor no\n"), qint64(20));
    second.close();
    BootSnapshot snapshot;
    snapshot.installations = {
        {LoaderKind::SystemdBoot, "first", firstEsp, {}, {}, first.fileName(), true},
        {LoaderKind::SystemdBoot, "second", secondEsp, {}, {}, second.fileName(), true}
    };
    SettingsPage page;
    BootContent::populate(snapshot);
    page.updateSnapshot(snapshot);
    BootTransaction transaction;
    connect(&page, &SettingsPage::operationRequested, &page, [&](const BootOperation &op) {
        transaction.stage(op);
    });

    auto *selector = page.findChild<QComboBox *>("loaderInstallation");
    QVERIFY(selector);
    auto currentTimeout = [&]() -> QLineEdit * {
        // Old form fields are removed with deleteLater when changing instances.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return page.findChild<QLineEdit *>("timeout");
    };
    auto *timeout = currentTimeout();
    QVERIFY(timeout);
    QCOMPARE(timeout->text(), QString("3"));
    timeout->setText("5");
    page.selectMethod(LoaderKind::Uki);
    page.selectMethod(LoaderKind::SystemdBoot);
    timeout = currentTimeout();
    QVERIFY(timeout);
    QCOMPARE(timeout->text(), QString("5"));
    selector->setCurrentIndex(1);
    timeout = currentTimeout();
    QVERIFY(timeout);
    QCOMPARE(timeout->text(), QString("7"));
    timeout->setText("9");
    QVERIFY(first.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(first.write("timeout 4\neditor yes\n"), qint64(21));
    first.close();
    selector->setCurrentIndex(0);
    timeout = currentTimeout();
    QVERIFY(timeout);
    QCOMPARE(timeout->text(), QString("5"));

    QCOMPARE(transaction.count(), 2);
    const auto operation = transaction.operations().first();
    QCOMPARE(operation.expected, QString("3"));
    QCOMPARE(operation.value, QString("5"));
    QCOMPARE(operation.target, firstEsp + "|timeout");
    transaction.remove(operation.key());
    page.discardOperation(operation);
    QCOMPARE(timeout->text(), QString("3"));
    QCOMPARE(transaction.count(), 1);
    selector->setCurrentIndex(1);
    timeout = currentTimeout();
    QVERIFY(timeout);
    QCOMPARE(timeout->text(), QString("9"));
    BootContent::populate(snapshot);
    page.updateSnapshot(snapshot, true);
    timeout = currentTimeout();
    QVERIFY(timeout);
    QCOMPARE(timeout->text(), QString("9"));
    page.updateSnapshot(snapshot);
    timeout = currentTimeout();
    QVERIFY(timeout);
    QCOMPARE(timeout->text(), QString("7"));
    selector->setCurrentIndex(0);
    timeout = currentTimeout();
    QVERIFY(timeout);
    QCOMPARE(timeout->text(), QString("4"));
}

void SettingsDraftTest::rejectUnboundGeneratorConfiguration()
{
    BootSnapshot snapshot;
    snapshot.installations = {
        {LoaderKind::Grub, "other-grub", "/other-esp", "uuid", "/other-esp/EFI/grub/grubx64.efi", "/etc/default/grub", true},
        {LoaderKind::Limine, "other-limine", "/other-esp", "uuid", "/other-esp/EFI/limine/limine.efi", "/etc/default/limine", true}
    };
    SettingsPage page;
    BootContent::populate(snapshot);
    page.updateSnapshot(snapshot);
    QSignalSpy operations(&page, &SettingsPage::operationRequested);
    QVERIFY(!page.findChild<QLineEdit *>("GRUB_TIMEOUT"));
    QCOMPARE(operations.count(), 0);
    page.selectMethod(LoaderKind::Limine);
    QCOMPARE(operations.count(), 0);
}

void SettingsDraftTest::editOnlySelectedRefindConfiguration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = directory.path() + "/first";
    const QString second = directory.path() + "/second";
    QVERIFY(QDir().mkpath(first));
    QVERIFY(QDir().mkpath(second));
    QFile config(first + "/refind.conf");
    const QByteArray original = "# keep this comment\ntimeout 5 # seconds\ndefault_selection \"CatOS Linux\"\nscanfor internal,external\n";
    QVERIFY(config.open(QIODevice::WriteOnly));
    QCOMPARE(config.write(original), qint64(original.size()));
    config.close();
    QVERIFY(QFile::copy(config.fileName(), second + "/refind.conf"));
    BootSnapshot snapshot;
    snapshot.installations = {
        {LoaderKind::Refind, "first", first, "first-uuid", first + "/refind_x64.efi", config.fileName(), true},
        {LoaderKind::Refind, "second", second, "second-uuid", second + "/refind_x64.efi", second + "/refind.conf", true}
    };
    SettingsPage page;
    BootContent::populate(snapshot);
    page.updateSnapshot(snapshot);
    BootTransaction transaction;
    connect(&page, &SettingsPage::operationRequested, &page, [&](const BootOperation &op) { transaction.stage(op); });

    auto *timeout = page.findChild<QLineEdit *>("timeout");
    QVERIFY(timeout);
    QVERIFY(!page.findChild<QLineEdit *>("default_selection"));
    QCOMPARE(timeout->text(), QString("5"));
    timeout->setText("8");

    QCOMPARE(transaction.count(), 1);
    QCOMPARE(transaction.operations()[0].kind, QString("refind-setting"));
    QCOMPARE(transaction.operations()[0].target, config.fileName() + "|timeout");
    QCOMPARE(transaction.operations()[0].expected, QString("5"));
    QString error;
    QVERIFY2(BootSettings::write(LoaderKind::Refind, config.fileName(), "timeout", "8", &error), qPrintable(error));
    QVERIFY2(BootSettings::write(LoaderKind::Refind, config.fileName(), "default_selection", "CatOS LTS", &error), qPrintable(error));
    QString readError;
    QCOMPARE(BootSettings::read(LoaderKind::Refind, config.fileName(), "timeout", &readError), QString("8"));
    QVERIFY(readError.isEmpty());
    QCOMPARE(BootSettings::read(LoaderKind::Refind, config.fileName(), "default_selection", &readError), QString("CatOS LTS"));
    QVERIFY(readError.isEmpty());
    QFile other(second + "/refind.conf");
    QVERIFY(other.open(QIODevice::ReadOnly));
    QCOMPARE(other.readAll(), original);
    QVERIFY(config.open(QIODevice::ReadOnly));
    const QByteArray changed = config.readAll();
    config.close();
    QVERIFY(changed.contains("# keep this comment"));
    QVERIFY(changed.contains("scanfor internal,external"));
    QVERIFY2(BootSettings::write(LoaderKind::Refind, config.fileName(), "default_selection", "", &error), qPrintable(error));
    QVERIFY(config.open(QIODevice::ReadOnly));
    QVERIFY(!config.readAll().contains("default_selection"));
}

void SettingsDraftTest::editRefindIncludesAndManualBlocks()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile config(directory.path() + "/refind.conf");
    const QByteArray included = "timeout 5\ninclude themes/dark/theme.conf\n";
    QVERIFY(config.open(QIODevice::WriteOnly));
    QCOMPARE(config.write(included), qint64(included.size()));
    config.close();
    QString error;
    QVERIFY(!BootSettings::refindEditable(config.fileName(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!BootSettings::write(LoaderKind::Refind, config.fileName(), "timeout", "8", &error));
    QVERIFY(config.open(QIODevice::ReadOnly));
    QCOMPARE(config.readAll(), included);
    config.close();
    const QByteArray manual = "timeout 5\nmenuentry CatOS {\n loader /vmlinuz\n}\n";
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(config.write(manual), qint64(manual.size()));
    config.close();
    QVERIFY2(BootSettings::refindEditable(config.fileName(), &error), qPrintable(error));
    QVERIFY2(BootSettings::write(LoaderKind::Refind, config.fileName(), "timeout", "8", &error), qPrintable(error));
    QVERIFY(config.open(QIODevice::ReadOnly));
    QCOMPARE(config.readAll(), QByteArray("timeout 8\nmenuentry CatOS {\n loader /vmlinuz\n}\n"));
    QVERIFY(!BootSettings::allowed(LoaderKind::Refind, "default_selection", "Linux\"\ninclude bad"));
    QVERIFY(!BootSettings::allowed(LoaderKind::Refind, "resolution", "1920x1080"));
    QVERIFY(BootSettings::allowed(LoaderKind::Refind, "resolution", "1920 1080"));
}

QTEST_MAIN(SettingsDraftTest)
#include "SettingsDraftTest.moc"
