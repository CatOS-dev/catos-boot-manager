#include "pages/SettingsPage.h"
#include "services/BootSettings.h"

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
    void preserveDraftAndOriginalValue();
    void rejectUnboundGeneratorConfiguration();
    void editOnlySelectedRefindConfiguration();
    void rejectRefindIncludesAndManualBlocks();
};

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
    page.updateSnapshot(snapshot);
    auto *selector = page.findChild<QComboBox *>("loaderInstallation");
    auto *queue = page.findChild<QPushButton *>("queueSettings");
    QVERIFY(selector);
    QVERIFY(queue);
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
    QVERIFY(!queue->isEnabled());
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
    BootTransaction transaction;
    connect(&page, &SettingsPage::operationRequested, &page, [&](const BootOperation &op) {
        transaction.stage(op);
    });
    queue->click();
    QCOMPARE(transaction.count(), 1);
    const auto operation = transaction.operations().first();
    QCOMPARE(operation.expected, QString("3"));
    QCOMPARE(operation.value, QString("5"));
    QCOMPARE(operation.target, firstEsp + "|timeout");
    page.discardOperation(operation);
    QCOMPARE(timeout->text(), QString("3"));
    queue->click();
    QCOMPARE(transaction.count(), 0);
    selector->setCurrentIndex(1);
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
    page.updateSnapshot(snapshot);
    QSignalSpy operations(&page, &SettingsPage::operationRequested);
    auto *queue = page.findChild<QPushButton *>("queueSettings");
    QVERIFY(queue);
    QVERIFY(!queue->isEnabled());
    queue->click();
    QCOMPARE(operations.count(), 0);
    page.selectMethod(LoaderKind::Limine);
    QVERIFY(!queue->isEnabled());
    queue->click();
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
    page.updateSnapshot(snapshot);
    auto *timeout = page.findChild<QLineEdit *>("timeout");
    auto *selection = page.findChild<QLineEdit *>("default_selection");
    auto *queue = page.findChild<QPushButton *>("queueSettings");
    QVERIFY(timeout);
    QVERIFY(selection);
    QVERIFY(queue);
    QVERIFY(queue->isEnabled());
    QCOMPARE(timeout->text(), QString("5"));
    QCOMPARE(selection->text(), QString("CatOS Linux"));
    timeout->setText("8");
    selection->setText("CatOS LTS");
    BootTransaction transaction;
    connect(&page, &SettingsPage::operationRequested, &page, [&](const BootOperation &op) { transaction.stage(op); });
    queue->click();
    QCOMPARE(transaction.count(), 2);
    QCOMPARE(transaction.operations()[0].kind, QString("refind-setting"));
    QCOMPARE(transaction.operations()[0].target, config.fileName() + "|timeout");
    QCOMPARE(transaction.operations()[0].expected, QString("5"));
    QString error;
    QVERIFY2(BootSettings::write(LoaderKind::Refind, config.fileName(), "timeout", "8", &error), qPrintable(error));
    QVERIFY2(BootSettings::write(LoaderKind::Refind, config.fileName(), "default_selection", "CatOS LTS", &error), qPrintable(error));
    QCOMPARE(BootSettings::read(LoaderKind::Refind, config.fileName(), "timeout"), QString("8"));
    QCOMPARE(BootSettings::read(LoaderKind::Refind, config.fileName(), "default_selection"), QString("CatOS LTS"));
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

void SettingsDraftTest::rejectRefindIncludesAndManualBlocks()
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
    QVERIFY(!BootSettings::refindEditable(config.fileName(), &error));
    QVERIFY(!BootSettings::write(LoaderKind::Refind, config.fileName(), "timeout", "8", &error));
    QVERIFY(config.open(QIODevice::ReadOnly));
    QCOMPARE(config.readAll(), manual);
    QVERIFY(!BootSettings::allowed(LoaderKind::Refind, "default_selection", "Linux\"\ninclude bad"));
    QVERIFY(!BootSettings::allowed(LoaderKind::Refind, "resolution", "1920x1080"));
    QVERIFY(BootSettings::allowed(LoaderKind::Refind, "resolution", "1920 1080"));
}

QTEST_MAIN(SettingsDraftTest)
#include "SettingsDraftTest.moc"
