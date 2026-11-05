#include "services/BootScan.h"

#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <QtTest>

class BootScanTest : public QObject {
    Q_OBJECT
private slots:
    void scanKeepsEventLoopAvailable();
};

void BootScanTest::scanKeepsEventLoopAvailable()
{
    QSemaphore entered;
    QSemaphore release;
    bool workerThread = false;
    BootScan scan([&] {
        workerThread = QThread::currentThread() != QCoreApplication::instance()->thread();
        entered.release();
        release.acquire();
        BootSnapshot snapshot;
        snapshot.bootCurrent = "0002";
        return snapshot;
    }, nullptr);
    bool completed = false;
    QString current;
    connect(&scan, &BootScan::completed, this, [&](const BootSnapshot &snapshot) {
        completed = true;
        current = snapshot.bootCurrent;
    });
    const bool started = scan.start();
    const bool enteredWorker = entered.tryAcquire(1, 3000);
    const bool rejectedDuplicate = !scan.start();
    bool eventHandled = false;
    QTimer::singleShot(0, this, [&] { eventHandled = true; });
    QCoreApplication::processEvents();
    const bool stayedRunning = scan.isRunning();
    release.release();
    QVERIFY(started);
    QVERIFY(enteredWorker);
    QVERIFY(rejectedDuplicate);
    QVERIFY(eventHandled);
    QVERIFY(stayedRunning);
    QTRY_VERIFY(completed);
    QVERIFY(workerThread);
    QCOMPARE(current, QString("0002"));
    QVERIFY(!scan.isRunning());
    QVERIFY(scan.start());
    release.release();
    QTRY_VERIFY(!scan.isRunning());
}

QTEST_GUILESS_MAIN(BootScanTest)
#include "BootScanTest.moc"
