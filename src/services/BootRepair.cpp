#include "BootRepair.h"

#include "BootDevices.h"
#include "BootDeploy.h"
#include "BootProbe.h"

#include <QObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <memory>

namespace {

struct Command {
    QString executable;
    QStringList arguments;
};

QString grubTarget(const QString &architecture);

QString mountSource(const QString &path)
{
    return BootProbe::runReadOnly(QStringLiteral("findmnt"),
        {QStringLiteral("-rn"), QStringLiteral("-M"), path,
         QStringLiteral("-o"), QStringLiteral("SOURCE")}).trimmed();
}

QString targetPath(const QString &root, const QString &path)
{
    if (root == QLatin1String("/")) return path;
    if (!path.startsWith(root + QLatin1Char('/'))) return {};
    return path.mid(root.size());
}

bool run(const QString &executable, const QStringList &arguments)
{
    QTextStream(stdout) << executable << QLatin1Char(' ')
                        << arguments.join(QLatin1Char(' ')) << Qt::endl;
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.start(executable, arguments);
    if (!process.waitForStarted(10000)) return false;
    process.waitForFinished(-1);
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

QList<Command> commands(const QJsonObject &data)
{
    const QString loader = data.value(QStringLiteral("loader")).toString();
    const QString mode = data.value(QStringLiteral("mode")).toString();
    const QString root = data.value(QStringLiteral("root")).toString();
    const QString esp = targetPath(root, data.value(QStringLiteral("esp")).toString());
    const QString disk = data.value(QStringLiteral("disk")).toString();
    const bool registerEfi = data.value(QStringLiteral("register")).toBool();
    const bool fallback = data.value(QStringLiteral("fallback")).toBool();
    const QString architecture = data.value(QStringLiteral("architecture")).toString();
    const QString bootloaderId = data.value(QStringLiteral("bootloaderId")).toString();
    QList<Command> result;
    if (loader == QLatin1String("grub")) {
        if (mode == QLatin1String("bios")) {
            result.append({QStringLiteral("grub-install"),
                {QStringLiteral("--target=i386-pc"), QStringLiteral("--recheck"), disk}});
        } else {
            const QString target = grubTarget(data.value(QStringLiteral("architecture")).toString());
            if (target.isEmpty()) return {};
            QStringList arguments{QStringLiteral("--target=") + target,
                QStringLiteral("--efi-directory=") + esp,
                QStringLiteral("--bootloader-id=") + data.value(QStringLiteral("bootloaderId")).toString("CatOS")};
            if (!registerEfi) arguments.append(QStringLiteral("--no-nvram"));
            if (fallback) arguments.append(QStringLiteral("--removable"));
            result.append({QStringLiteral("grub-install"), arguments});
        }
        result.append({QStringLiteral("grub-mkconfig"),
                       {QStringLiteral("-o"), QStringLiteral("/boot/grub/grub.cfg")}});
    } else if (loader == QLatin1String("systemd-boot")) {
        QStringList arguments{QStringLiteral("--esp-path=") + esp};
        if (!registerEfi) arguments.append(QStringLiteral("--no-variables"));
        arguments.append(data.value("action").toString() == QLatin1String("update") ? QStringLiteral("update") : QStringLiteral("install"));
        result.append({QStringLiteral("bootctl"), arguments});
    } else if (loader == QLatin1String("limine")) {
        QStringList arguments;
        if (!registerEfi) arguments.append(QStringLiteral("--no-efi-register"));
        if (fallback) arguments.append(QStringLiteral("--fallback"));
        result.append({QStringLiteral("limine-install"), arguments});
        result.append({QStringLiteral("limine-mkinitcpio"), {}});
    }
    return result;
}

bool autoRoot(const QString &root)
{
    return root.startsWith(QLatin1String("auto:/dev/"));
}

bool validSubvolume(const QString &subvolume)
{
    static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9_./+-]{0,200}$"));
    if (!allowed.match(subvolume).hasMatch() || subvolume.startsWith(QLatin1Char('/'))) return false;
    for (const auto &part : subvolume.split(QLatin1Char('/')))
        if (part == QLatin1String("..")) return false;
    return true;
}

bool validEspMount(const QString &path)
{
    return path == QLatin1String("/boot/efi") || path == QLatin1String("/efi")
        || path == QLatin1String("/boot");
}

QString grubTarget(const QString &architecture)
{
    if (architecture == QLatin1String("aa64")) return QStringLiteral("arm64-efi");
    if (architecture == QLatin1String("ia32")) return QStringLiteral("i386-efi");
    return architecture.isEmpty() || architecture == QLatin1String("x64") ? QStringLiteral("x86_64-efi") : QString();
}

class RepairMount {
public:
    bool prepare(const QJsonObject &data, QString *error)
    {
        dir_ = std::make_unique<QTemporaryDir>(QStringLiteral("/run/catos-boot-repair-XXXXXX"));
        if (!dir_->isValid()) {
            *error = QObject::tr("Cannot create a private repair mount directory");
            return false;
        }
        // A failed unmount must never cause recursive deletion of a mounted OS.
        dir_->setAutoRemove(false);
        const QString rootDevice = data.value(QStringLiteral("rootDevice")).toString();
        QStringList mountArguments;
        const QString subvolume = data.value(QStringLiteral("rootSubvolume")).toString();
        if (!subvolume.isEmpty()) mountArguments << QStringLiteral("-o")
            << QStringLiteral("rw,subvol=") + subvolume;
        mountArguments << rootDevice << dir_->path();
        if (!run(QStringLiteral("mount"), mountArguments)) {
            *error = QObject::tr("Could not mount the selected Linux filesystem");
            return false;
        }
        mounted_.append(dir_->path());
        if (!QFileInfo::exists(dir_->path() + QStringLiteral("/etc/os-release"))
            || !QFileInfo::exists(dir_->path() + QStringLiteral("/usr/bin/sh"))) {
            *error = QObject::tr("Selected root/subvolume is not a Linux system");
            return false;
        }
        if (data.value(QStringLiteral("mode")).toString() == QLatin1String("uefi")
            && data.value(QStringLiteral("espRelative")).toString() != QLatin1String("/boot")) {
            QFile fstab(dir_->path() + QStringLiteral("/etc/fstab"));
            if (fstab.open(QIODevice::ReadOnly | QIODevice::Text)) {
                while (!fstab.atEnd()) {
                    const QString line = QString::fromUtf8(fstab.readLine()).trimmed();
                    if (line.isEmpty() || line.startsWith('#')) continue;
                    const auto fields = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                    if (fields.size() >= 2 && fields.at(1) == QLatin1String("/boot")) {
                        *error = QObject::tr("The target has a separate /boot filesystem; mount it before repair.");
                        return false;
                    }
                }
            }
        }
        if (data.value(QStringLiteral("mode")).toString() == QLatin1String("uefi")) {
            const QString relative = data.value(QStringLiteral("espRelative")).toString();
            const QString target = dir_->path() + relative;
            if (!validEspMount(relative)
                || QFileInfo(dir_->path() + QStringLiteral("/boot")).isSymLink()
                || QFileInfo(dir_->path() + QStringLiteral("/efi")).isSymLink()
                || QFileInfo(target).isSymLink()
                || !QDir().mkpath(target)
                || !QFileInfo(target).canonicalFilePath().startsWith(dir_->path() + QLatin1Char('/'))) {
                *error = QObject::tr("Invalid target EFI mount path");
                return false;
            }
            const QString espDevice = data.value(QStringLiteral("espDevice")).toString();
            const QString currentMount = BootProbe::runReadOnly(QStringLiteral("findmnt"),
                {QStringLiteral("-rn"), QStringLiteral("-S"), espDevice,
                 QStringLiteral("-o"), QStringLiteral("TARGET")})
                .split(QLatin1Char('\n'), Qt::SkipEmptyParts).value(0).trimmed();
            const QStringList arguments = !currentMount.isEmpty() && QFileInfo(currentMount).isDir()
                ? QStringList{QStringLiteral("--bind"), currentMount, target}
                : QStringList{espDevice, target};
            if (!run(QStringLiteral("mount"), arguments)) {
                *error = QObject::tr("Could not mount selected EFI system partition");
                return false;
            }
            mounted_.append(target);
        }
        return true;
    }

