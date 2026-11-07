#include "services/BootScan.h"
#include "services/BootProbe.h"

#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtTest>

class BootScanTest : public QObject {
    Q_OBJECT
private slots:
    void scanKeepsEventLoopAvailable();
    void limineQueriesDoNotRunBootHooks();
    void readOnlyCommandsNeverEnterOtherSystems();
    void timedOutCommandsCanCleanUp();
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

void BootScanTest::limineQueriesDoNotRunBootHooks()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile command(directory.filePath("limine-entry-tool"));
    QVERIFY(command.open(QIODevice::WriteOnly));
    const QByteArray script = "#!/bin/sh\nhooks=yes\nmutex=yes\n"
        "for argument do\ncase \"$argument\" in\n--no-hooks) hooks=no;;\n--no-mutex) mutex=no;;\nesac\ndone\n"
        "if [ \"$hooks\" = yes ] || [ \"$mutex\" = yes ]; then\n"
        "printf modified > \"$CATOS_SCAN_TEST_SENTINEL\"\nfi\n"
        "printf '%s' '{\"entries\":[],\"globals\":{}}'\n";
    QCOMPARE(command.write(script), qint64(script.size()));
    command.close();
    QVERIFY(command.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    const auto previousPath = qgetenv("PATH");
    const auto previousSentinel = qgetenv("CATOS_SCAN_TEST_SENTINEL");
    const auto restore = qScopeGuard([&] {
        qputenv("PATH", previousPath);
        qputenv("CATOS_SCAN_TEST_SENTINEL", previousSentinel);
    });
    qputenv("PATH", directory.path().toUtf8());
    qputenv("CATOS_SCAN_TEST_SENTINEL", directory.filePath("modified").toUtf8());
    QCOMPARE(BootProbe::runReadOnly("limine-entry-tool", {"--tree-json"}), QString("{\"entries\":[],\"globals\":{}}"));
    QCOMPARE(BootProbe::runReadOnly("limine-entry-tool", {"--config-json"}), QString("{\"entries\":[],\"globals\":{}}"));
    QVERIFY(!QFile::exists(directory.filePath("modified")));
}

namespace {
bool executable(const QString &path, const QByteArray &script)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(script) == script.size()
        && file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}
}

void BootScanTest::readOnlyCommandsNeverEnterOtherSystems()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray script = "#!/bin/sh\ntouch \"$0.ran\"\necho output\n";
    for (const auto &name : {QStringLiteral("arch-chroot"), QStringLiteral("chroot"), QStringLiteral("systemd-nspawn")})
        QVERIFY(executable(directory.filePath(name), script));
    const auto previousPath = qgetenv("PATH");
    const auto restore = qScopeGuard([&] { qputenv("PATH", previousPath); });
    qputenv("PATH", directory.path().toUtf8());
    for (const auto &name : {QStringLiteral("arch-chroot"), QStringLiteral("chroot"), QStringLiteral("systemd-nspawn")}) {
        QVERIFY(BootProbe::runReadOnly(name, {"/run/media/drive", "true"}).isEmpty());
        QVERIFY(BootProbe::runReadOnly(directory.filePath(name), {"/run/media/drive", "true"}).isEmpty());
        QVERIFY(!QFile::exists(directory.filePath(name + ".ran")));
    }
}

void BootScanTest::timedOutCommandsCanCleanUp()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString command = directory.filePath("slow-tool");
    const QString cleaned = directory.filePath("cleaned");
    QVERIFY(executable(command, "#!/bin/sh\ntrap 'touch \"" + cleaned.toUtf8() + "\"; exit 1' TERM\n"
        "while :; do sleep 0.1; done\n"));
    QElapsedTimer timer;
    timer.start();
    QVERIFY(BootProbe::runReadOnly(command).isEmpty());
    QVERIFY(timer.elapsed() < 15000);
    QVERIFY(QFile::exists(cleaned));

    const QString stubborn = directory.filePath("stubborn-tool");
    QVERIFY(executable(stubborn, "#!/bin/sh\ntrap '' TERM\nwhile :; do sleep 0.1; done\n"));
    QProcess process;
    process.start(stubborn);
    QVERIFY(process.waitForStarted());
    QTest::qWait(200);
    BootProbe::stop(process);
    QCOMPARE(process.state(), QProcess::NotRunning);
}

QTEST_GUILESS_MAIN(BootScanTest)
#include "BootScanTest.moc"
