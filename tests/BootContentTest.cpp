#include "services/BootContent.h"

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
};

void BootContentTest::loadLimineSettingsOnceAndRetainMenuSnapshot()
{
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::Limine, "current", {}, {}, {}, "/etc/default/limine", true}};
    QStringList queries;
    const QString menu = R"({"entries":[{"name":"Linux","path":"Linux","position":1}],"globals":{"timeout":"5","default_entry":"1"}})";
    BootContent::populate(snapshot, [&](const QString &program, const QStringList &arguments) {
        queries.append(program + ' ' + arguments.join(' '));
        if (arguments == QStringList({"--config-json"}))
            return QString(R"({"values":{"BOOT_ORDER":"*, *fallback","FIND_BOOTLOADERS":"yes","MKINITCPIO_FALLBACK":"yes"},"kernel_cmdlines":{"default":"root=UUID=catos ro"}})");
        if (arguments == QStringList({"--tree-json"})) return menu;
        return QString();
    });
    QCOMPARE(queries, QStringList({"limine-entry-tool --config-json", "limine-entry-tool --tree-json"}));
    const auto installation = snapshot.installations.first();
    QVERIFY(installation.settingsEditable);
    QCOMPARE(installation.settingValues.value("KERNEL_CMDLINE[default]"), QString("root=UUID=catos ro"));
    QCOMPARE(installation.settingValues.value("BOOT_ORDER"), QString("*, *fallback"));
    QCOMPARE(installation.menuContent, menu);
    QVERIFY(installation.menuMessage.isEmpty());
}

void BootContentTest::failedStructuredApiStaysReadOnly()
{
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::Limine, "current", {}, {}, {}, "/etc/default/limine", true}};
    QStringList queries;
    BootContent::populate(snapshot, [&](const QString &program, const QStringList &arguments) {
        queries.append(program + ' ' + arguments.join(' '));
        return QString("{}");
    });
    QCOMPARE(queries, QStringList({"limine-entry-tool --config-json", "limine-entry-tool --tree-json"}));
    QVERIFY(!snapshot.installations.first().settingsEditable);
    QVERIFY(!snapshot.installations.first().settingsMessage.isEmpty());
    QVERIFY(snapshot.installations.first().menuContent.isEmpty());
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

QTEST_GUILESS_MAIN(BootContentTest)
#include "BootContentTest.moc"