    QString path() const { return dir_ ? dir_->path() : QString(); }

    bool cleanup(QString *error)
    {
        bool success = true;
        QStringList remaining;
        for (auto it = mounted_.crbegin(); it != mounted_.crend(); ++it) {
            if (!run(QStringLiteral("umount"), {*it})) {
                QTextStream(stderr) << QObject::tr("Repair mount remains at %1").arg(*it) << Qt::endl;
                success = false;
                remaining.prepend(*it);
            }
        }
        mounted_ = remaining;
        if (success && dir_) {
            const QString root = dir_->path();
            if (QDir().rmdir(root)) {
                dir_.reset();
            } else {
                // Mountpoint directories on the target root may have been
                // created; leave this empty temporary directory untouched.
                QTextStream(stderr) << QObject::tr("Repair directory remains at %1").arg(root) << Qt::endl;
            }
        } else if (error && !success) {
            *error = QObject::tr("A temporary repair mount could not be unmounted");
        }
        return success;
    }

    ~RepairMount()
    {
        if (dir_) cleanup(nullptr);
    }

private:
    std::unique_ptr<QTemporaryDir> dir_;
    QStringList mounted_;
};

bool validateAuto(const BootOperation &operation, const QJsonObject &data, QString *error)
{
    const QString loader = data.value(QStringLiteral("loader")).toString();
    const QString mode = data.value(QStringLiteral("mode")).toString();
    const QString root = data.value(QStringLiteral("root")).toString();
    const QString rootDevice = data.value(QStringLiteral("rootDevice")).toString();
    const QString subvolume = data.value(QStringLiteral("rootSubvolume")).toString();
    const QString espDevice = data.value(QStringLiteral("espDevice")).toString();
    const bool registerEfi = data.value(QStringLiteral("register")).toBool();
    const bool fallback = data.value(QStringLiteral("fallback")).toBool();
    const QString architecture = data.value(QStringLiteral("architecture")).toString();
    const QString bootloaderId = data.value(QStringLiteral("bootloaderId")).toString();
    const auto devices = BootDevices::discover();
    const auto *system = BootDevices::byPath(devices, rootDevice);
    if (system && (system->type == QLatin1String("crypt") || system->type == QLatin1String("lvm"))) {
        *error = QObject::tr("The selected root is an unlocked LUKS or LVM device; automatic repair needs its mapped filesystem mounted.");
        return false;
    }
    if (root != QStringLiteral("auto:") + rootDevice || !system
        || system->type != QLatin1String("part")
        || !QStringList{QStringLiteral("btrfs"), QStringLiteral("ext4"),
                        QStringLiteral("xfs"), QStringLiteral("f2fs")}.contains(system->filesystem)
        || !validSubvolume(subvolume)
        || (system->filesystem != QLatin1String("btrfs") && !subvolume.isEmpty())) {
        *error = QObject::tr("Invalid Linux partition or Btrfs subvolume");
        return false;
    }
    if (!QStringList{QStringLiteral("grub"), QStringLiteral("limine"),
                     QStringLiteral("systemd-boot"), QStringLiteral("refind"), QStringLiteral("uki"), QStringLiteral("efistub")}.contains(loader)
        || !QStringList{QStringLiteral("uefi"), QStringLiteral("bios")}.contains(mode)) {
        *error = QObject::tr("Unsupported loader or firmware mode");
        return false;
    }
    if (mode == QLatin1String("bios")) {
        const QString disk = data.value(QStringLiteral("disk")).toString();
        const auto *device = BootDevices::byPath(devices, disk);
        if (loader != QLatin1String("grub") || registerEfi || fallback
            || !data.value(QStringLiteral("esp")).toString().isEmpty()
            || !espDevice.isEmpty() || !device || device->type != QLatin1String("disk")
            || disk.startsWith(QLatin1String("/dev/zram"))
            || disk.startsWith(QLatin1String("/dev/loop"))) {
            *error = QObject::tr("Choose GRUB and a whole disk for BIOS repair");
            return false;
        }
    } else {
        const auto *efi = BootDevices::byPath(devices, espDevice);
        static const QString espPartitionType = QStringLiteral("c12a7328-f81f-11d2-ba4b-00a0c93ec93b");
        if (!efi || efi->type != QLatin1String("part")
            || efi->filesystem.compare(QLatin1String("vfat"), Qt::CaseInsensitive) != 0
            || efi->partuuid.compare(espPartitionType, Qt::CaseInsensitive) != 0
            || espDevice == rootDevice
            || !validEspMount(data.value(QStringLiteral("espRelative")).toString())
            || !data.value(QStringLiteral("esp")).toString().isEmpty()
            || !data.value(QStringLiteral("disk")).toString().isEmpty()
            || (fallback && loader == QLatin1String("systemd-boot"))) {
            *error = QObject::tr("Invalid UEFI partition or target mount point");
            return false;
        }
        if (loader == QLatin1String("limine")
            && data.value(QStringLiteral("espRelative")).toString() != QLatin1String("/boot")) {
            *error = QObject::tr("Automatic Limine repair requires the tool-configured EFI mount at /boot");
            return false;
        }
        if (registerEfi && !QDir(QStringLiteral("/sys/firmware/efi")).exists()) {
            *error = QObject::tr("Firmware registration requires UEFI boot mode");
            return false;
        }
    }
    if (BootDevices::fingerprint(devices) != operation.expected) {
        *error = QObject::tr("Disk inventory changed before repair was applied");
        return false;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("arch-chroot")).isEmpty()
        || QStandardPaths::findExecutable(QStringLiteral("mount")).isEmpty()
        || QStandardPaths::findExecutable(QStringLiteral("umount")).isEmpty()) {
        *error = QObject::tr("Mount and arch-chroot tools are required");
        return false;
    }
    return true;
}
}

