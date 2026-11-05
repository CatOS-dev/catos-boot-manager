#include "model/BootTransaction.h"
#include "model/BootPlan.h"
#include "services/BootProbe.h"
#include "services/BootSettings.h"
#include "services/BootRepair.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

#include <unistd.h>

namespace {
bool run(const QString &program, const QStringList &arguments)
{
    QTextStream(stdout) << program << QLatin1Char(' ') << arguments.join(QLatin1Char(' ')) << Qt::endl;
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.start(program, arguments);
    return process.waitForStarted(10000) && process.waitForFinished(-1)
        && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool available(const QString &name)
{
    return !QStandardPaths::findExecutable(name).isEmpty();
}

bool limineMenuMatches(const BootOperation &op, const QString &menuJson, QString *error)
{
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(menuJson.trimmed().toUtf8(),
                                    QCryptographicHash::Sha256).toHex());
    if (menuJson.isEmpty() || hash != op.expected) {
        *error = QStringLiteral("Limine menu changed after being loaded");
        return false;
    }
    return true;
}

bool validKernelName(const QString &name)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,90}$"));
    return valid.match(name).hasMatch();
}

bool validGlobalValue(const QString &key, const QString &value)
{
    if (value.isEmpty() || value.size() > 2048 || value.contains(QLatin1Char('\n'))
        || value.contains(QLatin1Char('\r'))) return false;
    for (QChar ch : value) if (ch.unicode() < 32) return false;
    if (key == QLatin1String("default_entry")) return true;
    if (key == QLatin1String("timeout")) {
        bool ok = false;
        const double timeout = value.toDouble(&ok);
        return value == QLatin1String("no") || (ok && timeout >= 0 && timeout <= 9999);
    }
    if (key == QLatin1String("quiet") || key == QLatin1String("graphics")
        || key == QLatin1String("remember_last_entry"))
        return value == QLatin1String("yes") || value == QLatin1String("no");
    return key == QLatin1String("wallpaper_style")
        && (value == QLatin1String("stretched") || value == QLatin1String("centered")
            || value == QLatin1String("tiled"));
}

