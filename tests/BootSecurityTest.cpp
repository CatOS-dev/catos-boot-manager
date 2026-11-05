#include "services/BootSecurity.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
}

class BootSecurityTest : public QObject {
    Q_OBJECT
private slots:
    void inspectFirmwareAndPublicCertificateListsWithoutMutation();
    void preserveUnknownOnMissingFirmwareAndFailedTools();
    void inspectRealUnsignedPeImage();
};

void BootSecurityTest::inspectFirmwareAndPublicCertificateListsWithoutMutation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString efivars = directory.path() + "/sys/firmware/efi/efivars/";
    const QString suffix = "-8be4df61-93ca-11d2-aa0d-00e098032b8c";
    const QByteArray enabled = QByteArray::fromHex("0700000001");
    const QByteArray disabled = QByteArray::fromHex("0700000000");
    QVERIFY(writeFile(efivars + "SecureBoot" + suffix, enabled));
    QVERIFY(writeFile(efivars + "SetupMode" + suffix, disabled));
    QVERIFY(writeFile(directory.path() + "/sys/module/module/parameters/sig_enforce", "Y\n"));
    const QString passwordPath = directory.path() + "/var/lib/catos-secureboot/enrollment-password";
    QVERIFY(writeFile(passwordPath, "must-remain-private-and-unchanged\n"));
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.installations = {{LoaderKind::Grub, "signed", "/esp", "uuid", "/esp/signed.efi", {}, false, true},
                             {LoaderKind::Uki, "unsigned", "/esp", "uuid", "/esp/unsigned.efi", {}, false, true}};
    QStringList commands;
    BootSecurity::inspect(snapshot, directory.path(), [&](const QString &program, const QStringList &args) {
        commands.append(program + " " + args.join(' '));
        if (program == "mokutil" && args == QStringList({"--list-enrolled"}))
            return BootSecurity::CommandResult{true, 0, "Enrolled certificate fingerprint", {}};
        if (program == "mokutil" && args == QStringList({"--list-new"}))
            return BootSecurity::CommandResult{true, 0, "Pending certificate fingerprint", {}};
        if (program == "sbverify" && args == QStringList({"--list", "/esp/signed.efi"}))
            return BootSecurity::CommandResult{true, 0, "signature 1\nimage signature issuers:\n /CN=Test\n", {}};
        if (program == "sbverify" && args == QStringList({"--list", "/esp/unsigned.efi"}))
            return BootSecurity::CommandResult{true, 0, "No signature table present\n", {}};
        return BootSecurity::CommandResult{false, -1, {}, "Unexpected command"};
    });
    QCOMPARE(commands, QStringList({"mokutil --list-enrolled", "mokutil --list-new",
                                   "sbverify --list /esp/signed.efi", "sbverify --list /esp/unsigned.efi"}));
    QCOMPARE(snapshot.secureBoot, SecureBootState::Enabled);
    QCOMPARE(snapshot.setupMode, SecureBootState::Disabled);
    QCOMPARE(snapshot.moduleSignatureEnforcement, SecureBootState::Enabled);
    QCOMPARE(snapshot.enrolledMok, QString("Enrolled certificate fingerprint"));
    QCOMPARE(snapshot.pendingMok, QString("Pending certificate fingerprint"));
    QCOMPARE(snapshot.installations[0].signature, SignatureState::Present);
    QCOMPARE(snapshot.installations[1].signature, SignatureState::Absent);
    QVERIFY(snapshot.securityMessages.isEmpty());
    QFile password(passwordPath);
    QVERIFY(password.open(QIODevice::ReadOnly));
    QCOMPARE(password.readAll(), QByteArray("must-remain-private-and-unchanged\n"));
    QVERIFY(!snapshot.enrolledMok.contains("must-remain-private"));
    QVERIFY(!snapshot.pendingMok.contains("must-remain-private"));
}

void BootSecurityTest::preserveUnknownOnMissingFirmwareAndFailedTools()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.installations = {{LoaderKind::Grub, "broken", "/esp", "uuid", "/esp/broken.efi", {}, false, true}};
    BootSecurity::inspect(snapshot, directory.path(), [](const QString &, const QStringList &) {
        return BootSecurity::CommandResult{false, -1, {}, "Tool is unavailable"};
    });
    QCOMPARE(snapshot.secureBoot, SecureBootState::Unknown);
    QCOMPARE(snapshot.setupMode, SecureBootState::Unknown);
    QCOMPARE(snapshot.moduleSignatureEnforcement, SecureBootState::Unknown);
    QCOMPARE(snapshot.installations.first().signature, SignatureState::Unknown);
    QCOMPARE(snapshot.securityMessages.size(), 2);
    const QString flag = directory.path() + "/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c";
    QVERIFY(writeFile(flag, QByteArray::fromHex("0700000002")));
    BootSecurity::inspect(snapshot, directory.path(), [](const QString &, const QStringList &) {
        return BootSecurity::CommandResult{true, 1, {}, "Permission denied"};
    });
    QCOMPARE(snapshot.secureBoot, SecureBootState::Unknown);
    QCOMPARE(snapshot.installations.first().signature, SignatureState::Unknown);
}

void BootSecurityTest::inspectRealUnsignedPeImage()
{
    const auto result = BootSecurity::readCommand("sbverify", {"--list", "/usr/lib/systemd/boot/efi/linuxx64.efi.stub"});
    QVERIFY2(result.completed, qPrintable(result.error));
    QCOMPARE(result.exitCode, 0);
    QCOMPARE((result.output + result.error).trimmed(), QString("No signature table present"));
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::Uki, "unsigned", {}, {}, "/usr/lib/systemd/boot/efi/linuxx64.efi.stub", {}, false, true}};
    BootSecurity::inspect(snapshot);
    QCOMPARE(snapshot.installations.first().signature, SignatureState::Absent);
    const auto missing = BootSecurity::readCommand("/nonexistent/boot-manager-test-command", {});
    QVERIFY(!missing.completed);
    QVERIFY(!missing.error.isEmpty());
}

QTEST_GUILESS_MAIN(BootSecurityTest)
#include "BootSecurityTest.moc"