QJsonObject BootRepair::arguments(const BootOperation &operation)
{
    return QJsonDocument::fromJson(operation.value.toUtf8()).object();
}

bool BootRepair::supports(const QString &operation)
{
    return operation == QLatin1String("repair-install");
}

QStringList BootRepair::mountedTargets()
{
    QStringList results{QStringLiteral("/")};
    const QString output = BootProbe::runReadOnly(QStringLiteral("findmnt"),
        {QStringLiteral("-rn"), QStringLiteral("-o"), QStringLiteral("TARGET")});
    for (const QString &line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString path = QDir::cleanPath(line.trimmed());
        if (!path.startsWith(QLatin1String("/mnt/")) && path != QLatin1String("/mnt")
            && !path.startsWith(QLatin1String("/run/media/"))) continue;
        if (QFileInfo::exists(path + QStringLiteral("/etc/os-release"))
            && QFileInfo::exists(path + QStringLiteral("/usr/bin/sh"))
            && !results.contains(path)) results.append(path);
    }
    results.sort();
    return results;
}

bool BootRepair::validate(const BootOperation &operation, QString *error)
{
    if (!supports(operation.kind)) {
        *error = QObject::tr("Unsupported repair action");
        return false;
    }
    const QJsonObject data = arguments(operation);
    const QString root = data.value(QStringLiteral("root")).toString();
    const QString loader = data.value(QStringLiteral("loader")).toString();
    const QString mode = data.value(QStringLiteral("mode")).toString();
    const QString esp = data.value(QStringLiteral("esp")).toString();
    const QString disk = data.value(QStringLiteral("disk")).toString();
    const bool registerEfi = data.value(QStringLiteral("register")).toBool();
    const bool fallback = data.value(QStringLiteral("fallback")).toBool();
    const QString architecture = data.value(QStringLiteral("architecture")).toString();
    const QString bootloaderId = data.value(QStringLiteral("bootloaderId")).toString();
    if (autoRoot(root)) return validateAuto(operation, data, error);
    if (!QStringList{QStringLiteral("grub"), QStringLiteral("systemd-boot"), QStringLiteral("limine"), QStringLiteral("refind"), QStringLiteral("uki"), QStringLiteral("efistub")}.contains(loader)
        || !QStringList{QStringLiteral("bios"), QStringLiteral("uefi")}.contains(mode)) {
        *error = QObject::tr("Unsupported boot loader or firmware mode"); return false;
    }
    if (mode == QLatin1String("uefi") && loader == QLatin1String("grub") && grubTarget(architecture).isEmpty()) {
        *error = QObject::tr("The selected GRUB instance has an unsupported EFI architecture"); return false;
    }
    if (bootloaderId.contains('/') || bootloaderId.contains('\\') || bootloaderId.size() > 80) {
        *error = QObject::tr("Invalid boot loader instance name"); return false;
    }
    if (root.isEmpty() || !mountedTargets().contains(root)
        || !QFileInfo::exists(root + QStringLiteral("/usr/bin/sh"))) {
        *error = QObject::tr("Target operating system is not mounted or cannot be identified"); return false;
    }
    if (mountSource(root).isEmpty()
        || mountSource(root) != data.value(QStringLiteral("rootSource")).toString()) {
        *error = QObject::tr("The target root mount changed or is not available"); return false;
    }
    if (mode == QLatin1String("bios")) {
        if (loader != QLatin1String("grub") || registerEfi || fallback || !esp.isEmpty()) {
            *error = QObject::tr("Only GRUB supports BIOS disk installation here"); return false;
        }
        const auto devices = BootDevices::discover();
        const auto *device = BootDevices::byPath(devices, disk);
        if (!device || device->type != QLatin1String("disk")
            || disk.startsWith(QLatin1String("/dev/zram"))
            || disk.startsWith(QLatin1String("/dev/loop"))) {
            *error = QObject::tr("Select a whole physical disk for BIOS GRUB installation");
            return false;
        }
    } else {
        if (!disk.isEmpty() || (fallback && loader == QLatin1String("systemd-boot"))) {
            *error = QObject::tr("Unsupported UEFI installation options"); return false;
        }
        const bool directory = QStringList{"refind", "uki", "efistub"}.contains(loader);
        if (directory) {
            if (!BootDeploy::validateInputs(data, root, error)) return false;
        } else {
            const QStringList mounts = BootProbe::espMounts();
            if (!esp.isEmpty() && (!mounts.contains(esp) || targetPath(root, esp).isEmpty()
                || mountSource(esp).isEmpty() || mountSource(esp) != data.value(QStringLiteral("espSource")).toString())) {
                *error = QObject::tr("The selected destination is not mounted inside the target OS"); return false;
            }
            if (loader != QLatin1String("limine") && esp.isEmpty()) {
                *error = QObject::tr("Choose an installation directory"); return false;
            }
        }
        if (registerEfi && !QDir(QStringLiteral("/sys/firmware/efi")).exists()) {
            *error = QObject::tr("UEFI firmware registration is not possible in this environment");
            return false;
        }
    }
    const QString fingerprint = BootDevices::fingerprint(BootDevices::discover());
    if (operation.expected != fingerprint) {
        *error = QObject::tr("Disk or partition inventory changed after the repair was queued");
        return false;
    }
    if (QStringList{"refind", "uki", "efistub"}.contains(loader)) {
        if (!BootDeploy::validateInputs(data, root, error)) return false;
        if (registerEfi && QStandardPaths::findExecutable("efibootmgr").isEmpty()) { *error = QObject::tr("Install efibootmgr"); return false; }
    }
    for (const Command &command : commands(data)) {
        const QString executable = root == QLatin1String("/")
            ? QStandardPaths::findExecutable(command.executable)
            : root + QStringLiteral("/usr/bin/") + command.executable;
        if (executable.isEmpty() || !QFileInfo::exists(executable)) {
            *error = QObject::tr("The target system is missing %1").arg(command.executable);
            return false;
        }
    }
    if (root != QLatin1String("/") && QStandardPaths::findExecutable(QStringLiteral("arch-chroot")).isEmpty()) {
        *error = QObject::tr("arch-chroot is required to repair mounted systems");
        return false;
    }
    return true;
}

