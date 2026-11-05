#include "pages/SettingsPage.h"

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
        {LoaderKind::SystemdBoot, "first", firstEsp},
        {LoaderKind::SystemdBoot, "second", secondEsp}
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

QTEST_MAIN(SettingsDraftTest)
#include "SettingsDraftTest.moc"
