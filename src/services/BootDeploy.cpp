#include "BootDeploy.h"
#include "BootMenus.h"
#include "BootProbe.h"
#include "BootRepair.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSysInfo>
#include <QTextStream>
#include <QStandardPaths>
#include <QtEndian>
#include <efivar/efiboot.h>
#include <cstdlib>
#include <memory>

namespace {
QString hostPath(const QString &root, const QString &path)
{
    if (!path.startsWith('/') || QDir::cleanPath(path) != path) return {};
    const QString result = root == QLatin1String("/") ? path : root + path;
    const QString canonical = QFileInfo(result).canonicalFilePath();
    if (canonical.isEmpty() || (root != QLatin1String("/") && !canonical.startsWith(QFileInfo(root).canonicalFilePath() + '/'))) return {};
    return result;
}

bool command(const QString &root, const QString &program, QStringList args, QString *error)
{
    QString executable = program;
    if (root != QLatin1String("/")) {
        args.prepend(program); args.prepend(root); executable = QStringLiteral("arch-chroot");
    }
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.start(executable, args);
    if (!process.waitForStarted(10000) || !process.waitForFinished(-1)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = QObject::tr("Unable to complete %1.").arg(program); return false;
    }
    return true;
}

QString relativeToRoot(const QString &root, const QString &path)
{
    if (root == QLatin1String("/")) return path;
    const auto relative = path.mid(root.size());
    return relative.isEmpty() ? QStringLiteral("/") : relative;
}

QString architecture()
{
    const auto arch = QSysInfo::currentCpuArchitecture();
    return arch == QLatin1String("aarch64") || arch == QLatin1String("arm64") ? QStringLiteral("aa64")
        : arch == QLatin1String("i386") ? QStringLiteral("ia32") : QStringLiteral("x64");
}

bool copy(const QString &source, const QString &destination, QString *error)
{
    if (QFileInfo(source).canonicalFilePath() == QFileInfo(destination).canonicalFilePath() || QFileInfo(destination).isSymLink() || !QDir().mkpath(QFileInfo(destination).absolutePath())) {
        *error = QObject::tr("Unable to write boot file: %1").arg(destination); return false;
    }
    QFile input(source);
    QSaveFile output(destination);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
        *error = QObject::tr("Unable to copy boot file: %1").arg(source); return false;
    }
    while (!input.atEnd()) {
        const auto chunk = input.read(1024 * 1024);
        if (input.error() != QFileDevice::NoError || output.write(chunk) != chunk.size()) { *error = QObject::tr("Unable to copy boot file: %1").arg(source); return false; }
    }
    if (!output.commit()) { *error = output.errorString(); return false; }
    return true;
}

bool copyDirectory(const QString &source, const QString &destination, QString *error)
{
    if (!QDir(source).exists()) return true;
    if (QFileInfo(destination).isSymLink() || !QDir().mkpath(destination)) { *error = QObject::tr("Unable to write boot files."); return false; }
    for (const auto &entry : QDir(source).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.isSymLink()) { *error = QObject::tr("Invalid boot file: %1").arg(entry.filePath()); return false; }
        const auto target = QDir(destination).filePath(entry.fileName());
        if (entry.isDir() ? !copyDirectory(entry.filePath(), target, error) : !copy(entry.filePath(), target, error)) return false;
    }
    return true;
}

QString mountValue(const QString &root, const QString &field)
{
    return BootProbe::runReadOnly("findmnt", {"-rn", "--nofsroot", "-T", root, "-o", field}).trimmed();
}

