#include "services/BootHealth.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class BootHealthTest : public QObject {
    Q_OBJECT
private slots:
    void reportWrongArchitectureAndUnsignedProgram();
    void detectMissingFilesAndInvalidFirmwareReferences();
};

void BootHealthTest::reportWrongArchitectureAndUnsignedProgram()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile image(directory.path() + "/program.efi");
    QVERIFY(image.open(QIODevice::WriteOnly));
    QCOMPARE(image.write("fixture PE program"), qint64(18));
    image.close();
    BootInstallation installation;
    installation.binary = image.fileName();
    installation.binaryPresent = true;
    installation.imageType = "pe";
    installation.architecture = "aa64";
    installation.signature = SignatureState::Absent;
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.efiArchitecture = "x64";
    snapshot.secureBoot = SecureBootState::Enabled;
    snapshot.installations = {installation};
    BootHealth::inspect(snapshot);
    QCOMPARE(snapshot.installations.first().issues.size(), 2);
    QVERIFY(snapshot.installations.first().issues.contains("The program architecture does not match the running firmware."));
    QVERIFY(snapshot.installations.first().issues.contains("Secure Boot is enabled but this EFI program has no signature table."));
    snapshot.installations[0].architecture = "x64";
    snapshot.installations[0].signature = SignatureState::Present;
    BootHealth::inspect(snapshot);
    QVERIFY(snapshot.installations.first().issues.isEmpty());
    // Signature presence remains a separate observation; health does not assert trust.
    QCOMPARE(snapshot.installations.first().signature, SignatureState::Present);
}

void BootHealthTest::detectMissingFilesAndInvalidFirmwareReferences()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    BootSnapshot snapshot;
    snapshot.bootNext = "0002";
    snapshot.bootOrder = {"0001", "0003"};
    snapshot.firmware = {{"0001", "Current", "", "", true, true},
                         {"0002", "Disabled", "", "", false, false}};
    BootInstallation installation;
    installation.binary = directory.path() + "/missing.efi";
    installation.config = directory.path() + "/missing.conf";
    installation.binaryPresent = true;
    snapshot.installations = {installation};
    BootHealth::inspect(snapshot);
    QCOMPARE(snapshot.installations.first().issues, QStringList({
        "The registered or discovered EFI program is missing.", "The expected configuration file is missing."}));
    QCOMPARE(snapshot.messages, QStringList({
        "BootNext refers to a missing or disabled firmware entry.", "BootOrder references missing firmware entry Boot0003."}));
    QCOMPARE(snapshot.installations.first().espBytesAvailable, qint64(-1));
}

QTEST_GUILESS_MAIN(BootHealthTest)
#include "BootHealthTest.moc"
