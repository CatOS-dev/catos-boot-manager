#include "services/BootBls.h"
#include "services/BootContent.h"
#include "model/BootPlan.h"
#include "pages/MenuPage.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

namespace {
bool writeFixture(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
}

class SystemdMenuTest : public QObject {
    Q_OBJECT
private slots:
    void displayRealBootctlEntriesAndQueueExactDefault();
    void rejectDuplicateAndForeignEntryIdentities();
};

void SystemdMenuTest::displayRealBootctlEntriesAndQueueExactDefault()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString esp = directory.path();
    QVERIFY(QDir().mkpath(esp + "/loader/entries"));
    QVERIFY(writeFixture(esp + "/loader/loader.conf", "default catos-one.conf\ntimeout 3\n"));
    const QByteArray entry = "title CatOS Linux\nlinux /vmlinuz-test\ninitrd /initramfs-test.img\noptions root=UUID=test ro\n";
    QVERIFY(writeFixture(esp + "/loader/entries/catos-one.conf", entry));
    QVERIFY(writeFixture(esp + "/loader/entries/catos-two.conf", entry));
    QVERIFY(writeFixture(esp + "/vmlinuz-test", "kernel test asset"));
    QVERIFY(writeFixture(esp + "/initramfs-test.img", "initramfs test asset"));
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::SystemdBoot, "systemd", esp, "uuid", {}, esp + "/loader/loader.conf", true}};
    int exitCode = -1;
    bool completed = false;
    QString error;
    BootContent::populate(snapshot, [&](const QString &program, const QStringList &arguments) {
        QProcess command;
        auto environment = QProcessEnvironment::systemEnvironment();
        // Only this contract test relaxes filesystem checks for its temporary ESP fixture.
        environment.insert("SYSTEMD_RELAX_ESP_CHECKS", "1");
        environment.insert("SYSTEMD_RELAX_XBOOTLDR_CHECKS", "1");
        environment.insert("LC_ALL", "C");
        command.setProcessEnvironment(environment);
        command.start(program, arguments);
        completed = command.waitForFinished();
        exitCode = command.exitCode();
        error = QString::fromUtf8(command.readAllStandardError());
        return QString::fromUtf8(command.readAllStandardOutput());
    });
    QVERIFY2(completed, qPrintable(error));
    QCOMPARE(exitCode, 0);
    QVERIFY(snapshot.installations.first().menuMessage.isEmpty());
    QString parseError;
    const auto entries = BootBls::entries(snapshot.installations.first().menuContent, esp, &parseError);
    QVERIFY2(parseError.isEmpty(), qPrintable(parseError));
    QCOMPARE(entries.size(), 2);
    MenuPage page;
    page.updateSnapshot(snapshot);
    auto *tree = page.findChild<QTreeWidget *>("bootMenuEntries");
    auto *select = page.findChild<QPushButton *>("systemdConfiguredDefault");
    QVERIFY(tree);
    QVERIFY(select);
    QCOMPARE(tree->topLevelItemCount(), 2);
    QVERIFY(select->isEnabled());
    QTreeWidgetItem *other = nullptr;
    QTreeWidgetItem *original = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto *item = tree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == "catos-two.conf") other = item;
        if (item->data(0, Qt::UserRole).toString() == "catos-one.conf") original = item;
    }
    QVERIFY(other);
    QVERIFY(original);
    tree->setCurrentItem(other);
    BootTransaction transaction;
    connect(&page, &MenuPage::operationRequested, &page, [&](const BootOperation &op) { transaction.stage(op); });
    select->click();
    QCOMPARE(transaction.count(), 1);
    const auto operation = transaction.operations().first();
    QCOMPARE(operation.kind, QString("systemd-setting"));
    QCOMPARE(operation.target, esp + "|default");
    QCOMPARE(operation.expected, QString("catos-one.conf"));
    QCOMPARE(operation.value, QString("catos-two.conf"));
    QVERIFY(!operation.expectedMenu.isEmpty());
    QVERIFY(BootBls::defaultTargetMatches(snapshot, esp, operation.value, operation.expectedMenu));
    BootSnapshot changed = snapshot;
    changed.installations[0].menuContent = "[]";
    QVERIFY(!BootBls::defaultTargetMatches(changed, esp, operation.value, operation.expectedMenu));
    QVERIFY(!BootBls::defaultTargetMatches(snapshot, esp, "missing.conf", operation.expectedMenu));
    tree->setCurrentItem(original);
    select->click();
    QCOMPARE(transaction.count(), 0);
    snapshot.systemdDefaultOverride = "efi-override.conf";
    QVERIFY(!BootPlan::firmwareConflict({operation}, snapshot).isEmpty());
    page.updateSnapshot(snapshot);
    QVERIFY(!select->isEnabled());
}

void SystemdMenuTest::rejectDuplicateAndForeignEntryIdentities()
{
    QString error;
    const QString entries = R"([{"type":"type1","id":"same.conf","root":"/esp","path":"/esp/loader/entries/same.conf"},{"type":"type1","id":"same.conf","root":"/esp","path":"/esp/loader/entries/other.conf"}])";
    QVERIFY(BootBls::entries(entries, "/esp", &error).isEmpty());
    QVERIFY(!error.isEmpty());
    const QString foreign = R"([{"type":"loader","id":"reported","path":"/sys/efivars"},{"type":"type1","id":"other.conf","root":"/other-esp","path":"/other-esp/loader/entries/other.conf"}])";
    QVERIFY(BootBls::entries(foreign, "/esp", &error).isEmpty());
    QVERIFY(error.isEmpty());
    QVERIFY(!BootBls::validId("../other.conf"));
    QVERIFY(!BootBls::validId("*.conf"));
    QVERIFY(BootBls::validId("catos-linux+3.conf"));
}

QTEST_MAIN(SystemdMenuTest)
#include "SystemdMenuTest.moc"