bool registerEntry(const QJsonObject &data, const QString &binary, const QString &options, QString *error)
{
    const bool create = data.value("register").toBool();
    if (!create) return true;
    const QString esp = data.value("esp").toString();
    const auto source = mountValue(esp, "SOURCE");
    const auto mount = mountValue(esp, "TARGET");
    if (mount.isEmpty()) { *error = QObject::tr("Unable to locate the boot entry's partition."); return false; }
    const QString efiPath = (QLatin1Char('/') + QDir(mount).relativeFilePath(binary)).replace('/', '\\');
    const auto snapshot = BootProbe::inspect();
    QStringList numbers;
    const QString uuid = BootProbe::runReadOnly("lsblk", {"-dn", "-o", "PARTUUID", source}).trimmed();
    for (const auto &entry : snapshot.firmware) {
        if (!uuid.isEmpty() && entry.partitionUuid.compare(uuid, Qt::CaseInsensitive) == 0
            && entry.path.compare(efiPath, Qt::CaseInsensitive) == 0) numbers.append(entry.number);
    }
    if (!numbers.isEmpty()) {
        for (const auto &number : numbers) {
            uint8_t *raw = nullptr;
            size_t size = 0;
            uint32_t attributes = 0;
            const auto name = (QStringLiteral("Boot") + number).toLatin1();
            if (efi_get_variable(efi_guid_global, name.constData(), &raw, &size, &attributes) < 0) {
                *error = QObject::tr("Unable to read the firmware boot entry."); return false;
            }
            std::unique_ptr<uint8_t, decltype(&std::free)> memory(raw, &std::free);
            const auto replacement = BootDeploy::firmwareData(QByteArray(reinterpret_cast<char *>(raw), size), data.value("label").toString(), options, error);
            if (!error->isEmpty()) return false;
            if (efi_set_variable(efi_guid_global, name.constData(), reinterpret_cast<uint8_t *>(const_cast<char *>(replacement.constData())), replacement.size(), attributes, 0600) < 0) {
                *error = QObject::tr("Unable to update the firmware boot entry."); return false;
            }
        }
    } else {
        if (!create) return true;
        const auto device = BootProbe::runReadOnly("lsblk", {"-dn", "-o", "PKNAME,PARTN", source})
            .trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (device.size() != 2 || !QRegularExpression(QStringLiteral("^[0-9]+$")).match(device[1]).hasMatch()) {
            *error = QObject::tr("Unable to locate the EFI partition."); return false;
        }
        QStringList args{"--create", "--disk", "/dev/" + device[0], "--part", device[1], "--loader", efiPath,
            "--label", data.value("label").toString()};
        if (!options.isEmpty()) args << "--unicode" << options;
        if (!command("/", "efibootmgr", args, error)) return false;
    }
    return true;
}
}

QByteArray BootDeploy::firmwareData(const QByteArray &original, const QString &label, const QString &options, QString *error)
{
    error->clear();
    auto *option = reinterpret_cast<efi_load_option *>(const_cast<char *>(original.constData()));
    if (efi_loadopt_is_valid(option, original.size()) != 1) { *error = QObject::tr("Invalid firmware boot entry."); return {}; }
    const auto path = efi_loadopt_path(option, original.size());
    const auto pathSize = efi_loadopt_pathlen(option, original.size());
    const auto title = label.toUtf8();
    QByteArray arguments((options.size() + 1) * 2, '\0');
    for (int i = 0; i < options.size(); ++i) qToLittleEndian<quint16>(options[i].unicode(), arguments.data() + i * 2);
    auto *description = reinterpret_cast<unsigned char *>(const_cast<char *>(title.constData()));
    auto *optional = reinterpret_cast<uint8_t *>(arguments.data());
    const auto count = options.isEmpty() ? 0 : arguments.size();
    const auto size = efi_loadopt_create(nullptr, 0, efi_loadopt_attrs(option), path, pathSize, description, optional, count);
    if (size <= 0) { *error = QObject::tr("Unable to update the firmware boot entry."); return {}; }
    QByteArray output(size, '\0');
    if (efi_loadopt_create(reinterpret_cast<uint8_t *>(output.data()), size, efi_loadopt_attrs(option), path, pathSize, description, optional, count) != size) {
        *error = QObject::tr("Unable to update the firmware boot entry."); return {};
    }
    return output;
}

