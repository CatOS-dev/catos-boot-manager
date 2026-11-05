#include "pages/MethodsPage.h"
#include "model/BootPlan.h"

#include <QComboBox>
#include <QFile>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

class MethodsPageTest : public QObject {
    Q_OBJECT
private slots:
    void stageInstanceSelectionAndRejectChangedTarget();
};

void MethodsPageTest::stageInstanceSelectionAndRejectChangedTarget()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile program(directory.path() + "/grubx64.efi");
    QVERIFY(program.open(QIODevice::WriteOnly));
    QCOMPARE(program.write("test PE image"), qint64(13));
    program.close();
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.secureBoot = SecureBootState::Disabled;
    snapshot.bootOrder = {"0001", "0002"};
    snapshot.firmware = {{"0001", "Old", "\\EFI\\old.efi", "uuid", true, true},
                         {"0002", "New", "\\EFI\\grubx64.efi", "uuid", true, false}};
    BootInstallation installation;
    installation.kind = LoaderKind::Grub;
    installation.id = "grub:uuid:efi/grubx64.efi";
    installation.esp = directory.path();
    installation.espPartuuid = "uuid";
    installation.binary = program.fileName();
    installation.binaryPresent = true;
    installation.firmwareEntries = {"0002"};
    installation.imageType = "pe";
    snapshot.installations = {installation};
    BootTransaction transaction;
    MethodsPage page;
    page.updateSnapshot(snapshot);
    connect(&page, &MethodsPage::operationRequested, &page, [&](const BootOperation &op) {
        transaction.stage(op);
        page.updateTransaction(transaction);
    });
    auto *setDefault = page.findChild<QPushButton *>("methodSetDefault");
    auto *once = page.findChild<QPushButton *>("methodBootOnce");
    QVERIFY(setDefault);
    QVERIFY(once);
    QVERIFY(setDefault->isEnabled());
    setDefault->click();
    QCOMPARE(transaction.count(), 1);
    QCOMPARE(transaction.operations().first().value, QString("0002,0001"));
    QCOMPARE(transaction.operations().first().expectedEntry, snapshot.firmware[1].identity());
    QVERIFY(!setDefault->isEnabled());
    once->click();
    QCOMPARE(transaction.count(), 2);
    QVERIFY(!once->isEnabled());
    QString error;
    const auto decoded = BootTransaction::decode(transaction.encoded(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(decoded.size(), 2);
    QCOMPARE(decoded[1].expectedEntry, snapshot.firmware[1].identity());
    QVERIFY(BootPlan::firmwareConflict(decoded, snapshot).isEmpty());
    BootSnapshot changed = snapshot;
    changed.firmware[1].partitionUuid = "another-esp";
    QVERIFY(!BootPlan::firmwareConflict(decoded, changed).isEmpty());
    changed = snapshot;
    changed.installations[0].imageType = "unknown";
    QVERIFY(!BootPlan::firmwareConflict(decoded, changed).isEmpty());
    changed = snapshot;
    changed.secureBoot = SecureBootState::Enabled;
    QVERIFY(!BootPlan::firmwareConflict(decoded, changed).isEmpty());
    page.updateSnapshot(changed);
    QVERIFY(!setDefault->isEnabled());
    QVERIFY(!once->isEnabled());
    QVERIFY(program.remove());
    QVERIFY(!BootPlan::firmwareConflict(decoded, snapshot).isEmpty());
    page.updateTransaction({});
    page.updateSnapshot(snapshot);
    QVERIFY(!setDefault->isEnabled());
    QVERIFY(!once->isEnabled());
}

QTEST_MAIN(MethodsPageTest)
#include "MethodsPageTest.moc"
