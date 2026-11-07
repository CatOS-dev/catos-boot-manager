#include "model/BootTransaction.h"
#include "model/BootPlan.h"
#include "model/BootSnapshotCodec.h"
#include "services/BootProbe.h"
#include "services/BootSettings.h"
#include "services/BootRepair.h"
#include "services/BootBackup.h"
#include "services/BootBls.h"
#include "services/BootPackages.h"
#include "services/BootMenus.h"
#include "services/BootDeploy.h"
#include "services/BootPreview.h"
#include "services/BootSecurity.h"

#include <QObject>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QTranslator>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <poll.h>
#include <cerrno>
#include <thread>

namespace {
bool recordResult(BootBackup &backup, const QString &action, bool success)
{
    QString error;
    if (!backup.record(action, success, &error)) {
        QTextStream(stderr) << QObject::tr("Cannot record execution result: %1").arg(error) << Qt::endl;
        return false;
    }
    return success;
}

bool run(BootBackup &backup, const QString &program, const QStringList &arguments)
{
    QTextStream(stdout) << program << QLatin1Char(' ') << arguments.join(QLatin1Char(' ')) << Qt::endl;
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.start(program, arguments);
    const bool success = process.waitForStarted(10000) && process.waitForFinished(-1)
        && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    return recordResult(backup, program + QLatin1Char(' ') + arguments.join(QLatin1Char(' ')), success);
}

bool available(const QString &name)
{
    return !QStandardPaths::findExecutable(name).isEmpty();
}

bool firmwareAddArguments(const QString &path, const QString &label,
                          QStringList *arguments, QString *error)
{
    const QStringList mounts = BootProbe::espMounts();
    if (!BootProbe::isOnEsp(path, mounts)
        || !path.endsWith(QLatin1String(".efi"), Qt::CaseInsensitive)
        || label.trimmed().isEmpty() || label.size() > 80
        || label.contains(QLatin1Char('\n'))) {
        *error = QObject::tr("EFI file or name is not valid");
        return false;
    }
    QString mount;
    const QString absolute = QFileInfo(path).canonicalFilePath();
    for (const auto &candidate : mounts) {
        const QString base = QFileInfo(candidate).canonicalFilePath();
        if (absolute.startsWith(base + QStringLiteral("/EFI/"), Qt::CaseInsensitive)) {
            mount = base;
            break;
        }
    }
    if (mount.isEmpty()) { *error = QObject::tr("EFI binary is not inside the EFI directory"); return false; }
    const QString source = BootProbe::runReadOnly(QStringLiteral("findmnt"),
        {QStringLiteral("-rn"), QStringLiteral("-T"), mount,
         QStringLiteral("-o"), QStringLiteral("SOURCE")}).trimmed();
    const QString diskInfo = BootProbe::runReadOnly(QStringLiteral("lsblk"),
        {QStringLiteral("-dn"), QStringLiteral("-o"), QStringLiteral("PKNAME,PARTN"), source}).trimmed();
    const auto parts = diskInfo.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (parts.size() != 2 || !QRegularExpression(QStringLiteral("^[0-9]+$")).match(parts[1]).hasMatch()) {
        *error = QObject::tr("Cannot locate physical EFI partition");
        return false;
    }
    const QString disk = QStringLiteral("/dev/") + parts[0];
    if (!QFileInfo::exists(disk)) { *error = QObject::tr("EFI disk is unavailable"); return false; }
    QString efiRelative = absolute.mid(mount.size());
    efiRelative.replace(QLatin1Char('/'), QLatin1Char('\\'));
    *arguments = {QStringLiteral("-d"), disk,
                  QStringLiteral("-p"), parts[1], QStringLiteral("-L"), label,
                  QStringLiteral("-l"), efiRelative};
    return true;
}

bool preflight(const QList<BootOperation> &operations, const BootSnapshot &snapshot, QString *error)
{
    *error = BootPlan::firmwareConflict(operations, snapshot);
    if (!error->isEmpty()) return false;
    QSet<QString> used;
    bool rebuildGrub = false;
    bool rebuildLimine = false;
    QMap<QString, QMap<QString, QString>> imageChanges;
    QMap<QString, QSet<LoaderKind>> imageRebuilds;
    for (const auto &op : operations) {
        if (used.contains(op.key())) { *error = QObject::tr("Duplicate operation"); return false; }
        used.insert(op.key());
        if (!op.expectedMenu.isEmpty()) {
            const int marker = op.target.lastIndexOf(QLatin1Char('|'));
            bool matches = marker > 0 && op.kind == QLatin1String("systemd-setting") && op.target.endsWith(QLatin1String("|default"))
                && BootBls::defaultTargetMatches(snapshot, op.target.left(marker), op.value, op.expectedMenu);
            // A new BLS entry may be selected in the same transaction that creates it.
            for (const auto &entry : operations) {
                if (entry.kind != QLatin1String("menu-write") || entry.target != op.target.left(marker) + "/loader/entries/" + op.value) continue;
                const auto request = QJsonDocument::fromJson(entry.value.toUtf8()).object();
                for (const auto &loader : snapshot.installations)
                    if (loader.kind == LoaderKind::SystemdBoot && loader.esp == op.target.left(marker)
                        && BootBls::fingerprint(loader.menuContent) == op.expectedMenu && request.value("format") == QLatin1String("bls"))
                        matches = !request.value("remove").toBool();
            }
            if (!matches) { *error = QObject::tr("The selected systemd-boot menu changed or its target entry disappeared"); return false; }
        }
        if (op.kind == QLatin1String("install-packages")) {
            const auto arguments = BootPackages::installArguments({op}, snapshot, error);
            if (!error->isEmpty() || arguments.isEmpty() || !available(QStringLiteral("pacman"))) return false;
        } else if (op.kind == QLatin1String("menu-write")) {
            if (!BootMenus::validate(op, snapshot, error)) return false;
        } else if (op.kind.endsWith(QLatin1String("-setting"))) {
            const BootInstallation *bound = nullptr;
            const QString key = op.kind == QLatin1String("grub-setting") ? op.target : op.target.mid(op.target.lastIndexOf('|') + 1);
            for (const auto &installation : snapshot.installations)
                if (BootSettings::operationKind(installation.kind) == op.kind
                    && BootSettings::operationTarget(installation, key) == op.target && installation.settingsEditable) bound = &installation;
            if (!bound || !BootSettings::allowed(bound->kind, key, op.value)
                || bound->settingValues.value(key) != op.expected) {
                *error = QObject::tr("Boot settings changed since they were loaded"); return false;
            }
            rebuildGrub |= bound->kind == LoaderKind::Grub;
            if (BootDeploy::directImage(bound->kind)) {
                imageChanges[bound->config].insert(key, op.value);
                imageRebuilds[bound->config].insert(bound->kind);
            }
        } else if (op.kind == QLatin1String("firmware-order")) {
            if (!snapshot.uefi || snapshot.bootOrder.join(QLatin1Char(',')) != op.expected) {
                *error = QObject::tr("Firmware boot order changed"); return false;
            }
            const QStringList requested = op.value.split(QLatin1Char(','), Qt::SkipEmptyParts);
            if (requested.isEmpty()) {
                *error = QObject::tr("Boot order cannot be empty"); return false;
            }
        } else if (op.kind == QLatin1String("firmware-next")) {
            if (!snapshot.uefi || snapshot.bootNext != op.expected
                || (!op.value.isEmpty() && !snapshot.entry(op.value))) {
                *error = QObject::tr("Next boot entry is not available"); return false;
            }
        } else if (op.kind == QLatin1String("firmware-toggle")) {
            const auto *entry = snapshot.entry(op.target);
            if (!entry || op.expected != (entry->enabled ? QLatin1String("1") : QLatin1String("0"))
                || (op.value != QLatin1String("1") && op.value != QLatin1String("0"))) {
                *error = QObject::tr("Firmware entry changed"); return false;
            }
        } else if (op.kind == QLatin1String("firmware-rename")) {
            const auto *entry = snapshot.entry(op.target);
            bool invalid = !entry || entry->identity() != op.expected
                || op.value.trimmed().isEmpty() || op.value.size() > 80;
            for (QChar ch : op.value) if (ch.unicode() < 32 || ch.unicode() == 127) invalid = true;
            if (invalid) {
                *error = QObject::tr("Firmware entry changed or label is invalid"); return false;
            }
        } else if (op.kind == QLatin1String("firmware-remove")) {
            const auto *entry = snapshot.entry(op.target);
            if (!entry || entry->identity() != op.expected || entry->current) {
                *error = QObject::tr("Firmware entry changed or is currently in use"); return false;
            }
        } else if (op.kind == QLatin1String("firmware-add")) {
            QStringList arguments;
            if (!snapshot.uefi || !firmwareAddArguments(op.target, op.value, &arguments, error)) return false;
        } else if (op.kind == QLatin1String("rebuild-grub")) rebuildGrub = true;
        else if (op.kind == QLatin1String("rebuild-limine")) {
            bool bound = false;
            for (const auto &loader : snapshot.installations) if (loader.kind == LoaderKind::Limine && loader.config == op.target && loader.settingsEditable) bound = true;
            if (!bound || BootMenus::limineGeneratorPath(error) != op.target) { if (error->isEmpty()) *error = QObject::tr("Select the Limine installation for this system"); return false; }
            for (const auto &pending : operations)
                if ((pending.kind == QLatin1String("menu-write") && pending.target == op.target)
                    || (pending.kind == QLatin1String("limine-setting") && pending.target.startsWith(op.target + '|'))) {
                    *error = QObject::tr("Apply menu changes before rebuilding."); return false;
                }
            rebuildLimine = true;
        } else if (op.kind == QLatin1String("rebuild-image")) {
            bool bound = false;
            for (const auto &loader : snapshot.installations)
                if (BootDeploy::directImage(loader.kind) && BootDeploy::loaderId(loader.kind) == op.value && loader.config == op.target && loader.settingsEditable
                    && QJsonDocument::fromJson(loader.menuContent.toUtf8()).object().value("fingerprint").toString() == op.expected) bound = true;
            if (!bound) { *error = QObject::tr("The selected image settings changed"); return false; }
            imageRebuilds[op.target].insert(op.value == QLatin1String("uki") ? LoaderKind::Uki : LoaderKind::EfiStub);
        }
        else if (op.kind == QLatin1String("restore-backup")) {
            if (!BootBackup::validateRestore(BootBackup::defaultRoot(), op.target, op.expected, error)) return false;
        }
        else if (BootRepair::supports(op.kind)) {
            if (!BootRepair::validate(op, error)) return false;
        } else {
            *error = QObject::tr("Unknown operation type"); return false;
        }
    }
    if (rebuildGrub && (!available(QStringLiteral("grub-mkconfig"))
                        || !QDir(QStringLiteral("/boot/grub")).exists())) {
        *error = QObject::tr("GRUB generator or destination missing"); return false;
    }
    if (rebuildLimine && !available(QStringLiteral("limine-mkinitcpio"))) {
        *error = QObject::tr("limine-mkinitcpio is unavailable"); return false;
    }
    for (const auto &op : operations) {
        if (op.kind.startsWith(QLatin1String("firmware-")) && !available(QStringLiteral("efibootmgr"))) {
            *error = QObject::tr("efibootmgr is unavailable"); return false;
        }
    }
    for (auto it = imageRebuilds.begin(); it != imageRebuilds.end(); ++it)
        for (const auto kind : it.value())
            if (!BootDeploy::validateRebuild(kind, it.key(), imageChanges.value(it.key()), snapshot, error)) return false;
    // Do not leave a configured default pointing to a removed entry.
    for (const auto &loader : snapshot.installations) {
        if (loader.kind != LoaderKind::SystemdBoot) continue;
        QString selected = loader.settingValues.value("default");
        for (const auto &op : operations) if (op.kind == QLatin1String("systemd-setting") && op.target == loader.esp + "|default") selected = op.value;
        for (const auto &op : operations) if (op.kind == QLatin1String("menu-write") && op.target == loader.esp + "/loader/entries/" + selected
            && QJsonDocument::fromJson(op.value.toUtf8()).object().value("remove").toBool()) {
            *error = QObject::tr("Choose another default before removing this boot entry"); return false;
        }
    }
    return true;
}

bool apply(const QList<BootOperation> &operations, BootBackup &backup, const BootSnapshot &snapshot, QJsonObject *outcomes)
{
    for (const auto &op : operations) outcomes->insert(op.key(), "not-executed");
    QString packageError;
    const auto packages = BootPackages::installArguments(operations, snapshot, &packageError);
    if (!packageError.isEmpty()) return false;
    if (!packages.isEmpty()) {
        const bool synced = run(backup, QStringLiteral("pacman"), {QStringLiteral("-Sy"), QStringLiteral("--noconfirm"), QStringLiteral("--")});
        const bool success = synced && run(backup, QStringLiteral("pacman"), packages);
        for (const auto &op : operations)
            if (op.kind == "install-packages") outcomes->insert(op.key(), success ? "succeeded" : "failed");
        if (!success) return false;
    }
    for (const auto &op : operations) {
        if (op.kind != QLatin1String("restore-backup")) continue;
        QString error;
        const bool restored = BootBackup::restore(BootBackup::defaultRoot(), op.target, &error);
        outcomes->insert(op.key(), restored ? "succeeded" : "failed");
        if (!recordResult(backup, op.kind + ':' + op.target, restored)) { QTextStream(stderr) << error << Qt::endl; return false; }
    }
    bool grubEdited = false;
    QMap<QString, QSet<LoaderKind>> images;
    bool rebuildGrub = false;
    bool rebuildLimine = false;
    for (const auto &op : operations) {
        if (op.kind != QLatin1String("menu-write")) continue;
        QString error;
        const bool written = BootMenus::execute(op, &error);
        outcomes->insert(op.key(), written ? "succeeded" : "failed");
        if (!recordResult(backup, op.kind + ':' + op.target, written)) { QTextStream(stderr) << error << Qt::endl; return false; }
    }
    for (const auto &op : operations) {
        if (!op.kind.endsWith(QLatin1String("-setting"))) continue;
        const QString key = op.kind == QLatin1String("grub-setting") ? op.target : op.target.mid(op.target.lastIndexOf('|') + 1);
        const BootInstallation *bound = nullptr;
        for (const auto &installation : snapshot.installations)
            if (BootSettings::operationKind(installation.kind) == op.kind && BootSettings::operationTarget(installation, key) == op.target) bound = &installation;
        if (!bound) return false;
        const auto location = bound->kind == LoaderKind::SystemdBoot ? bound->esp : op.target.left(op.target.lastIndexOf('|'));
        QString error;
        const bool written = BootSettings::write(bound->kind, location, key, op.value, &error);
        outcomes->insert(op.key(), written ? "succeeded" : "failed");
        if (!recordResult(backup, op.kind + ':' + op.target, written)) { QTextStream(stderr) << error << Qt::endl; return false; }
        grubEdited |= bound->kind == LoaderKind::Grub;
        if (BootDeploy::directImage(bound->kind)) images[bound->config].insert(bound->kind);
    }
    for (const auto &op : operations) {
        if (op.kind == QLatin1String("rebuild-image")) images[op.target].insert(op.value == QLatin1String("uki") ? LoaderKind::Uki : LoaderKind::EfiStub);
        rebuildGrub |= op.kind == QLatin1String("rebuild-grub");
        rebuildLimine |= op.kind == QLatin1String("rebuild-limine");
    }
    if (grubEdited || rebuildGrub) {
        const bool success = run(backup, QStringLiteral("grub-mkconfig"), {QStringLiteral("-o"), QStringLiteral("/boot/grub/grub.cfg")});
        outcomes->insert("generate:grub", success ? "succeeded" : "failed");
        for (const auto &op : operations) if (op.kind == "rebuild-grub") outcomes->insert(op.key(), success ? "succeeded" : "failed");
        if (!success) return false;
    }
    if (rebuildLimine) {
        const bool success = run(backup, QStringLiteral("limine-mkinitcpio"), {});
        outcomes->insert("generate:limine", success ? "succeeded" : "failed");
        for (const auto &op : operations) if (op.kind == "rebuild-limine") outcomes->insert(op.key(), success ? "succeeded" : "failed");
        if (!success) return false;
    }
    for (auto it = images.begin(); it != images.end(); ++it) {
        for (const auto kind : it.value()) {
            QString error;
            const bool success = BootDeploy::rebuild(kind, it.key(), &error);
            outcomes->insert("generate:" + it.key(), success ? "succeeded" : "failed");
            for (const auto &op : operations)
                if (op.kind == "rebuild-image" && op.target == it.key()) outcomes->insert(op.key(), success ? "succeeded" : "failed");
            if (!recordResult(backup, "rebuild-image:" + it.key(), success)) { QTextStream(stderr) << error << Qt::endl; return false; }
        }
    }
    // Firmware operations are deliberately last; they can be partially applied
    // by UEFI firmware even when another later operation fails.
    for (const auto &op : operations) {
        bool success = false;
        if (op.kind == QLatin1String("firmware-order")) {
            success = run(backup, QStringLiteral("efibootmgr"), {QStringLiteral("-o"), op.value});
        } else if (op.kind == QLatin1String("firmware-next")) {
            success = run(backup, QStringLiteral("efibootmgr"), op.value.isEmpty()
                ? QStringList{QStringLiteral("-N")}
                : QStringList{QStringLiteral("-n"), op.value});
        } else if (op.kind == QLatin1String("firmware-toggle")) {
            success = run(backup, QStringLiteral("efibootmgr"), {QStringLiteral("-b"), op.target,
                op.value == QLatin1String("1") ? QStringLiteral("-a") : QStringLiteral("-A")});
        } else if (op.kind == QLatin1String("firmware-rename")) {
            success = run(backup, QStringLiteral("efibootmgr"), {QStringLiteral("-b"), op.target,
                QStringLiteral("-L"), op.value});
        } else if (op.kind == QLatin1String("firmware-remove")) {
            success = run(backup, QStringLiteral("efibootmgr"), {QStringLiteral("-b"), op.target,
                QStringLiteral("-B")});
        } else if (op.kind == QLatin1String("firmware-add")) {
            QStringList arguments;
            QString error;
            success = firmwareAddArguments(op.target, op.value, &arguments, &error);
            if (success) {
                QTextStream(stdout) << "efibootmgr --create-only " << arguments.join(QLatin1Char(' ')) << Qt::endl;
                success = BootDeploy::createFirmwareEntry(arguments, &error);
            }
            if (!error.isEmpty()) QTextStream(stderr) << error << Qt::endl;
            success = recordResult(backup, QStringLiteral("efibootmgr --create-only ") + arguments.join(QLatin1Char(' ')), success);
        } else if (BootRepair::supports(op.kind)) {
            QString error;
            success = BootRepair::execute(op, &error);
            if (!success) {
                QTextStream(stderr) << QObject::tr("Boot repair failed: %1").arg(error) << Qt::endl;
            }
        } else continue;
        outcomes->insert(op.key(), success ? "succeeded" : "failed");
        if (!success) return false;
    }
    return true;
}
}