QStringList BootRepair::preview(const BootOperation &operation)
{
    QJsonObject data = arguments(operation);
    QString root = data.value(QStringLiteral("root")).toString();
    const bool automatic = autoRoot(root);
    QStringList result;
    if (automatic) {
        root = QStringLiteral("/run/<temporary-repair-root>");
        QString mount = QStringLiteral("mount ");
        const QString subvolume = data.value(QStringLiteral("rootSubvolume")).toString();
        if (!subvolume.isEmpty()) mount += QStringLiteral("-o rw,subvol=") + subvolume + QLatin1Char(' ');
        result.append(mount + data.value(QStringLiteral("rootDevice")).toString() + QLatin1Char(' ') + root);
        data.insert(QStringLiteral("root"), root);
        if (data.value(QStringLiteral("mode")).toString() == QLatin1String("uefi")) {
            const QString mountpoint = root + data.value(QStringLiteral("espRelative")).toString();
            const QString espDevice = data.value(QStringLiteral("espDevice")).toString();
            const QString currentMount = BootProbe::runReadOnly(QStringLiteral("findmnt"),
                {QStringLiteral("-rn"), QStringLiteral("-S"), espDevice,
                 QStringLiteral("-o"), QStringLiteral("TARGET")})
                .split(QLatin1Char('\n'), Qt::SkipEmptyParts).value(0).trimmed();
            result.append(!currentMount.isEmpty()
                ? QStringLiteral("mount --bind ") + currentMount + QLatin1Char(' ') + mountpoint
                : QStringLiteral("mount ") + espDevice + QLatin1Char(' ') + mountpoint);
            data.insert(QStringLiteral("esp"), mountpoint);
        }
    }
    for (const Command &command : commands(data)) {
        const QString line = command.executable + QLatin1Char(' ')
            + command.arguments.join(QLatin1Char(' '));
        result.append(root == QLatin1String("/") ? line
            : QStringLiteral("arch-chroot ") + root + QLatin1Char(' ') + line);
    }
    if (automatic) {
        if (data.value(QStringLiteral("mode")).toString() == QLatin1String("uefi"))
            result.append(QStringLiteral("umount ") + data.value(QStringLiteral("esp")).toString());
        result.append(QStringLiteral("umount ") + root);
    }
    return result;
}