bool firmwareAddArguments(const QString &path, const QString &label,
                          QStringList *arguments, QString *error)
{
    const QStringList mounts = BootProbe::espMounts();
    if (!BootProbe::isOnEsp(path, mounts)
        || !path.endsWith(QLatin1String(".efi"), Qt::CaseInsensitive)
        || label.trimmed().isEmpty() || label.size() > 80
        || label.contains(QLatin1Char('\n'))) {
        *error = QStringLiteral("EFI file or name is not valid");
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
    if (mount.isEmpty()) { *error = QStringLiteral("EFI binary is not inside the EFI directory"); return false; }
    const QString source = BootProbe::runReadOnly(QStringLiteral("findmnt"),
        {QStringLiteral("-rn"), QStringLiteral("-T"), mount,
         QStringLiteral("-o"), QStringLiteral("SOURCE")}).trimmed();
    const QString diskInfo = BootProbe::runReadOnly(QStringLiteral("lsblk"),
        {QStringLiteral("-dn"), QStringLiteral("-o"), QStringLiteral("PKNAME,PARTN"), source}).trimmed();
    const auto parts = diskInfo.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (parts.size() != 2 || !QRegularExpression(QStringLiteral("^[0-9]+$")).match(parts[1]).hasMatch()) {
        *error = QStringLiteral("Cannot locate physical EFI partition");
        return false;
    }
    const QString disk = QStringLiteral("/dev/") + parts[0];
    if (!QFileInfo::exists(disk)) { *error = QStringLiteral("EFI disk is unavailable"); return false; }
    QString efiRelative = absolute.mid(mount.size());
    efiRelative.replace(QLatin1Char('/'), QLatin1Char('\\'));
    *arguments = {QStringLiteral("-c"), QStringLiteral("-d"), disk,
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
    const QString menu = BootProbe::runReadOnly(QStringLiteral("limine-entry-tool"),
                                                {QStringLiteral("--tree-json")});
    const QJsonObject menuRoot = QJsonDocument::fromJson(menu.toUtf8()).object();
    for (const auto &op : operations) {
        if (used.contains(op.key())) { *error = QStringLiteral("Duplicate operation"); return false; }
        used.insert(op.key());
        if (op.kind == QLatin1String("grub-setting")
            || op.kind == QLatin1String("limine-setting")
            || op.kind == QLatin1String("systemd-setting")) {
            LoaderKind loader;
            if (op.kind == QLatin1String("grub-setting")) { loader = LoaderKind::Grub; rebuildGrub = true; }
            else if (op.kind == QLatin1String("limine-setting")) { loader = LoaderKind::Limine; rebuildLimine = true; }
            else if (op.kind == QLatin1String("systemd-setting")) loader = LoaderKind::SystemdBoot;
            else { *error = QStringLiteral("Unknown setting type"); return false; }
            // For systemd-boot the ESP is encoded in the operation's target as esp|key.
            QString key = op.target;
            QString systemdEsp;
            if (loader == LoaderKind::SystemdBoot) {
                const int marker = op.target.lastIndexOf(QLatin1Char('|'));
                if (marker <= 0) { *error = QStringLiteral("Missing systemd-boot ESP"); return false; }
                systemdEsp = op.target.left(marker);
                key = op.target.mid(marker + 1);
                if (!BootProbe::espMounts().contains(systemdEsp)) {
                    *error = QStringLiteral("ESP was unmounted or changed"); return false;
                }
            }
            if (!BootSettings::allowed(loader, key, op.value)
                || BootSettings::read(loader, systemdEsp, key) != op.expected) {
                *error = QStringLiteral("Boot settings changed since they were loaded"); return false;
            }
        } else if (op.kind == QLatin1String("firmware-order")) {
            if (!snapshot.uefi || snapshot.bootOrder.join(QLatin1Char(',')) != op.expected) {
                *error = QStringLiteral("Firmware boot order changed"); return false;
            }
            const QStringList requested = op.value.split(QLatin1Char(','), Qt::SkipEmptyParts);
            if (requested.isEmpty()) {
                *error = QStringLiteral("Boot order cannot be empty"); return false;
            }
        } else if (op.kind == QLatin1String("firmware-next")) {
            if (!snapshot.uefi || snapshot.bootNext != op.expected
                || (!op.value.isEmpty() && !snapshot.entry(op.value))) {
                *error = QStringLiteral("Next boot entry is not available"); return false;
            }
        } else if (op.kind == QLatin1String("firmware-toggle")) {
            const auto *entry = snapshot.entry(op.target);
            if (!entry || op.expected != (entry->enabled ? QLatin1String("1") : QLatin1String("0"))
                || (op.value != QLatin1String("1") && op.value != QLatin1String("0"))) {
                *error = QStringLiteral("Firmware entry changed"); return false;
            }
        } else if (op.kind == QLatin1String("firmware-rename")) {
            const auto *entry = snapshot.entry(op.target);
            bool invalid = !entry || entry->identity() != op.expected
                || op.value.trimmed().isEmpty() || op.value.size() > 80;
            for (QChar ch : op.value) if (ch.unicode() < 32 || ch.unicode() == 127) invalid = true;
            if (invalid) {
                *error = QStringLiteral("Firmware entry changed or label is invalid"); return false;
            }
        } else if (op.kind == QLatin1String("firmware-remove")) {
            const auto *entry = snapshot.entry(op.target);
            if (!entry || entry->identity() != op.expected || entry->current) {
                *error = QStringLiteral("Firmware entry changed or is currently in use"); return false;
            }
        } else if (op.kind == QLatin1String("firmware-add")) {
            QStringList arguments;
            if (!snapshot.uefi || !firmwareAddArguments(op.target, op.value, &arguments, error)) return false;
        } else if (op.kind == QLatin1String("limine-add-efi")) {
            if (!limineMenuMatches(op, menu, error)) return false;
            if (!BootProbe::isOnEsp(op.target, BootProbe::espMounts())
                || op.value.isEmpty() || op.value.size() > 100 || op.value.contains(QLatin1Char('\n'))) {
                *error = QStringLiteral("Invalid Limine EFI entry"); return false;
            }
        } else if (op.kind == QLatin1String("limine-remove-entry")) {
            if (!limineMenuMatches(op, menu, error)) return false;
            bool ok = false;
            const int position = op.value.toInt(&ok);
            if (!ok || position <= 0 || position > 10000
                || op.target.isEmpty() || op.target.size() > 256) {
                *error = QStringLiteral("Invalid Limine menu path"); return false;
            }
        } else if (op.kind == QLatin1String("limine-add-kernel")) {
            if (!limineMenuMatches(op, menu, error)) return false;
            const QJsonObject data = QJsonDocument::fromJson(op.value.toUtf8()).object();
            const QString kernel = data.value(QStringLiteral("kernel")).toString();
            const QString initramfs = data.value(QStringLiteral("initramfs")).toString();
            const QString suffix = data.value(QStringLiteral("suffix")).toString();
            if (!validKernelName(op.target)
                || (!suffix.isEmpty() && !validKernelName(QStringLiteral("a") + suffix))
                || !QFileInfo(kernel).isFile() || !QFileInfo(initramfs).isFile()) {
                *error = QStringLiteral("Invalid Limine kernel image or name"); return false;
            }
        } else if (op.kind == QLatin1String("limine-add-uki")) {
            if (!limineMenuMatches(op, menu, error)) return false;
            if (!validKernelName(op.target) || !QFileInfo(op.value).isFile()
                || !op.value.endsWith(QLatin1String(".efi"), Qt::CaseInsensitive)) {
                *error = QStringLiteral("Invalid unified kernel image"); return false;
            }
        } else if (op.kind == QLatin1String("limine-global-setting")) {
            const QString current = menuRoot.value(QStringLiteral("globals"))
                                         .toObject().value(op.target).toString();
            if (menu.isEmpty() || !validGlobalValue(op.target, op.value)
                || current != op.expected) {
                *error = QStringLiteral("Limine global configuration changed"); return false;
            }
        } else if (op.kind == QLatin1String("rebuild-grub")) rebuildGrub = true;
        else if (op.kind == QLatin1String("rebuild-limine")) rebuildLimine = true;
        else if (op.kind == QLatin1String("update-systemd")) {
            if (!snapshot.uefi || !BootProbe::espMounts().contains(op.target)) {
                *error = QStringLiteral("Selected systemd-boot ESP is unavailable"); return false;
            }
            bool found = false;
            for (const auto &loader : snapshot.installations)
                if (loader.kind == LoaderKind::SystemdBoot && loader.esp == op.target)
                    found = true;
            if (!found) { *error = QStringLiteral("systemd-boot not found on selected ESP"); return false; }
        } else if (BootRepair::supports(op.kind)) {
            if (!BootRepair::validate(op, error)) return false;
        } else {
            *error = QStringLiteral("Unknown operation type"); return false;
        }
    }
    if (rebuildGrub && (!available(QStringLiteral("grub-mkconfig"))
                        || !QDir(QStringLiteral("/boot/grub")).exists())) {
        *error = QStringLiteral("GRUB generator or destination missing"); return false;
    }
    if (rebuildLimine && !available(QStringLiteral("limine-mkinitcpio"))) {
        *error = QStringLiteral("limine-mkinitcpio is unavailable"); return false;
    }
    for (const auto &op : operations) {
        if (op.kind.startsWith(QLatin1String("firmware-")) && !available(QStringLiteral("efibootmgr"))) {
            *error = QStringLiteral("efibootmgr is unavailable"); return false;
        }
        if (op.kind == QLatin1String("update-systemd") && !available(QStringLiteral("bootctl"))) {
            *error = QStringLiteral("bootctl is unavailable"); return false;
        }
        if (op.kind.startsWith(QLatin1String("limine-")) && !available(QStringLiteral("limine-entry-tool"))) {
            *error = QStringLiteral("limine-entry-tool is unavailable"); return false;
        }
    }
    return true;
}

bool apply(const QList<BootOperation> &operations)
{
    bool grubEdited = false;
    bool limineEdited = false;
    bool rebuildGrub = false;
    bool rebuildLimine = false;
    // Apply configuration edits before invoking a generator, never per setting.
    for (const auto &op : operations) {
        if (op.kind != QLatin1String("grub-setting")
            && op.kind != QLatin1String("limine-setting")
            && op.kind != QLatin1String("systemd-setting")) continue;
        LoaderKind loader = LoaderKind::Grub;
        if (op.kind == QLatin1String("limine-setting")) loader = LoaderKind::Limine;
        else if (op.kind == QLatin1String("systemd-setting")) loader = LoaderKind::SystemdBoot;
        QString key = op.target;
        QString esp;
        if (loader == LoaderKind::SystemdBoot) {
            const int marker = key.lastIndexOf(QLatin1Char('|'));
            esp = key.left(marker);
            key = key.mid(marker + 1);
        }
        QString error;
        if (!BootSettings::write(loader, esp, key, op.value, &error)) {
            QTextStream(stderr) << "Configuration write failed: " << error << Qt::endl;
            return false;
        }
        grubEdited |= loader == LoaderKind::Grub;
        limineEdited |= loader == LoaderKind::Limine;
    }
    for (const auto &op : operations) {
        rebuildGrub |= op.kind == QLatin1String("rebuild-grub");
        rebuildLimine |= op.kind == QLatin1String("rebuild-limine");
    }
    if ((grubEdited || rebuildGrub)
        && !run(QStringLiteral("grub-mkconfig"), {QStringLiteral("-o"), QStringLiteral("/boot/grub/grub.cfg")})) return false;
    if ((limineEdited || rebuildLimine) && !run(QStringLiteral("limine-mkinitcpio"), {})) return false;
    // Firmware operations are deliberately last; they can be partially applied
    // by UEFI firmware even when another later operation fails.
    for (const auto &op : operations) {
        if (op.kind == QLatin1String("firmware-order")) {
            if (!run(QStringLiteral("efibootmgr"), {QStringLiteral("-o"), op.value})) return false;
        } else if (op.kind == QLatin1String("firmware-next")) {
            if (!run(QStringLiteral("efibootmgr"), op.value.isEmpty()
                ? QStringList{QStringLiteral("-N")}
                : QStringList{QStringLiteral("-n"), op.value})) return false;
        } else if (op.kind == QLatin1String("firmware-toggle")) {
            if (!run(QStringLiteral("efibootmgr"), {QStringLiteral("-b"), op.target,
                op.value == QLatin1String("1") ? QStringLiteral("-a") : QStringLiteral("-A")})) return false;
        } else if (op.kind == QLatin1String("firmware-rename")) {
            if (!run(QStringLiteral("efibootmgr"), {QStringLiteral("-b"), op.target,
                QStringLiteral("-L"), op.value})) return false;
        } else if (op.kind == QLatin1String("firmware-remove")) {
            if (!run(QStringLiteral("efibootmgr"), {QStringLiteral("-b"), op.target,
                QStringLiteral("-B")})) return false;
        } else if (op.kind == QLatin1String("firmware-add")) {
            QStringList arguments;
            QString error;
            if (!firmwareAddArguments(op.target, op.value, &arguments, &error)
                || !run(QStringLiteral("efibootmgr"), arguments)) return false;
        } else if (op.kind == QLatin1String("limine-add-efi")) {
            if (!run(QStringLiteral("limine-entry-tool"), {QStringLiteral("--add-efi"), op.value, op.target})) return false;
        } else if (op.kind == QLatin1String("limine-remove-entry")) {
            if (!run(QStringLiteral("limine-entry-tool"),
                     {QStringLiteral("--remove-entry"), op.target, op.value})) return false;
        } else if (op.kind == QLatin1String("limine-add-kernel")) {
            const QJsonObject data = QJsonDocument::fromJson(op.value.toUtf8()).object();
            QStringList args{QStringLiteral("--add-kernel"), op.target,
                             data.value(QStringLiteral("initramfs")).toString(),
                             data.value(QStringLiteral("kernel")).toString()};
            const QString suffix = data.value(QStringLiteral("suffix")).toString();
            if (!suffix.isEmpty()) args.append(suffix);
            if (!run(QStringLiteral("limine-entry-tool"), args)) return false;
        } else if (op.kind == QLatin1String("limine-add-uki")) {
            if (!run(QStringLiteral("limine-entry-tool"),
                     {QStringLiteral("--add-uki"), op.target, op.value})) return false;
        } else if (op.kind == QLatin1String("limine-global-setting")) {
            if (!run(QStringLiteral("limine-entry-tool"),
                     {QStringLiteral("--set-global"), op.target, op.value})) return false;
        } else if (op.kind == QLatin1String("update-systemd")) {
            if (!run(QStringLiteral("bootctl"),
                     {QStringLiteral("--esp-path=") + op.target, QStringLiteral("update")})) return false;
        } else if (BootRepair::supports(op.kind)) {
            QString error;
            if (!BootRepair::execute(op, &error)) {
                QTextStream(stderr) << "Boot repair failed: " << error << Qt::endl;
                return false;
            }
        }
    }
    return true;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    QTextStream errors(stderr);
    if (geteuid() != 0 || args.size() != 2 || args[1] != QLatin1String("--apply-stdin")) {
        errors << "Administrator authorization required" << Qt::endl;
        return 2;
    }
    QFile stream;
    if (!stream.open(stdin, QIODevice::ReadOnly)) {
        errors << "Cannot read transaction input" << Qt::endl;
        return 2;
    }
    QString error;
    const QByteArray payload = stream.readAll();
    if (payload.size() > 1048576) {
        errors << "Transaction payload is too large" << Qt::endl;
        return 2;
    }
    const auto operations = BootTransaction::decode(payload, &error);
    if (!error.isEmpty()) { errors << error << Qt::endl; return 2; }
    const auto snapshot = BootProbe::inspect();
    if (!preflight(operations, snapshot, &error)) {
        errors << "Transaction rejected: " << error << Qt::endl;
        return 3;
    }
    if (!apply(operations)) {
        errors << "Transaction stopped. Some earlier operations may already have been applied."
               << Qt::endl;
        return 4;
    }
    QTextStream(stdout) << "Transaction completed" << Qt::endl;
    return 0;
}
