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
    void restoreFirmwareValues();
    void distinctLimineOccurrences();
    void cancelIndividualOperation();
    void clearStagedBootNext();
    void restoreOrderThroughPage();
    void rejectDeletedNextBoot();
    void clearNextBeforeRemoval();
    void rejectEditOfRemovedEntry();
    void validateRemainingOrder();
};

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

void BootTransactionTest::distinctLimineOccurrences()
{
    BootTransaction transaction;
    transaction.stage({"limine-remove-entry", "Linux", "1", "hash", "Remove first"});
    transaction.stage({"limine-remove-entry", "Linux", "2", "hash", "Remove second"});
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
    auto *queue = page.findChild<QPushButton *>("queueBootOrder");
    QVERIFY(table);
    QVERIFY(up);
    QVERIFY(down);
    QVERIFY(queue);
    table->selectRow(1);
    up->click();
    queue->click();
    QCOMPARE(transaction.count(), 1);
    QCOMPARE(transaction.operations().first().value, QString("0002,0001"));
    table->selectRow(0);
    down->click();
    queue->click();
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

QTEST_MAIN(BootTransactionTest)
#include "BootTransactionTest.moc"