QString BootDeploy::loaderId(LoaderKind kind)
{
    switch (kind) {
    case LoaderKind::Grub: return "grub";
    case LoaderKind::Limine: return "limine";
    case LoaderKind::Refind: return "refind";
    case LoaderKind::SystemdBoot: return "systemd-boot";
    case LoaderKind::Uki: return "uki";
    case LoaderKind::EfiStub: return "efistub";
    }
    return {};
}

bool BootDeploy::directImage(LoaderKind kind) { return kind == LoaderKind::Uki || kind == LoaderKind::EfiStub; }
QString BootDeploy::configurationPath(const QString &root)
{
    return (root == QLatin1String("/") ? QString() : root) + QStringLiteral("/etc/catos/firmware-boot.conf");
}

namespace {
QString configurationRoot(const QString &path)
{
    const QString suffix = QStringLiteral("/etc/catos/firmware-boot.conf");
    if (!path.endsWith(suffix) || QDir::cleanPath(path) != path) return {};
    const auto root = path.left(path.size() - suffix.size());
    return root.isEmpty() ? QStringLiteral("/") : root;
}

QStringList configurationArguments(const QString &root)
{
    return {"--config", relativeToRoot(root, BootDeploy::configurationPath(root))};
}
}

namespace {
bool singleLine(const QString &value)
{
    for (const auto ch : value) if (ch.unicode() < 32 || ch.unicode() == 127) return false;
    return true;
}

// Reads a mounted system's configuration as data, mirroring the provider's
// --describe output. Programs inside another system are never executed while
// inspecting it: the system may be an untrusted, automatically mounted drive.
QJsonObject mountedProfile(const QString &root, QString *error)
{
    const auto invalid = [error] { *error = QObject::tr("Unable to read boot settings."); return QJsonObject(); };
    const QString path = BootDeploy::configurationPath(root);
    if (!BootMenus::safePath(path, root) || !QFileInfo(path).isFile()) return invalid();
    const QString text = BootMenus::readText(path, error);
    if (!error->isEmpty()) return invalid();
    QMap<QString, QMap<QString, QString>> sections;
    QString section;
    for (const auto &line : text.split(QLatin1Char('\n'))) {
        const QString clean = line.trimmed();
        if (clean.isEmpty() || clean.startsWith(QLatin1Char('#')) || clean.startsWith(QLatin1Char(';'))) continue;
        if (line.front().isSpace()) return invalid();
        if (clean.startsWith(QLatin1Char('['))) {
            if (!clean.endsWith(QLatin1Char(']'))) return invalid();
            section = clean.mid(1, clean.size() - 2);
            if (sections.contains(section)) return invalid();
            sections.insert(section, {});
            continue;
        }
        int separator = -1;
        for (const QChar delimiter : {QLatin1Char('='), QLatin1Char(':')}) {
            const int position = clean.indexOf(delimiter);
            if (position > 0 && (separator < 0 || position < separator)) separator = position;
        }
        if (section.isEmpty() || separator < 0) return invalid();
        const QString key = clean.left(separator).trimmed().toLower();
        if (key.isEmpty() || sections[section].contains(key)) return invalid();
        sections[section].insert(key, clean.mid(separator + 1).trimmed());
    }
    if (!sections.contains(QStringLiteral("boot"))) return invalid();
    const auto values = sections.value(QStringLiteral("boot"));
    const QString method = values.value("method", "disabled").toLower();
    bool partitionValid = true;
    const int partition = values.contains("efi_partition") ? values.value("efi_partition").toInt(&partitionValid) : 0;
    const QString fallbackText = values.value("install_fallback", "no").toLower();
    const bool fallback = QStringList{"1", "yes", "true", "on"}.contains(fallbackText);
    const QString esp = values.value("esp_path", "/boot/efi");
    const QString disk = values.value("efi_disk");
    const QString directory = values.value("firmware_directory", "/");
    const QString label = values.value("label_prefix").isEmpty() ? QStringLiteral("CatOS") : values.value("label_prefix");
    const QString kernel = values.value("default_kernel").isEmpty() ? QStringLiteral("linux") : values.value("default_kernel");
    if (!QStringList{"disabled", "uki", "efistub"}.contains(method) || !partitionValid || partition < 0
        || (!fallback && !QStringList{"0", "no", "false", "off"}.contains(fallbackText))
        || !QDir::isAbsolutePath(esp) || disk.isEmpty() != (partition == 0)
        || !directory.startsWith(QLatin1Char('/')) || directory.contains(QLatin1Char('\\'))
        || directory.split(QLatin1Char('/')).contains(QStringLiteral(".."))) return invalid();
    for (const auto &value : {esp, disk, directory, label, kernel}) if (!singleLine(value)) return invalid();
    QString cmdline;
    const QString cmdlinePath = (root == QLatin1String("/") ? QString() : root) + QStringLiteral("/etc/kernel/cmdline");
    if (QFileInfo(cmdlinePath).isSymLink()) return invalid();
    if (QFileInfo(cmdlinePath).isFile()) {
        if (!BootMenus::safePath(cmdlinePath, root)) return invalid();
        cmdline = BootMenus::readText(cmdlinePath, error).trimmed();
        if (!error->isEmpty()) return invalid();
    }
    return {{"loader", method}, {"esp", esp}, {"efi_disk", disk}, {"efi_partition", partition},
        {"label_prefix", label}, {"default_kernel", kernel}, {"fallback", fallback},
        {"firmware_directory", directory}, {"cmdline", cmdline}};
}
}

