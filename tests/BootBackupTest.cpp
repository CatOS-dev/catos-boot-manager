#include "services/BootBackup.h"
#include "services/BootSettings.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class BootBackupTest : public QObject {
    Q_OBJECT
private slots:
    void keepOriginalAfterConfigurationWriteAndGeneratorFailure();
    void recordMissingOriginalAndRejectSymlink();
    void listAndRestoreOriginals();
};

void BootBackupTest::keepOriginalAfterConfigurationWriteAndGeneratorFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString esp = directory.path() + "/esp";
    QVERIFY(QDir().mkpath(esp + "/loader"));
    QFile config(esp + "/loader/loader.conf");
    const QByteArray original = "timeout 3\neditor yes\n";
    QVERIFY(config.open(QIODevice::WriteOnly));
    QCOMPARE(config.write(original), qint64(original.size()));
    config.close();
    const QList<BootOperation> operations{{"systemd-setting", esp + "|timeout", "8", "3", "Timeout"}};
    BootSnapshot snapshot;
    snapshot.bootCurrent = "0001";
    snapshot.bootNext = "0002";
    snapshot.bootOrder = {"0001", "0002"};
    snapshot.firmware = {{"0002", "CatOS", "\\EFI\\systemd\\systemd-bootx64.efi", "uuid", true, false}};
    BootBackup backup;
    QString error;
    QVERIFY2(backup.prepare(operations, snapshot, directory.path() + "/backups", &error), qPrintable(error));
    QVERIFY2(BootSettings::write(LoaderKind::SystemdBoot, esp, "timeout", "8", &error), qPrintable(error));
    QVERIFY2(backup.record("write config", true, &error), qPrintable(error));
    QProcess generator;
    generator.start(QStringLiteral("/usr/bin/false"), {});
    QVERIFY(generator.waitForFinished());
    QCOMPARE(generator.exitCode(), 1);
    const bool generated = generator.exitCode() == 0;
    QVERIFY2(backup.record("generator", generated, &error), qPrintable(error));
    QVERIFY2(backup.finish(generated, &error), qPrintable(error));
    QFile manifest(backup.directory() + "/manifest.json");
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    const QJsonObject report = QJsonDocument::fromJson(manifest.readAll()).object();
    QCOMPARE(report.value("phase").toString(), QString("incomplete"));
    QCOMPARE(report.value("bootNext").toString(), QString("0002"));
    QCOMPARE(report.value("bootOrder").toArray(), QJsonArray({"0001", "0002"}));
    QCOMPARE(report.value("firmware").toArray()[0].toObject().value("partitionUuid").toString(), QString("uuid"));
    const auto files = report.value("files").toArray();
    QCOMPARE(files.size(), 1);
    const auto file = files[0].toObject();
    QCOMPARE(file.value("path").toString(), config.fileName());
    QVERIFY(file.value("existed").toBool());
    QFile saved(backup.directory() + '/' + file.value("backup").toString());
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), original);
    QVERIFY(config.open(QIODevice::ReadOnly));
    QVERIFY(config.readAll().contains("timeout 8"));
    const auto results = report.value("results").toArray();
    QCOMPARE(results.size(), 2);
    QVERIFY(results[0].toObject().value("success").toBool());
    QVERIFY(!results[1].toObject().value("success").toBool());
    const auto otherAccess = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    QVERIFY(!(QFileInfo(backup.directory()).permissions() & otherAccess));
    QVERIFY(!(QFileInfo(manifest).permissions() & otherAccess));
    QVERIFY(!(QFileInfo(saved).permissions() & otherAccess));
}

void BootBackupTest::recordMissingOriginalAndRejectSymlink()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString esp = directory.path() + "/esp";
    QVERIFY(QDir().mkpath(esp + "/loader"));
    BootBackup backup;
    QString error;
    const QList<BootOperation> operations{{"systemd-setting", esp + "|timeout", "8", "", "Timeout"}};
    QVERIFY2(backup.prepare(operations, {}, directory.path() + "/backups", &error), qPrintable(error));
    QFile manifest(backup.directory() + "/manifest.json");
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    const auto files = QJsonDocument::fromJson(manifest.readAll()).object().value("files").toArray();
    QCOMPARE(files.size(), 1);
    QVERIFY(!files[0].toObject().value("existed").toBool());
    QVERIFY(!files[0].toObject().contains("backup"));
    QFile original(directory.path() + "/original");
    QVERIFY(original.open(QIODevice::WriteOnly));
    QCOMPARE(original.write("do not touch\n"), qint64(13));
    original.close();
    QVERIFY(QFile::link(original.fileName(), esp + "/loader/loader.conf"));
    BootBackup rejected;
    QVERIFY(!rejected.prepare(operations, {}, directory.path() + "/backups", &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArray("do not touch\n"));
}

void BootBackupTest::listAndRestoreOriginals()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path() + "/backups";
    const QString esp = directory.path() + "/esp";
    const QString config = esp + "/loader/loader.conf";
    const QString created = esp + "/loader/entries/new.conf";
    QVERIFY(QDir().mkpath(esp + "/loader/entries"));
    auto write = [](const QString &path, const QByteArray &data) {
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    auto read = [](const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); };
    QVERIFY(write(config, "timeout 3\n"));
    const QList<BootOperation> operations{{"systemd-setting", esp + "|timeout", "8", "3", "Timeout"},
        {"menu-write", created, "{}", "", "Add entry"}};
    BootBackup backup;
    QString error;
    QVERIFY2(backup.prepare(operations, {}, root, &error), qPrintable(error));
    QVERIFY(backup.finish(true, &error));
    QVERIFY(write(config, "timeout 8\n"));
    QVERIFY(write(created, "title New\n"));

    const auto list = BootBackup::list(root);
    QCOMPARE(list.size(), 1);
    const auto summary = list.first().toObject();
    const QString id = summary.value("id").toString();
    QCOMPARE(summary.value("phase").toString(), QString("completed"));
    QCOMPARE(summary.value("operations").toArray().size(), 2);
    QCOMPARE(summary.value("files").toArray().size(), 2);
    const QString digest = summary.value("digest").toString();
    QVERIFY(!BootBackup::validateRestore(root, id, "stale", &error));
    QVERIFY(!BootBackup::validateRestore(root, "../escape", digest, &error));
    QVERIFY2(BootBackup::validateRestore(root, id, digest, &error), qPrintable(error));

    // Restoring is itself backed up, so it can be undone.
    BootBackup undo;
    QVERIFY2(undo.prepare({{"restore-backup", id, {}, digest, "Restore"}}, {}, root, &error), qPrintable(error));
    QVERIFY2(BootBackup::restore(root, id, &error), qPrintable(error));
    QCOMPARE(read(config), QByteArray("timeout 3\n"));
    QVERIFY(!QFileInfo::exists(created));
    QCOMPARE(BootBackup::list(root).size(), 2);

    const auto originals = QDir(root + "/" + id).entryList({"*.original"}, QDir::Files);
    QCOMPARE(originals.size(), 1);
    QFile saved(QDir(root + "/" + id).filePath(originals.first()));
    QVERIFY(saved.open(QIODevice::Append));
    saved.write("tampered");
    saved.close();
    QVERIFY(!BootBackup::validateRestore(root, id, digest, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(BootBackupTest)
#include "BootBackupTest.moc"
