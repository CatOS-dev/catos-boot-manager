#include "services/BootSession.h"
#include "model/BootSnapshotCodec.h"

#include <QFile>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>
#include <unistd.h>

namespace {
bool writeResponse(QFile &output, const QJsonObject &value)
{
    const auto data = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
    return output.write(data) == data.size() && output.flush();
}

int fixtureSession()
{
    QFile output;
    QFile input;
    QFile lifecycle(qEnvironmentVariable("CATOS_SESSION_TEST_LIFECYCLE"));
    if (!output.open(stdout, QIODevice::WriteOnly) || !input.open(stdin, QIODevice::ReadOnly)
        || !lifecycle.open(QIODevice::WriteOnly)) return 2;
    const QString mode = qEnvironmentVariable("CATOS_SESSION_TEST_MODE");
    if (!writeResponse(output, {{"type", "ready"}, {"version", 1},
                               {"uid", mode == "unprivileged" ? 1000 : 0}})) return 2;
    for (;;) {
        const auto line = input.readLine();
        if (line.isEmpty()) {
            lifecycle.write("stopped\n");
            return lifecycle.flush() ? 0 : 2;
        }
        const auto request = QJsonDocument::fromJson(line).object();
        const QString action = request.value("action").toString();
        lifecycle.write(action.toUtf8() + '\n');
        lifecycle.flush();
        if (action == "scan") {
            BootSnapshot snapshot;
            snapshot.uefi = true;
            snapshot.bootCurrent = "0002";
            snapshot.bootOrder = {"0002"};
            snapshot.firmware = {{"0002", "GRUB", "\\EFI\\grub.efi", "partition", true, true}};
            snapshot.devices = {{"/dev/test", "disk", {}, "100 GiB", {}, "Test disk"}};
            snapshot.repairRoots = {"/"};
            snapshot.espMounts = {"/private/esp"};
            snapshot.mountSources = {{"/", "/dev/root"}, {"/private/esp", "/dev/esp"}};
            snapshot.installedPackages = {"grub"};
            snapshot.packageStateAvailable = true;
            snapshot.secureBoot = SecureBootState::Disabled;
            BootInstallation loader;
            loader.kind = LoaderKind::Grub;
            loader.id = "private-config";
            loader.config = "/private/grub";
            loader.configPresent = true;
            loader.settingsEditable = true;
            loader.settingValues = {{"GRUB_TIMEOUT", "7"}};
            loader.menuContent = "menuentry 'Linux' {}";
            loader.signature = SignatureState::Present;
            loader.espBytesAvailable = 404 * 1024 * 1024;
            snapshot.installations = {loader};
            const auto data = mode == "invalid-snapshot" ? QByteArray("broken") : BootSnapshotCodec::encode(snapshot);
            if (!writeResponse(output, {{"type", "snapshot"}, {"data", QString::fromLatin1(data)}})) return 2;
        } else if (action == "apply") {
            if (mode == "disconnect") return 1;
            QString error;
            const auto operations = BootTransaction::decode(request.value("transaction").toString().toLatin1(), &error);
            if (!error.isEmpty() || operations.size() != 1) return 2;
            QTextStream(stderr) << "operation log" << Qt::endl;
            if (!writeResponse(output, {{"type", "result"}, {"success", operations[0].value != "fail"}})) return 2;
        } else return 2;
    }
}
}

class BootSessionTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void oneAuthorizationForReadsAndOperations();
    void cancelledAuthorization();
    void cancelledStartupExitsApplication();
    void failedAuthorization();
    void rejectUnprivilegedSession();
    void failedReadDoesNotPublishEmptySnapshot();
    void disconnectFailsOperation();
    void rootExecutorSession();
    void closingSessionStopsActiveScan();

private:
    std::unique_ptr<QTemporaryDir> directory_;
    QProcessEnvironment environment_;
};

