#include "BootSecurity.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>

namespace {
SecureBootState firmwareFlag(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return SecureBootState::Unknown;
    const QByteArray value = file.readAll();
    if (value.size() != 5) return SecureBootState::Unknown;
    if (value[4] == 0) return SecureBootState::Disabled;
    if (value[4] == 1) return SecureBootState::Enabled;
    return SecureBootState::Unknown;
}
}

BootSecurity::CommandResult BootSecurity::readCommand(const QString &program, const QStringList &arguments)
{
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(environment);
    process.start(program, arguments);
    CommandResult result;
    if (!process.waitForStarted(2000)) { result.error = process.errorString(); return result; }
    if (!process.waitForFinished(6500)) {
        process.kill();
        process.waitForFinished();
        result.error = QStringLiteral("Read-only command timed out");
        return result;
    }
    result.completed = process.exitStatus() == QProcess::NormalExit;
    result.exitCode = process.exitCode();
    result.output = QString::fromUtf8(process.readAllStandardOutput());
    result.error = QString::fromUtf8(process.readAllStandardError());
    return result;
}

void BootSecurity::inspect(BootSnapshot &snapshot, const QString &root, const Reader &reader)
{
    snapshot.securityMessages.clear();
    const QString efivars = QDir(root).filePath(QStringLiteral("sys/firmware/efi/efivars/"));
    const QString suffix = QStringLiteral("-8be4df61-93ca-11d2-aa0d-00e098032b8c");
    snapshot.secureBoot = snapshot.uefi ? firmwareFlag(efivars + QStringLiteral("SecureBoot") + suffix)
                                      : SecureBootState::Unsupported;
    snapshot.setupMode = snapshot.uefi ? firmwareFlag(efivars + QStringLiteral("SetupMode") + suffix)
                                    : SecureBootState::Unsupported;
    snapshot.moduleSignatureEnforcement = SecureBootState::Unknown;
    QFile enforced(QDir(root).filePath(QStringLiteral("sys/module/module/parameters/sig_enforce")));
    if (enforced.open(QIODevice::ReadOnly)) {
        const QByteArray value = enforced.readAll().trimmed().toLower();
        if (value == "1" || value == "y" || value == "yes") snapshot.moduleSignatureEnforcement = SecureBootState::Enabled;
        if (value == "0" || value == "n" || value == "no") snapshot.moduleSignatureEnforcement = SecureBootState::Disabled;
    }
    snapshot.enrolledMok.clear();
    snapshot.pendingMok.clear();
    if (snapshot.uefi) {
        const auto enrolled = reader(QStringLiteral("mokutil"), {QStringLiteral("--list-enrolled")});
        if (enrolled.completed && enrolled.exitCode == 0) snapshot.enrolledMok = enrolled.output;
        else snapshot.securityMessages.append(QStringLiteral("mokutil --list-enrolled: ") + enrolled.error);
        const auto pending = reader(QStringLiteral("mokutil"), {QStringLiteral("--list-new")});
        if (pending.completed && pending.exitCode == 0) snapshot.pendingMok = pending.output;
        else snapshot.securityMessages.append(QStringLiteral("mokutil --list-new: ") + pending.error);
    }
    for (auto &installation : snapshot.installations) {
        installation.signature = SignatureState::Unknown;
        installation.signatureDetails.clear();
        if (!installation.binaryPresent) continue;
        const auto result = reader(QStringLiteral("sbverify"), {QStringLiteral("--list"), installation.binary});
        installation.signatureDetails = result.output + result.error;
        if (!result.completed || result.exitCode != 0) continue;
        if (installation.signatureDetails.trimmed() == QLatin1String("No signature table present"))
            installation.signature = SignatureState::Absent;
        else if (QRegularExpression(QStringLiteral("(?:^|\n)signature [0-9]+"))
                     .match(installation.signatureDetails).hasMatch()) installation.signature = SignatureState::Present;
    }
}
