#include "services/BootContent.h"
#include <QJsonDocument>
#include <QJsonArray>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class BootContentTest : public QObject {
    Q_OBJECT
private slots:
    void loadLimineSettingsOnceAndRetainMenuSnapshot();
    void failedStructuredApiStaysReadOnly();
    void readSystemdConfigurationIntoSnapshot();
    void missingConfigurationIsNotEditable();
};

void BootContentTest::loadLimineSettingsOnceAndRetainMenuSnapshot()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.path() + "/limine.conf";
    QFile config(path);
    QVERIFY(config.open(QIODevice::WriteOnly));
    const QByteArray content = "timeout: 5\ndefault_entry: Linux\n/Linux\nprotocol: linux\npath: boot():/vmlinuz\n";
    QCOMPARE(config.write(content), content.size()); config.close();
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::Limine, "installed", directory.path(), {}, {}, path, true}};
    QStringList queries;
    BootContent::populate(snapshot, [&](const QString &program, const QStringList &) { queries.append(program); return QString(); });
    QVERIFY(queries.isEmpty());
    const auto loader = snapshot.installations.first();
    QVERIFY2(loader.settingsEditable, qPrintable(loader.settingsMessage));
    QCOMPARE(loader.settingValues.value("timeout"), QString("5"));
    const auto menu = QJsonDocument::fromJson(loader.menuContent.toUtf8()).object();
    QCOMPARE(menu.value("entries").toArray()[0].toObject().value("name").toString(), QString("Linux"));
    QVERIFY(!menu.value("fingerprint").toString().isEmpty());
}

void BootContentTest::failedStructuredApiStaysReadOnly()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::Limine, "installed", directory.path(), {}, {}, directory.path() + "/missing.conf", true}};
    BootContent::populate(snapshot);
    QVERIFY(!snapshot.installations.first().settingsEditable);
    QVERIFY(!snapshot.installations.first().settingsMessage.isEmpty());
    QVERIFY(!snapshot.installations.first().menuMessage.isEmpty());
}

void BootContentTest::readSystemdConfigurationIntoSnapshot()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir().mkpath(directory.path() + "/loader"));
    QFile config(directory.path() + "/loader/loader.conf");
    QVERIFY(config.open(QIODevice::WriteOnly));
    QCOMPARE(config.write("timeout 3\neditor no\n"), qint64(20));
    config.close();
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::SystemdBoot, "systemd", directory.path(), {}, {}, config.fileName(), true}};
    QStringList queries;
    BootContent::populate(snapshot, [&](const QString &program, const QStringList &arguments) {
        queries.append(program + ' ' + arguments.join(' '));
        return QString("[]");
    });
    QCOMPARE(queries, QStringList({"bootctl --esp-path=" + directory.path() + " --boot-path=" + directory.path() + " --no-pager --json=short list"}));
    QVERIFY(snapshot.installations.first().settingsEditable);
    QCOMPARE(snapshot.installations.first().settingValues.value("timeout"), QString("3"));
    QCOMPARE(snapshot.installations.first().menuContent, QString("[]"));
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(config.write("timeout 9\neditor no\n"), qint64(20));
    config.close();
    QCOMPARE(snapshot.installations.first().settingValues.value("timeout"), QString("3"));
}

void BootContentTest::missingConfigurationIsNotEditable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir().mkpath(directory.path() + "/loader"));
    const QString path = directory.path() + "/loader/loader.conf";
    QFile config(path);
    QVERIFY(config.open(QIODevice::WriteOnly));
    QCOMPARE(config.write("timeout 3\n"), qint64(10));
    config.close();
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::SystemdBoot, "systemd", directory.path(), {}, {}, path, true}};
    QVERIFY(config.remove());
    BootContent::populate(snapshot, [](const QString &, const QStringList &) { return QString("[]"); });
    QCOMPARE(snapshot.installations.size(), 1);
    const auto loader = snapshot.installations.first();
    QVERIFY(loader.configPresent);
    QVERIFY(!loader.settingsEditable);
    QVERIFY(loader.settingValues.isEmpty());
    QCOMPARE(loader.settingsMessage, QString("Unable to read configuration: ") + path);
}

QTEST_GUILESS_MAIN(BootContentTest)
#include "BootContentTest.moc"