void BootSessionTest::init()
{
    environment_ = QProcessEnvironment::systemEnvironment();
    directory_ = std::make_unique<QTemporaryDir>();
    QVERIFY(directory_->isValid());
    QFile launcher(directory_->filePath("pkexec"));
    QVERIFY(launcher.open(QIODevice::WriteOnly));
    const QByteArray script = "#!/bin/sh\nprintf '%s\\n' \"$@\" >> \"$CATOS_SESSION_TEST_LAUNCH\"\n"
        "case \"$CATOS_SESSION_TEST_MODE\" in\n"
        "cancel) exit 126;;\ndenied) exit 127;;\n"
        "live) exec unshare -Ur \"$@\";;\n"
        "*) exec \"$CATOS_SESSION_TEST_BINARY\" --fixture-session;;\nesac\n";
    QCOMPARE(launcher.write(script), qint64(script.size()));
    launcher.close();
    QVERIFY(launcher.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    qputenv("PATH", directory_->path().toUtf8() + ':' + qgetenv("PATH"));
    qputenv("CATOS_SESSION_TEST_LAUNCH", directory_->filePath("launches").toUtf8());
    qputenv("CATOS_SESSION_TEST_LIFECYCLE", directory_->filePath("lifecycle").toUtf8());
    qputenv("CATOS_SESSION_TEST_BINARY", QCoreApplication::applicationFilePath().toUtf8());
    qunsetenv("CATOS_SESSION_TEST_MODE");
}

void BootSessionTest::cleanup()
{
    const QStringList keys{"PATH", "CATOS_SESSION_TEST_LAUNCH", "CATOS_SESSION_TEST_LIFECYCLE",
                           "CATOS_SESSION_TEST_BINARY", "CATOS_SESSION_TEST_MODE"};
    for (const auto &key : keys) {
        qunsetenv(key.toUtf8());
        if (environment_.contains(key)) qputenv(key.toUtf8(), environment_.value(key).toUtf8());
    }
    directory_.reset();
}

void BootSessionTest::oneAuthorizationForReadsAndOperations()
{
    BootSession session;
    QSignalSpy ready(&session, &BootSession::authorized);
    QSignalSpy failed(&session, &BootSession::failed);
    QSignalSpy results(&session, &BootSession::operationFinished);
    QSignalSpy logs(&session, &BootSession::output);
    QList<BootSnapshot> snapshots;
    connect(&session, &BootSession::snapshotReady, this, [&](const BootSnapshot &snapshot) { snapshots.append(snapshot); });
    QVERIFY(!session.scan());
    QVERIFY(!session.isAuthorized());
    session.start();
    session.start();
    QTRY_COMPARE(ready.count(), 1);
    QVERIFY(session.isAuthorized());
    QVERIFY(session.scan());
    QVERIFY(session.isBusy());
    QVERIFY(!session.scan());
    QTRY_COMPARE(snapshots.size(), 1);
    QVERIFY(!session.isBusy());
    const auto snapshot = snapshots.first();
    QCOMPARE(snapshot.bootCurrent, QString("0002"));
    QCOMPARE(snapshot.firmware.first().label, QString("GRUB"));
    QCOMPARE(snapshot.devices.first().path, QString("/dev/test"));
    QCOMPARE(snapshot.repairRoots, QStringList({"/"}));
    QCOMPARE(snapshot.espMounts, QStringList({"/private/esp"}));
    QCOMPARE(snapshot.mountSources.value("/private/esp"), QString("/dev/esp"));
    QCOMPARE(snapshot.secureBoot, SecureBootState::Disabled);
    QCOMPARE(snapshot.installedPackages, QSet<QString>({"grub"}));
    QVERIFY(snapshot.packageStateAvailable);
    const auto loader = snapshot.installations.first();
    QCOMPARE(loader.kind, LoaderKind::Grub);
    QCOMPARE(loader.config, QString("/private/grub"));
    QCOMPARE(loader.settingValues.value("GRUB_TIMEOUT"), QString("7"));
    QCOMPARE(loader.menuContent, QString("menuentry 'Linux' {}"));
    QCOMPARE(loader.signature, SignatureState::Present);
    QCOMPARE(loader.espBytesAvailable, qint64(404 * 1024 * 1024));
    QVERIFY(loader.settingsEditable);
    BootTransaction transaction;
    transaction.stage({"install-packages", "GRUB", "grub"});
    QVERIFY(session.apply(transaction));
    QVERIFY(!session.apply(transaction));
    QTRY_COMPARE(results.count(), 1);
    QCOMPARE(results.at(0).at(0).toBool(), true);
    transaction.stage({"install-packages", "GRUB", "fail"});
    QVERIFY(session.apply(transaction));
    QTRY_COMPARE(results.count(), 2);
    QCOMPARE(results.at(1).at(0).toBool(), false);
    QVERIFY(session.scan());
    QTRY_COMPARE(snapshots.size(), 2);
    session.stop();
    QVERIFY(!session.isAuthorized());
    QFile launches(directory_->filePath("launches"));
    QVERIFY(launches.open(QIODevice::ReadOnly));
    QCOMPARE(launches.readAll(), (QCoreApplication::applicationDirPath() + "/catos-boot-apply\n--session\n").toUtf8());
    QFile lifecycle(directory_->filePath("lifecycle"));
    QVERIFY(lifecycle.open(QIODevice::ReadOnly));
    QCOMPARE(lifecycle.readAll(), QByteArray("scan\napply\napply\nscan\nstopped\n"));
    QCOMPARE(failed.count(), 0);
    QVERIFY(logs.count() >= 2);
}

void BootSessionTest::cancelledAuthorization()
{
    qputenv("CATOS_SESSION_TEST_MODE", "cancel");
    BootSession session;
    QSignalSpy cancelled(&session, &BootSession::authorizationCancelled);
    QSignalSpy ready(&session, &BootSession::authorized);
    QSignalSpy failed(&session, &BootSession::failed);
    session.start();
    QTRY_COMPARE(cancelled.count(), 1);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(failed.count(), 0);
    QVERIFY(!session.scan());
}

void BootSessionTest::failedAuthorization()
{
    qputenv("CATOS_SESSION_TEST_MODE", "denied");
    BootSession session;
    QSignalSpy ready(&session, &BootSession::authorized);
    QSignalSpy failed(&session, &BootSession::failed);
    session.start();
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(ready.count(), 0);
    QVERIFY(!session.scan());
}

void BootSessionTest::cancelledStartupExitsApplication()
{
    qputenv("CATOS_SESSION_TEST_MODE", "cancel");
    QProcess manager;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_QPA_PLATFORM", "offscreen");
    manager.setProcessEnvironment(environment);
    manager.start(QCoreApplication::applicationDirPath() + "/catos-boot-manager", {});
    QVERIFY(manager.waitForStarted());
    QTRY_COMPARE(manager.state(), QProcess::NotRunning);
    QCOMPARE(manager.exitStatus(), QProcess::NormalExit);
    QCOMPARE(manager.exitCode(), 0);
    QVERIFY(!QFile::exists(directory_->filePath("lifecycle")));
}

void BootSessionTest::rejectUnprivilegedSession()
{
    qputenv("CATOS_SESSION_TEST_MODE", "unprivileged");
    BootSession session;
    QSignalSpy ready(&session, &BootSession::authorized);
    QSignalSpy failed(&session, &BootSession::failed);
    session.start();
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(ready.count(), 0);
    QVERIFY(!session.scan());
}

void BootSessionTest::failedReadDoesNotPublishEmptySnapshot()
{
    qputenv("CATOS_SESSION_TEST_MODE", "invalid-snapshot");
    BootSession session;
    QSignalSpy ready(&session, &BootSession::authorized);
    QSignalSpy failed(&session, &BootSession::failed);
    QSignalSpy snapshots(&session, &BootSession::snapshotReady);
    session.start();
    QTRY_COMPARE(ready.count(), 1);
    QVERIFY(session.scan());
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(snapshots.count(), 0);
    QVERIFY(!session.scan());
}

void BootSessionTest::disconnectFailsOperation()
{
    qputenv("CATOS_SESSION_TEST_MODE", "disconnect");
    BootSession session;
    QSignalSpy ready(&session, &BootSession::authorized);
    QSignalSpy failed(&session, &BootSession::failed);
    QSignalSpy results(&session, &BootSession::operationFinished);
    session.start();
    QTRY_COMPARE(ready.count(), 1);
    BootTransaction transaction;
    transaction.stage({"install-packages", "GRUB", "grub"});
    QVERIFY(session.apply(transaction));
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(results.count(), 1);
    QCOMPARE(results.first().first().toBool(), false);
    QVERIFY(!session.scan());
}

void BootSessionTest::rootExecutorSession()
{
    const uid_t originalUid = geteuid();
    qputenv("CATOS_SESSION_TEST_MODE", "live");
    BootSession session;
    QSignalSpy ready(&session, &BootSession::authorized);
    QSignalSpy failed(&session, &BootSession::failed);
    QSignalSpy results(&session, &BootSession::operationFinished);
    QList<BootSnapshot> snapshots;
    connect(&session, &BootSession::snapshotReady, this, [&](const BootSnapshot &snapshot) { snapshots.append(snapshot); });
    session.start();
    QTRY_COMPARE(ready.count(), 1);
    QVERIFY(session.scan());
    QTRY_COMPARE_WITH_TIMEOUT(snapshots.size(), 1, 60000);
    QVERIFY(snapshots.first().repairRoots.contains("/"));
    QVERIFY(!snapshots.first().mountSources.value("/").isEmpty());
    BootTransaction invalid;
    invalid.stage({"install-packages", "InvalidLoader", "injected"});
    QVERIFY(session.apply(invalid));
    QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, 60000);
    QCOMPARE(results.first().first().toBool(), false);
    QVERIFY(session.scan());
    QTRY_COMPARE_WITH_TIMEOUT(snapshots.size(), 2, 60000);
    session.stop();
    QCOMPARE(failed.count(), 0);
    QFile launches(directory_->filePath("launches"));
    QVERIFY(launches.open(QIODevice::ReadOnly));
    QCOMPARE(launches.readAll().count("--session"), 1);
    QCOMPARE(geteuid(), originalUid);
}