QJsonObject BootDeploy::profile(const QString &path, QString *error)
{
    error->clear();
    const auto root = configurationRoot(path);
    if (root.isEmpty()) { *error = QObject::tr("Select a target system."); return {}; }
    QJsonObject document;
    if (root == QLatin1String("/")) {
        const auto output = BootProbe::runReadOnly("catos-firmware-boot-update", configurationArguments(root) + QStringList{"--describe"});
        QJsonParseError parseError;
        const auto parsed = QJsonDocument::fromJson(output.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
            *error = QObject::tr("Unable to read boot settings."); return {};
        }
        document = parsed.object();
    } else {
        document = mountedProfile(root, error);
        if (!error->isEmpty()) return {};
    }
    auto data = document;
    data.insert("root", root);
    if (root != QLatin1String("/")) data.insert("esp", root + data.value("esp").toString());
    return data;
}

bool BootDeploy::validateInputs(const QJsonObject &data, const QString &root, QString *error)
{
    error->clear();
    const auto loader = data.value("loader").toString();
    if (!QStringList{"refind", "uki", "efistub"}.contains(loader)) {
        *error = QObject::tr("Select a boot loader."); return false;
    }
    const auto destination = data.value("esp").toString();
    if (!destination.startsWith('/') || QDir::cleanPath(destination) != destination
        || (root != QLatin1String("/") && destination != root && !destination.startsWith(root + '/'))
        || (destination != root && !BootMenus::safePath(destination, root))) {
        *error = QObject::tr("Choose an installation directory."); return false;
    }
    for (const auto &key : {QStringLiteral("cmdline"), QStringLiteral("label"), QStringLiteral("label_prefix"), QStringLiteral("default_kernel")}) {
        for (const auto ch : data.value(key).toString()) {
            if (ch.unicode() < 32 || ch.unicode() == 127) { *error = QObject::tr("Enter a single-line value."); return false; }
        }
    }
    if (loader == QLatin1String("refind")) {
        const auto source = hostPath(root, "/usr/share/refind/refind_" + architecture() + ".efi");
        if (source.isEmpty() || QFileInfo(source).size() == 0) { *error = QObject::tr("Install the rEFInd software package in the target system."); return false; }
        if (data.value("cmdline").toString().contains('"')) { *error = QObject::tr("Invalid startup parameters."); return false; }
    } else {
        const auto executable = root == QLatin1String("/") ? QStandardPaths::findExecutable("catos-firmware-boot-update")
            : root + "/usr/bin/catos-firmware-boot-update";
        if (executable.isEmpty() || !QFileInfo(executable).isExecutable()) {
            *error = QObject::tr("Install the catos-firmware-boot software package in the target system."); return false;
        }
    }
    return true;
}