bool BootRepair::execute(const BootOperation &operation, QString *error)
{
    if (!validate(operation, error)) return false;
    QJsonObject data = arguments(operation);
    QString root = data.value(QStringLiteral("root")).toString();
    std::unique_ptr<RepairMount> mounted;
    if (autoRoot(root)) {
        mounted = std::make_unique<RepairMount>();
        if (!mounted->prepare(data, error)) return false;
        root = mounted->path();
        data.insert(QStringLiteral("root"), root);
        if (data.value(QStringLiteral("mode")).toString() == QLatin1String("uefi"))
            data.insert(QStringLiteral("esp"), root + data.value(QStringLiteral("espRelative")).toString());
    }
    bool success = true;
    if (QStringList{"refind", "uki", "efistub"}.contains(data.value("loader").toString()))
        success = BootDeploy::deploy(data, root, error);
    for (const Command &command : commands(data)) {
        const QString executablePath = root == QLatin1String("/")
            ? QStandardPaths::findExecutable(command.executable)
            : root + QStringLiteral("/usr/bin/") + command.executable;
        if (executablePath.isEmpty() || !QFileInfo::exists(executablePath)) {
            *error = QObject::tr("The target system is missing %1").arg(command.executable);
            success = false;
            break;
        }
        QString executable = command.executable;
        QStringList arguments = command.arguments;
        if (root != QLatin1String("/")) {
            arguments.prepend(command.executable);
            arguments.prepend(root);
            executable = QStringLiteral("arch-chroot");
        }
        if (!run(executable, arguments)) {
            *error = QObject::tr("Failed: %1").arg(command.executable);
            success = false;
            break;
        }
    }
    if (mounted) {
        QString cleanupError;
        if (!mounted->cleanup(&cleanupError)) {
            if (error->isEmpty()) *error = cleanupError;
            success = false;
        }
    }
    return success;
}