void BootSessionTest::closingSessionStopsActiveScan()
{
    qputenv("CATOS_SESSION_TEST_MODE", "live");
    QFile pacman(directory_->filePath("pacman"));
    QVERIFY(pacman.open(QIODevice::WriteOnly));
    const QByteArray script = "#!/bin/sh\n"
        "trap 'printf stopped > \"$CATOS_SESSION_TEST_LIFECYCLE\"; exit 0' TERM\n"
        "printf started > \"$CATOS_SESSION_TEST_LIFECYCLE.started\"\n"
        "/usr/bin/sleep 5\n";
    QCOMPARE(pacman.write(script), qint64(script.size()));
    pacman.close();
    QVERIFY(pacman.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    BootSession session;
    QSignalSpy ready(&session, &BootSession::authorized);
    QSignalSpy failed(&session, &BootSession::failed);
    QSignalSpy snapshots(&session, &BootSession::snapshotReady);
    session.start();
    QTRY_COMPARE(ready.count(), 1);
    QVERIFY(session.scan());
    QTRY_VERIFY(QFile::exists(directory_->filePath("lifecycle.started")));
    session.stop();
    QTRY_VERIFY(QFile::exists(directory_->filePath("lifecycle")));
    QFile lifecycle(directory_->filePath("lifecycle"));
    QVERIFY(lifecycle.open(QIODevice::ReadOnly));
    QCOMPARE(lifecycle.readAll(), QByteArray("stopped"));
    QCOMPARE(snapshots.count(), 0);
    QCOMPARE(failed.count(), 0);
    QVERIFY(!session.isAuthorized());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--fixture-session")) return fixtureSession();
    BootSessionTest tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "BootSessionTest.moc"