bool BootDeploy::deploy(const QJsonObject &input, const QString &root, QString *error)
{
    if (!validateInputs(input, root, error)) return false;
    auto data = input;
    const auto esp = data.value("esp").toString();
    const auto loader = data.value("loader").toString();
    if (loader != QLatin1String("refind")) {
        auto args = configurationArguments(root);
        args << "--method" << loader << "--output-root" << relativeToRoot(root, esp)
             << "--install-fallback" << (data.value("fallback").toBool() ? "yes" : "no") << "--save-config";
        if (data.value("register").toBool()) {
            QString existing = esp;
            while (!QFileInfo::exists(existing) && existing != QLatin1String("/")) existing = QFileInfo(existing).absolutePath();
            const auto source = BootProbe::runReadOnly("findmnt", {"-rn", "--nofsroot", "-T", existing, "-o", "SOURCE"}).trimmed();
            const auto device = BootProbe::runReadOnly("lsblk", {"-dn", "-o", "PKNAME,PARTN", source}).trimmed()
                .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (device.size() != 2 || !QRegularExpression(QStringLiteral("^[1-9][0-9]*$")).match(device[1]).hasMatch()) {
                *error = QObject::tr("Choose a directory on the partition for this boot entry."); return false;
            }
            const auto mount = BootProbe::runReadOnly("findmnt", {"-rn", "-T", existing, "-o", "TARGET"}).trimmed();
            if (mount.isEmpty() || (esp != mount && !esp.startsWith(mount == QLatin1String("/") ? mount : mount + '/'))) {
                *error = QObject::tr("Unable to locate the boot entry's partition."); return false;
            }
            const auto relative = QDir(mount).relativeFilePath(esp);
            args << "--efi-disk" << "/dev/" + device[0] << "--efi-partition" << device[1]
                 << "--firmware-directory" << (relative == QLatin1String(".") ? QStringLiteral("/") : '/' + relative);
        } else {
            args << "--efi-disk" << "" << "--efi-partition" << "0" << "--firmware-directory" << "/" << "--no-variables";
        }
        return command(root, "catos-firmware-boot-update", args, error);
    }
    const auto arch = architecture();
    for (const auto &directory : {esp + "/EFI", esp + "/EFI/refind", esp + "/EFI/BOOT"}) {
        if (!BootMenus::safePath(directory, root)) { *error = QObject::tr("Invalid EFI destination."); return false; }
    }
    QString binary;
    if (loader == QLatin1String("refind")) {
        const auto destination = esp + QStringLiteral("/EFI/refind");
        binary = destination + "/refind_" + arch + ".efi";
        const auto source = root == QLatin1String("/") ? QStringLiteral("/usr/share/refind") : root + QStringLiteral("/usr/share/refind");
        if (!copy(source + "/refind_" + arch + ".efi", binary, error)
            || !copyDirectory(source + "/drivers_" + arch, destination + "/drivers_" + arch, error)
            || !copyDirectory(source + "/icons", destination + "/icons", error)) return false;
        const auto config = destination + QStringLiteral("/refind.conf");
        if (!QFileInfo::exists(config) && !BootMenus::save(config,
            QStringLiteral("timeout 10\nscanfor internal,external,optical,manual\n"), error)) return false;
        const auto linuxOptions = root == QLatin1String("/") ? QStringLiteral("/boot/refind_linux.conf") : root + QStringLiteral("/boot/refind_linux.conf");
        if (!data.value("cmdline").toString().isEmpty() && !QFileInfo::exists(linuxOptions)) {
            QString parameters = data.value("cmdline").toString();
            if (parameters.contains('"') || parameters.contains('\n')) { *error = QObject::tr("Invalid startup parameters."); return false; }
            if (!BootMenus::save(linuxOptions, "\"Boot Linux\" \"" + parameters + "\"\n", error)) return false;
        }
    }
    if (data.value("fallback").toBool()) {
        const auto destination = esp + "/EFI/BOOT";
        const auto filename = arch == QLatin1String("aa64") ? QStringLiteral("BOOTAA64.EFI")
            : arch == QLatin1String("ia32") ? QStringLiteral("BOOTIA32.EFI") : QStringLiteral("BOOTX64.EFI");
        if (!copy(binary, destination + '/' + filename, error) || !copyDirectory(esp + "/EFI/refind", destination, error)) return false;
    }
    return registerEntry(data, binary, {}, error);
}

