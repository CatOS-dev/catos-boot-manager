#include "services/BootSecurity.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QStandardPaths>
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
    void parseMokCertificates();
    void validateMokRequests();
    void passPasswordOnlyToMokutil();
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
    if (QStandardPaths::findExecutable(QStringLiteral("sbverify")).isEmpty())
        QSKIP("sbverify is not installed");
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

void BootSecurityTest::parseMokCertificates()
{
    const QString output =
        "[key 1]\nSHA1 Fingerprint: 9b:2a:00:11\nCertificate:\n    Data:\n        Version: 3 (0x2)\n"
        "        Issuer: CN = CatOS Machine Owner Key, O = CatOS\n        Validity\n"
        "            Not Before: Nov  1 00:00:00 2026 GMT\n            Not After : Oct 29 00:00:00 2036 GMT\n"
        "        Subject: CN = CatOS Machine Owner Key, O = CatOS\n"
        "[key 2]\nSHA1 Fingerprint: aa:bb\nCertificate:\n        Issuer: O=Vendor, CN=Vendor CA\n"
        "            Not After : Jan  1 00:00:00 2030 GMT\n        Subject: O=Vendor, CN=Vendor Signing\n";
    const auto certificates = BootSecurity::certificates(output);
    QCOMPARE(certificates.size(), 2);
    QCOMPARE(certificates[0].commonName, QString("CatOS Machine Owner Key"));
    QCOMPARE(certificates[0].fingerprint, QString("9b:2a:00:11"));
    QCOMPARE(certificates[0].notAfter, QString("Oct 29 00:00:00 2036 GMT"));
    QCOMPARE(certificates[1].commonName, QString("Vendor Signing"));
    QCOMPARE(certificates[1].issuer, QString("O=Vendor, CN=Vendor CA"));
    QVERIFY(BootSecurity::certificates("MokListRT is empty\n").isEmpty());
}

void BootSecurityTest::validateMokRequests()
{
    QByteArray der(128, '\x01'); der[0] = '\x30';
    const QJsonObject valid{{"operation", "import"}, {"certificate", QString::fromLatin1(der.toBase64())}, {"password", "secret"}};
    QString error;
    QVERIFY2(BootSecurity::validateMokRequest(valid, &error), qPrintable(error));
    QVERIFY(BootSecurity::validateMokRequest({{"operation", "revoke-import"}}, &error));
    auto invalid = valid; invalid.insert("password", "");
    QVERIFY(!BootSecurity::validateMokRequest(invalid, &error));
    invalid = valid; invalid.insert("password", "line\nbreak");
    QVERIFY(!BootSecurity::validateMokRequest(invalid, &error));
    invalid = valid; invalid.insert("certificate", QString::fromLatin1(QByteArray("-----BEGIN CERTIFICATE-----").repeated(4).toBase64()));
    QVERIFY(!BootSecurity::validateMokRequest(invalid, &error));
    QVERIFY(!BootSecurity::validateMokRequest({{"operation", "delete"}}, &error));
}

void BootSecurityTest::passPasswordOnlyToMokutil()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QFile mokutil(directory.filePath("mokutil"));
    QVERIFY(mokutil.open(QIODevice::WriteOnly));
    mokutil.write("#!/bin/sh\nprintf '%s\\n' \"$*\" > \"$0.args\"\nwhile read -r line; do printf '%s\\n' \"$line\" >> \"$0.input\"; done\n");
    mokutil.close();
    QVERIFY(mokutil.setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const auto oldPath = qgetenv("PATH");
    qputenv("PATH", directory.path().toUtf8() + ':' + oldPath);
    QByteArray der(128, '\x01'); der[0] = '\x30';
    QString error;
    const bool done = BootSecurity::executeMokRequest({{"operation", "import"}, {"certificate", QString::fromLatin1(der.toBase64())},
                                                      {"password", "secret"}}, &error);
    qputenv("PATH", oldPath);
    QVERIFY2(done, qPrintable(error));
    QFile input(directory.filePath("mokutil.input")); QVERIFY(input.open(QIODevice::ReadOnly));
    QCOMPARE(input.readAll(), QByteArray("secret\nsecret\n"));
    QFile args(directory.filePath("mokutil.args")); QVERIFY(args.open(QIODevice::ReadOnly));
    const QByteArray arguments = args.readAll();
    QVERIFY(arguments.startsWith("--import "));
    QVERIFY(!arguments.contains("secret"));
}

QTEST_GUILESS_MAIN(BootSecurityTest)
#include "BootSecurityTest.moc"
