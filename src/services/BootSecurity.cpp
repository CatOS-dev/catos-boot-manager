#include "BootSecurity.h"
#include "BootProbe.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextStream>

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
        BootProbe::stop(process);
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

QList<BootSecurity::Certificate> BootSecurity::certificates(const QString &output)
{
    static const QRegularExpression keyHeader(QStringLiteral("^\\[key [0-9]+\\]\\s*$"), QRegularExpression::MultilineOption);
    static const QRegularExpression fingerprint(QStringLiteral("^\\s*SHA1 Fingerprint:\\s*(\\S+)"), QRegularExpression::MultilineOption);
    static const QRegularExpression subject(QStringLiteral("^\\s*Subject:\\s*(.*)$"), QRegularExpression::MultilineOption);
    static const QRegularExpression issuer(QStringLiteral("^\\s*Issuer:\\s*(.*)$"), QRegularExpression::MultilineOption);
    static const QRegularExpression notAfter(QStringLiteral("^\\s*Not After\\s*:\\s*(.*)$"), QRegularExpression::MultilineOption);
    static const QRegularExpression commonName(QStringLiteral("CN\\s*=\\s*([^,/]+)"));
    QList<Certificate> result;
    QList<qsizetype> starts;
    for (auto it = keyHeader.globalMatch(output); it.hasNext();) starts.append(it.next().capturedStart());
    if (starts.isEmpty() && output.contains(QLatin1String("SHA1 Fingerprint"))) starts.append(0);
    for (int i = 0; i < starts.size(); ++i) {
        const QString block = output.mid(starts[i], i + 1 < starts.size() ? starts[i + 1] - starts[i] : -1);
        Certificate certificate;
        certificate.fingerprint = fingerprint.match(block).captured(1).trimmed();
        certificate.subject = subject.match(block).captured(1).trimmed();
        certificate.issuer = issuer.match(block).captured(1).trimmed();
        certificate.notAfter = notAfter.match(block).captured(1).trimmed();
        certificate.commonName = commonName.match(certificate.subject).captured(1).trimmed();
        if (certificate.commonName.isEmpty()) certificate.commonName = certificate.subject;
        if (!certificate.fingerprint.isEmpty() || !certificate.subject.isEmpty()) result.append(certificate);
    }
    return result;
}

bool BootSecurity::mokAvailable(QString *error)
{
    if (QDir(QStringLiteral("/sys/firmware/efi")).exists() && !QStandardPaths::findExecutable(QStringLiteral("mokutil")).isEmpty())
        return true;
    *error = QObject::tr("MOK management requires UEFI and the mokutil program.");
    return false;
}

bool BootSecurity::validateMokRequest(const QJsonObject &request, QString *error)
{
    const QString operation = request.value(QStringLiteral("operation")).toString();
    if (operation == QLatin1String("revoke-import")) return true;
    if (operation != QLatin1String("import")) { *error = QObject::tr("Unsupported Secure Boot operation."); return false; }
    const QByteArray certificate = QByteArray::fromBase64(request.value(QStringLiteral("certificate")).toString().toLatin1());
    // A DER certificate is an ASN.1 SEQUENCE; mokutil accepts only DER.
    if (certificate.size() < 64 || certificate.size() > 64 * 1024 || quint8(certificate.at(0)) != 0x30) {
        *error = QObject::tr("Choose a certificate in DER format.");
        return false;
    }
    const QString password = request.value(QStringLiteral("password")).toString();
    if (password.isEmpty() || password.size() > 256) { *error = QObject::tr("Enter a password of 1 to 256 characters."); return false; }
    for (const auto ch : password)
        if (ch.unicode() < 32 || ch.unicode() == 127) { *error = QObject::tr("The password cannot contain control characters."); return false; }
    return true;
}

bool BootSecurity::executeMokRequest(const QJsonObject &request, QString *error)
{
    if (!validateMokRequest(request, error)) return false;
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    QTemporaryFile file(QDir::temp().filePath(QStringLiteral("catos-boot-mok-XXXXXX.der")));
    if (request.value(QStringLiteral("operation")).toString() == QLatin1String("revoke-import")) {
        QTextStream(stdout) << "mokutil --revoke-import" << Qt::endl;
        process.start(QStringLiteral("mokutil"), {QStringLiteral("--revoke-import")});
        process.closeWriteChannel();
    } else {
        const QByteArray certificate = QByteArray::fromBase64(request.value(QStringLiteral("certificate")).toString().toLatin1());
        if (!file.open() || file.write(certificate) != certificate.size() || !file.flush()) {
            *error = QObject::tr("Unable to prepare the certificate."); return false;
        }
        QTextStream(stdout) << "mokutil --import <certificate>" << Qt::endl;
        process.start(QStringLiteral("mokutil"), {QStringLiteral("--import"), file.fileName()});
        // mokutil asks for the password twice on standard input.
        const QByteArray password = request.value(QStringLiteral("password")).toString().toUtf8() + '\n';
        process.write(password + password);
        process.closeWriteChannel();
    }
    if (!process.waitForStarted(10000) || !process.waitForFinished(60000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (process.state() != QProcess::NotRunning) BootProbe::stop(process);
        *error = QObject::tr("mokutil did not complete the request.");
        return false;
    }
    return true;
}
