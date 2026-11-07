#include "model/BootTransaction.h"
#include "model/BootPlan.h"
#include "pages/FirmwarePage.h"
#include "pages/PendingPage.h"

#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QtTest>

class BootTransactionTest : public QObject {
    Q_OBJECT
private slots:
    void toggleAndAutomaticallyQueueOrder();
    void previewRenameAndRemoval();
    void restoreFirmwareValues();
    void distinctMenuConfigurations();
    void cancelIndividualOperation();
    void clearStagedBootNext();
    void restoreOrderThroughPage();
    void rejectDeletedNextBoot();
    void clearNextBeforeRemoval();
    void rejectEditOfRemovedEntry();
    void validateRemainingOrder();
    void restoreBackupAlone();
};

void BootTransactionTest::toggleAndAutomaticallyQueueOrder()
{
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.bootOrder = {"0001", "0002"};
    snapshot.firmware = {{"0001", "First", "", "", true, true},
                         {"0002", "Second", "", "", true, false}};
    FirmwarePage page;
    BootTransaction transaction;
    page.updateSnapshot(snapshot);
    connect(&page, &FirmwarePage::operationRequested, &page, [&](const BootOperation &op) {
        transaction.stage(op);
        page.updateTransaction(transaction);
    });
    auto *table = page.findChild<QTableWidget *>();
    auto *toggle = page.findChild<QPushButton *>("toggleBootEntry");
    QVERIFY(table); QVERIFY(toggle);
    table->selectRow(1);
    page.findChild<QPushButton *>("moveBootEntryUp")->click();
    QCOMPARE(transaction.operations().first().value, QString("0002,0001"));
    toggle->click();
    QCOMPARE(transaction.count(), 2);
    QCOMPARE(transaction.operations().last().value, QString("0"));
    QCOMPARE(table->item(0, 3)->text(), QString("Disabled · Pending change"));
    QCOMPARE(toggle->text(), QString("Enable"));
    toggle->click();
    QCOMPARE(transaction.count(), 1);
    QCOMPARE(table->item(0, 3)->text(), QString("Enabled"));
    QCOMPARE(toggle->text(), QString("Disable"));
    page.findChild<QPushButton *>("bootEntryOnce")->click();
    QCOMPARE(table->item(0, 0)->data(Qt::UserRole).toString(), QString("0002"));
    QCOMPARE(transaction.operations().first().value, QString("0002,0001"));
    QCOMPARE(transaction.operations().last().kind, QString("firmware-next"));
}

void BootTransactionTest::previewRenameAndRemoval()
{
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.bootOrder = {"0001", "0002"};
    snapshot.firmware = {{"0001", "First", "", "", true, true}, {"0002", "Second", "", "", true, false}};
    FirmwarePage page;
    page.updateSnapshot(snapshot);
    BootTransaction transaction;
    transaction.stage({"firmware-rename", "0002", "New name", "identity", "Rename"});
    page.updateTransaction(transaction);
    auto *table = page.findChild<QTableWidget *>(); QVERIFY(table);
    QCOMPARE(table->item(1, 1)->text(), QString("New name"));
    QVERIFY(table->item(1, 3)->text().contains("Pending rename"));
    transaction.clear();
    transaction.stage({"firmware-remove", "0002", "", "identity", "Remove"});
    page.updateTransaction(transaction);
    QVERIFY(table->item(1, 3)->text().contains("Pending removal"));
    table->selectRow(1);
    QVERIFY(!page.findChild<QPushButton *>("toggleBootEntry")->isEnabled());
    page.resize(900, 650); page.show();
    QVERIFY(QTest::qWaitForWindowActive(&page));
    QVERIFY(page.grab().save("/tmp/catos-ux-firmware.png"));
    transaction.clear(); page.updateTransaction(transaction);
    QCOMPARE(table->item(1, 1)->text(), QString("Second"));
    QCOMPARE(table->item(1, 3)->text(), QString("Enabled"));
}

