#include "model/BootTransaction.h"
#include "pages/FirmwarePage.h"
#include "pages/PendingPage.h"

#include <QListWidget>
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

QTEST_MAIN(BootTransactionTest)
#include "BootTransactionTest.moc"
