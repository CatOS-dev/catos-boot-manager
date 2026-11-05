#include "widgets/BootUi.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QLabel>
#include <QTableWidget>
#include <QTreeWidget>
#include <QtTest>

class BootCopyTest : public QObject {
    Q_OBJECT
private slots:
    void copyDisplayedPath();
    void copyFirmwareRow();
    void copyBootMenuEntry();
    void copySelectedInstance();
};

void BootCopyTest::copyDisplayedPath()
{
    QWidget parent;
    auto *label = BootUi::muted("Configuration: /boot/loader/loader.conf", &parent);
    parent.show();
    QVERIFY(QTest::qWaitForWindowActive(&parent));
    label->setSelection(15, 24);
    label->setFocus();
    QApplication::clipboard()->clear();
    QTest::keySequence(label, QKeySequence::Copy);
    QCOMPARE(QApplication::clipboard()->text(), QString("/boot/loader/loader.conf"));
}

void BootCopyTest::copyFirmwareRow()
{
    QTableWidget table(1, 3);
    table.setSelectionBehavior(QAbstractItemView::SelectRows);
    table.setEditTriggers(QAbstractItemView::NoEditTriggers);
    table.setItem(0, 0, new QTableWidgetItem("Boot0001"));
    table.setItem(0, 1, new QTableWidgetItem("CatOS"));
    table.setItem(0, 2, new QTableWidgetItem("/EFI/CatOS/loader.efi"));
    BootUi::enableCopy(&table);
    table.show();
    QVERIFY(QTest::qWaitForWindowActive(&table));
    table.selectRow(0);
    table.setFocus();
    QApplication::clipboard()->clear();
    QTest::keySequence(&table, QKeySequence::Copy);
    QCOMPARE(QApplication::clipboard()->text(), QString("Boot0001\tCatOS\t/EFI/CatOS/loader.efi"));
}

void BootCopyTest::copyBootMenuEntry()
{
    QTreeWidget tree;
    tree.setColumnCount(2);
    auto *item = new QTreeWidgetItem(&tree, {"CatOS Linux", "BLS entry"});
    BootUi::enableCopy(&tree);
    tree.show();
    QVERIFY(QTest::qWaitForWindowActive(&tree));
    tree.setCurrentItem(item);
    tree.setFocus();
    QApplication::clipboard()->clear();
    QTest::keySequence(&tree, QKeySequence::Copy);
    QCOMPARE(QApplication::clipboard()->text(), QString("CatOS Linux\tBLS entry"));
}

void BootCopyTest::copySelectedInstance()
{
    QComboBox combo;
    combo.addItems({"/first/EFI/loader.efi", "/second/EFI/loader.efi"});
    BootUi::enableCopy(&combo);
    combo.show();
    QVERIFY(QTest::qWaitForWindowActive(&combo));
    combo.setCurrentIndex(1);
    combo.setFocus();
    QApplication::clipboard()->clear();
    QTest::keySequence(&combo, QKeySequence::Copy);
    QCOMPARE(QApplication::clipboard()->text(), QString("/second/EFI/loader.efi"));
}

QTEST_MAIN(BootCopyTest)
#include "BootCopyTest.moc"