void BootTransactionTest::restoreFirmwareValues()
{
    BootTransaction transaction;
    transaction.stage({"firmware-order", "BootOrder", "0002,0001", "0001,0002", "Order"});
    QCOMPARE(transaction.count(), 1);
    transaction.stage({"firmware-order", "BootOrder", "0001,0002", "0001,0002", "Order"});
    QCOMPARE(transaction.count(), 0);
    transaction.stage({"firmware-next", "BootNext", "0002", "", "Next"});
    QCOMPARE(transaction.count(), 1);
    transaction.stage({"firmware-next", "BootNext", "", "", "Next"});
    QCOMPARE(transaction.count(), 0);
}

void BootTransactionTest::distinctMenuConfigurations()
{
    BootTransaction transaction;
    transaction.stage({"menu-write", "/first/limine.conf", "first contents", "hash", "Edit first menu"});
    transaction.stage({"menu-write", "/second/limine.conf", "second contents", "hash", "Edit second menu"});
    QCOMPARE(transaction.count(), 2);
    QString error;
    const auto decoded = BootTransaction::decode(transaction.encoded(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(decoded.size(), 2);
    QVERIFY(decoded[0].key() != decoded[1].key());
}

void BootTransactionTest::cancelIndividualOperation()
{
    BootTransaction transaction;
    transaction.stage({"firmware-next", "BootNext", "0002", "", "Boot once"});
    transaction.stage({"grub-setting", "GRUB_TIMEOUT", "5", "3", "Timeout"});
    PendingPage page;
    page.updateTransaction(transaction);
    connect(&page, &PendingPage::removalRequested, &page, [&](const QString &key) {
        transaction.remove(key);
        page.updateTransaction(transaction);
    });
    auto *items = page.findChild<QListWidget *>("pendingOperations");
    auto *cancel = page.findChild<QPushButton *>("cancelPendingOperation");
    QVERIFY(items);
    QVERIFY(cancel);
    QCOMPARE(items->count(), 2);
    cancel->click();
    QCOMPARE(transaction.count(), 1);
    QCOMPARE(transaction.operations().first().target, QString("GRUB_TIMEOUT"));
    QCOMPARE(items->count(), 1);
    cancel->click();
    QCOMPARE(transaction.count(), 0);
    QVERIFY(!cancel->isEnabled());
}

void BootTransactionTest::clearStagedBootNext()
{
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.bootOrder = {"0001", "0002"};
    snapshot.firmware = {{"0001", "First", "", "", true, true},
                         {"0002", "Second", "", "", true, false}};
    BootTransaction transaction;
    FirmwarePage page;
    page.updateSnapshot(snapshot);
    connect(&page, &FirmwarePage::operationRequested, &page, [&](const BootOperation &op) {
        transaction.stage(op);
        page.updateTransaction(transaction);
    });
    auto *clear = page.findChild<QPushButton *>("clearNextBoot");
    QVERIFY(clear);
    QVERIFY(!clear->isEnabled());
    transaction.stage({"firmware-next", "BootNext", "0002", "", "Boot once"});
    page.updateTransaction(transaction);
    QVERIFY(clear->isEnabled());
    clear->click();
    QCOMPARE(transaction.count(), 0);
    QVERIFY(!clear->isEnabled());
}

void BootTransactionTest::restoreOrderThroughPage()
{
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.bootOrder = {"0001", "0002"};
    snapshot.firmware = {{"0001", "First", "", "", true, true},
                         {"0002", "Second", "", "", true, false}};
    BootTransaction transaction;
    FirmwarePage page;
    page.updateSnapshot(snapshot);
    connect(&page, &FirmwarePage::operationRequested, &page, [&](const BootOperation &op) {
        transaction.stage(op);
        page.updateTransaction(transaction);
    });
    auto *table = page.findChild<QTableWidget *>();
    auto *up = page.findChild<QPushButton *>("moveBootEntryUp");
    auto *down = page.findChild<QPushButton *>("moveBootEntryDown");
    QVERIFY(table);
    QVERIFY(up);
    QVERIFY(down);
    table->selectRow(1);
    up->click();
    QCOMPARE(transaction.count(), 1);
    QCOMPARE(transaction.operations().first().value, QString("0002,0001"));
    table->selectRow(0);
    down->click();
    QCOMPARE(transaction.count(), 0);
    QCOMPARE(table->item(0, 0)->data(Qt::UserRole).toString(), QString("0001"));
}

void BootTransactionTest::rejectDeletedNextBoot()
{
    BootSnapshot snapshot;
    snapshot.bootOrder = {"0001", "0002"};
    snapshot.firmware = {{"0002", "Second", "", "", true, false}};
    BootTransaction transaction;
    transaction.stage({"firmware-remove", "0002", "", "", "Remove second"});
    transaction.stage({"firmware-next", "BootNext", "0002", "", "Boot once"});
    QVERIFY(!BootPlan::firmwareConflict(transaction.operations(), snapshot).isEmpty());
    PendingPage page;
    page.updateTransaction(transaction, snapshot);
    auto *conflict = page.findChild<QLabel *>("pendingConflict");
    QVERIFY(conflict);
    QVERIFY(!conflict->isHidden());
    transaction.remove("firmware-next:BootNext");
    page.updateTransaction(transaction, snapshot);
    QVERIFY(BootPlan::firmwareConflict(transaction.operations(), snapshot).isEmpty());
    QVERIFY(conflict->isHidden());
}

void BootTransactionTest::clearNextBeforeRemoval()
{
    BootSnapshot snapshot;
    snapshot.bootNext = "0002";
    BootTransaction transaction;
    transaction.stage({"firmware-remove", "0002", "", "", "Remove second"});
    QVERIFY(!BootPlan::firmwareConflict(transaction.operations(), snapshot).isEmpty());
    transaction.stage({"firmware-next", "BootNext", "", "0002", "Clear next"});
    QVERIFY(BootPlan::firmwareConflict(transaction.operations(), snapshot).isEmpty());
    transaction.stage({"firmware-next", "BootNext", "0002", "0002", "Restore next"});
    QVERIFY(!BootPlan::firmwareConflict(transaction.operations(), snapshot).isEmpty());
}

void BootTransactionTest::rejectEditOfRemovedEntry()
{
    BootTransaction transaction;
    transaction.stage({"firmware-rename", "0002", "New name", "", "Rename"});
    transaction.stage({"firmware-remove", "0002", "", "", "Remove"});
    QVERIFY(!BootPlan::firmwareConflict(transaction.operations(), {}).isEmpty());
    transaction.remove("firmware-rename:0002");
    transaction.stage({"firmware-toggle", "0002", "0", "1", "Disable"});
    QVERIFY(!BootPlan::firmwareConflict(transaction.operations(), {}).isEmpty());
}

void BootTransactionTest::validateRemainingOrder()
{
    BootSnapshot snapshot;
    snapshot.bootOrder = {"0001", "0002", "0003"};
    snapshot.firmware = {{"0001", "First", "", "", true, true},
                         {"0002", "Second", "", "", true, false},
                         {"0003", "Third", "", "", true, false}};
    BootTransaction transaction;
    transaction.stage({"firmware-remove", "0002", "", "", "Remove"});
    transaction.stage({"firmware-order", "BootOrder", "0003,0002,0001", "0001,0002,0003", "Order"});
    QVERIFY(!BootPlan::firmwareConflict(transaction.operations(), snapshot).isEmpty());
    transaction.stage({"firmware-order", "BootOrder", "0003,0001", "0001,0002,0003", "Order"});
    QVERIFY(BootPlan::firmwareConflict(transaction.operations(), snapshot).isEmpty());
    transaction.stage({"firmware-order", "BootOrder", "0003,0001,0001", "0001,0002,0003", "Order"});
    QVERIFY(!BootPlan::firmwareConflict(transaction.operations(), snapshot).isEmpty());
}

void BootTransactionTest::restoreBackupAlone()
{
    const BootOperation restore{"restore-backup", "id", {}, "digest", "Restore"};
    QVERIFY(BootPlan::firmwareConflict({restore}, {}).isEmpty());
    QVERIFY(!BootPlan::firmwareConflict({restore, {"grub-setting", "GRUB_TIMEOUT", "5", "3", "Timeout"}}, {}).isEmpty());
}

QTEST_MAIN(BootTransactionTest)
#include "BootTransactionTest.moc"