namespace {
void endSession(int)
{
    kill(0, SIGTERM);
}

int executeTransaction(const QByteArray &payload, QJsonObject *outcomes, QString *reportDirectory)
{
    QTextStream errors(stderr);
    QString error;
    if (payload.size() > 16 * 1024 * 1024) {
        errors << QObject::tr("Transaction payload is too large") << Qt::endl;
        return 2;
    }
    const auto operations = BootTransaction::decode(payload, &error);
    if (!error.isEmpty()) { errors << error << Qt::endl; return 2; }
    for (const auto &op : operations) outcomes->insert(op.key(), "not-executed");
    const auto snapshot = BootProbe::inspect();
    if (!preflight(operations, snapshot, &error)) {
        errors << QObject::tr("Transaction rejected: %1").arg(error) << Qt::endl;
        return 3;
    }
    BootBackup backup;
    if (!backup.prepare(operations, snapshot, BootBackup::defaultRoot(), &error)) {
        errors << QObject::tr("Backup failed before applying changes: %1").arg(error) << Qt::endl;
        return 3;
    }
    QTextStream(stdout) << QObject::tr("Configuration backup and execution report: %1").arg(backup.directory()) << Qt::endl;
    *reportDirectory = backup.directory();
    const bool applied = apply(operations, backup, snapshot, outcomes);
    if (!backup.finish(applied, &error)) {
        errors << QObject::tr("Execution report could not be finalized: %1").arg(error) << Qt::endl;
        return 4;
    }
    if (!applied) {
        errors << QObject::tr("Operation failed.") << Qt::endl;
        return 4;
    }
    QTextStream(stdout) << QObject::tr("Transaction completed") << Qt::endl;
    return 0;
}

bool respond(QFile &channel, const QJsonObject &response)
{
    const auto data = QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n';
    return channel.write(data) == data.size() && channel.flush();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (geteuid() != 0 || args.size() != 2 || args[1] != QLatin1String("--session")) {
        QTextStream(stderr) << "Administrator authorization required" << Qt::endl;
        return 2;
    }
    const pid_t parent = getppid();
    if (parent == 1 || (getpgrp() != getpid() && setpgid(0, 0) != 0)) return 2;
    struct sigaction terminate{};
    terminate.sa_handler = endSession;
    sigemptyset(&terminate.sa_mask);
    if (sigaction(SIGUSR1, &terminate, nullptr) != 0
        || prctl(PR_SET_PDEATHSIG, SIGUSR1) != 0 || getppid() != parent) return 2;
    // Watch disconnection independently of a scan or command in progress.
    std::thread([] {
        pollfd input{STDIN_FILENO, 0, 0};
        int result;
        do { result = poll(&input, 1, -1); } while (result < 0 && errno == EINTR);
        endSession(0);
    }).detach();
    QTranslator translator;
    if (QLocale::system().language() == QLocale::Chinese) {
        const QString directory = QCoreApplication::applicationDirPath();
        const QStringList locations{directory, directory + QStringLiteral("/translations"),
            QDir::cleanPath(directory + QStringLiteral("/../../share/catos-boot-manager/translations"))};
        for (const auto &location : locations) {
            if (translator.load(QStringLiteral("catos-boot-manager_zh_CN"), location)) {
                app.installTranslator(&translator);
                break;
            }
        }
    }
    // Reserve stdout for responses. Commands and their child processes log to stderr.
    const int responseFd = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 3);
    if (responseFd < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0) return 2;
    QFile responses;
    QFile requests;
    if (!responses.open(responseFd, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle)
        || !requests.open(stdin, QIODevice::ReadOnly)) return 2;
    if (!respond(responses, {{QStringLiteral("type"), QStringLiteral("ready")},
                            {QStringLiteral("version"), 1}, {QStringLiteral("uid"), 0}})) return 2;
    for (;;) {
        const QByteArray line = requests.readLine(16 * 1024 * 1024 + 1026);
        if (line.isEmpty()) return requests.error() == QFileDevice::NoError ? 0 : 2;
        if (!line.endsWith('\n') || line.size() > 16 * 1024 * 1024 + 1024) return 2;
        const auto document = QJsonDocument::fromJson(line);
        if (!document.isObject()) return 2;
        const auto request = document.object();
        const QString action = request.value(QStringLiteral("action")).toString();
        if (action == QLatin1String("scan")) {
            bool connected = true;
            const auto snapshot = BootProbe::inspect([&](const QString &stage) {
                connected &= respond(responses, {{QStringLiteral("type"), QStringLiteral("progress")}, {QStringLiteral("stage"), stage}});
            });
            if (!connected) return 2;
            if (!respond(responses, {{QStringLiteral("type"), QStringLiteral("snapshot")},
                {QStringLiteral("data"), QString::fromLatin1(BootSnapshotCodec::encode(snapshot))}})) return 2;
        } else if (action == QLatin1String("secure-boot")) {
            // MOK requests run immediately. The password goes only to mokutil's
            // standard input; it is not written to the log or a backup.
            QString error;
            const bool success = BootSecurity::mokAvailable(&error)
                && BootSecurity::executeMokRequest(request.value(QStringLiteral("request")).toObject(), &error);
            if (!success) QTextStream(stderr) << error << Qt::endl;
            if (!respond(responses, {{QStringLiteral("type"), QStringLiteral("result")}, {QStringLiteral("success"), success},
                                    {QStringLiteral("code"), success ? 0 : 4}, {QStringLiteral("outcomes"), QJsonObject{}},
                                    {QStringLiteral("reportDirectory"), QString()}})) return 2;
        } else if (action == QLatin1String("preview")) {
            // Read-only: renders the files the transaction would change.
            QString error;
            QJsonArray files;
            const auto operations = BootTransaction::decode(request.value(QStringLiteral("transaction")).toString().toLatin1(), &error);
            if (error.isEmpty()) files = BootPreview::files(operations, &error);
            if (!respond(responses, {{QStringLiteral("type"), QStringLiteral("preview")},
                                    {QStringLiteral("files"), files}, {QStringLiteral("error"), error}})) return 2;
        } else if (action == QLatin1String("apply")) {
            QJsonObject outcomes;
            QString reportDirectory;
            const int code = executeTransaction(request.value(QStringLiteral("transaction")).toString().toLatin1(), &outcomes, &reportDirectory);
            if (!respond(responses, {{QStringLiteral("type"), QStringLiteral("result")},
                                    {QStringLiteral("success"), code == 0}, {QStringLiteral("code"), code},
                                    {QStringLiteral("outcomes"), outcomes}, {QStringLiteral("reportDirectory"), reportDirectory}})) return 2;
        } else return 2;
    }
}