bool BootDeploy::writeSetting(LoaderKind kind, const QString &path, const QString &key, const QString &value, QString *error)
{
    const auto root = configurationRoot(path);
    if (root.isEmpty() || !QStringList{"cmdline", "label_prefix", "default_kernel"}.contains(key)) {
        *error = QObject::tr("Invalid boot setting."); return false;
    }
    auto args = configurationArguments(root);
    Q_UNUSED(kind);
    args << "--configure-only" << "--" + QString(key).replace('_', '-') + '=' + value;
    return command(root, "catos-firmware-boot-update", args, error);
}

bool BootDeploy::validateRebuild(LoaderKind kind, const QString &path, const QMap<QString, QString> &values, const BootSnapshot &snapshot, QString *error)
{
    auto data = profile(path, error);
    if (!error->isEmpty()) return false;
    if (!snapshot.repairRoots.contains(data.value("root").toString())) { *error = QObject::tr("Select a target system."); return false; }
    bool bound = false;
    for (const auto &installation : snapshot.installations)
        bound |= directImage(kind) && installation.kind == kind && installation.config == path;
    data.insert("loader", loaderId(kind));
    if (!bound) { *error = QObject::tr("The boot configuration changed. Refresh and try again."); return false; }
    for (auto it = values.begin(); it != values.end(); ++it) data.insert(it.key(), it.value());
    return validateInputs(data, data.value("root").toString(), error);
}

bool BootDeploy::rebuild(LoaderKind kind, const QString &path, QString *error)
{
    const auto root = configurationRoot(path);
    if (root.isEmpty()) { *error = QObject::tr("Select a target system."); return false; }
    return command(root, "catos-firmware-boot-update", configurationArguments(root) + QStringList{"--method", loaderId(kind)}, error);
}

void BootDeploy::populate(BootSnapshot &snapshot)
{
    snapshot.kernels = {};
    QSet<QString> seen;
    for (const auto &root : snapshot.repairRoots) {
        const QString prefix = root == QLatin1String("/") ? QString() : root;
        const QString modules = prefix + "/usr/lib/modules";
        for (const auto &version : QDir(modules).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString identity = root + '|' + version;
            if (seen.contains(identity)) continue;
            const QString kernel = QStringLiteral("/usr/lib/modules/") + version + "/vmlinuz";
            if (!QFileInfo(hostPath(root, kernel)).isFile()) continue;
            QString ignored;
            const auto packagePath = modules + '/' + version + "/pkgbase";
            const QString package = QFileInfo::exists(packagePath) ? BootMenus::readText(packagePath, &ignored).trimmed() : version;
            if (!QRegularExpression("^[A-Za-z0-9_.+-]+$").match(package).hasMatch()) continue;
            seen.insert(identity);
            snapshot.kernels.append(QJsonObject{{"root", root}, {"name", package}, {"version", version}, {"kernel", kernel}});
        }
    }
}
