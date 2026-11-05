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
    return root == QLatin1String("/") ? path : path.mid(root.size());
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
    return BootProbe::runReadOnly("findmnt", {"-rn", "-M", root, "-o", field}).trimmed();
}

bool registerEntry(const QJsonObject &data, const QString &binary, const QString &options, QString *error)
{
    const bool create = data.value("register").toBool();
    const auto loader = data.value("loader").toString();
    if (!create && loader == QLatin1String("refind")) return true;
    const QString esp = data.value("esp").toString();
    const auto source = mountValue(esp, "SOURCE");
    if (source.isEmpty() && !create) return true;
    const QString efiPath = (QLatin1Char('/') + QDir(esp).relativeFilePath(binary)).replace('/', '\\');
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
QString BootDeploy::profilePath(const QString &binary) { return binary + QStringLiteral(".catos.json"); }

QJsonObject BootDeploy::profile(const QString &path, QString *error)
{
    error->clear();
    if (!QFileInfo::exists(path)) return {};
    const auto document = QJsonDocument::fromJson(BootMenus::readText(path, error).toUtf8());
    if (error->isEmpty() && !document.isObject()) *error = QObject::tr("Unable to read generation settings.");
    return document.object();
}

bool BootDeploy::validateInputs(const QJsonObject &data, const QString &root, QString *error)
{
    error->clear();
    const auto loader = data.value("loader").toString();
    const auto id = data.value("id").toString();
    const auto label = data.value("label").toString();
    for (const auto ch : label) if (ch.unicode() < 32 || ch.unicode() == 127) { *error = QObject::tr("Enter a valid installation name."); return false; }
    if (!QStringList{"refind", "uki", "efistub"}.contains(loader)
        || !QRegularExpression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,70}$")).match(id).hasMatch()
        || label.trimmed().isEmpty() || label.size() > 80 || label.contains('\n') || label.contains('\r')) {
        *error = QObject::tr("Enter a valid installation name."); return false;
    }
    const auto esp = data.value("esp").toString();
    if (esp.isEmpty() || !QDir(esp).exists() || QDir::cleanPath(esp) != esp || QFileInfo(esp).isSymLink()
        || (root != QLatin1String("/") && !esp.startsWith(root + '/'))) {
        *error = QObject::tr("Select a mounted EFI partition."); return false;
    }
    const auto binary = data.value("binary").toString(loader == QLatin1String("uki") ? esp + "/EFI/Linux/catos-" + id + ".efi"
        : loader == QLatin1String("efistub") ? esp + "/EFI/CatOS/" + id + "/vmlinuz.efi" : esp + "/EFI/refind/refind_" + architecture() + ".efi");
    if (!binary.startsWith(esp + "/EFI/") || !BootMenus::safePath(binary, esp) || !BootMenus::safePath(profilePath(binary), esp)
        || binary == hostPath(root, data.value("kernel").toString())
        || (loader == QLatin1String("efistub") && QFileInfo(binary).absolutePath() + "/initramfs.img" == hostPath(root, data.value("initramfs").toString()))) {
        *error = QObject::tr("Invalid EFI destination."); return false;
    }
    if (data.value("fallback").toBool() && loader == QLatin1String("efistub")) { *error = QObject::tr("Invalid EFI destination."); return false; }
    const auto parameters = data.value("cmdline").toString();
    if (parameters.contains('\n') || parameters.contains('\r') || parameters.contains(QChar::Null)
        || (loader == QLatin1String("refind") && parameters.contains('"'))) { *error = QObject::tr("Invalid startup parameters."); return false; }
    if (loader == QLatin1String("refind")) {
        const auto source = hostPath(root, "/usr/share/refind/refind_" + architecture() + ".efi");
        if (source.isEmpty() || QFileInfo(source).size() == 0) { *error = QObject::tr("Install the rEFInd software package in the target system."); return false; }
        return true;
    }
    for (const auto &key : {QStringLiteral("kernel"), QStringLiteral("initramfs")}) {
        const auto input = hostPath(root, data.value(key).toString());
        if (input.isEmpty() || !QFileInfo(input).isFile() || QFileInfo(input).size() == 0) {
            *error = QObject::tr("Select an existing kernel and initramfs in the target system."); return false;
        }
    }
    for (const auto &key : {QStringLiteral("cmdline"), QStringLiteral("version"), QStringLiteral("private-key"), QStringLiteral("certificate")}) {
        const auto value = data.value(key).toString();
        if (value.contains('\n') || value.contains('\r') || value.contains(QChar::Null)) { *error = QObject::tr("Invalid startup parameters."); return false; }
    }
    if (loader == QLatin1String("efistub") && (!data.value("private-key").toString().isEmpty() || !data.value("certificate").toString().isEmpty())) { *error = QObject::tr("Invalid image settings."); return false; }
    const auto key = data.value("private-key").toString();
    const auto certificate = data.value("certificate").toString();
    if (key.isEmpty() != certificate.isEmpty()
        || (!key.isEmpty() && (hostPath(root, key).isEmpty() || hostPath(root, certificate).isEmpty()))) {
        *error = QObject::tr("Select both the signing key and certificate."); return false;
    }
    return true;
}

bool BootDeploy::deploy(const QJsonObject &input, const QString &root, QString *error)
{
    if (!validateInputs(input, root, error)) return false;
    auto data = input;
    const auto esp = data.value("esp").toString();
    if (esp.isEmpty() || QDir::cleanPath(esp) != esp || QFileInfo(esp).isSymLink() || !QDir(esp).exists()
        || (root != QLatin1String("/") && !esp.startsWith(root + "/"))) { *error = QObject::tr("Select a mounted EFI partition."); return false; }
    const auto loader = data.value("loader").toString();
    const auto id = data.value("id").toString();
    const auto arch = architecture();
    // Reject all symlink ancestors before creating or replacing boot files.
    for (const auto &directory : {esp + "/EFI", esp + "/EFI/refind", esp + "/EFI/BOOT", esp + "/EFI/Linux", esp + "/EFI/CatOS", esp + "/EFI/CatOS/" + id}) {
        if (!BootMenus::safePath(directory, esp)) { *error = QObject::tr("Invalid EFI destination."); return false; }
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
    } else {
        binary = data.value("binary").toString();
        if (binary.isEmpty()) binary = loader == QLatin1String("uki") ? esp + "/EFI/Linux/catos-" + id + ".efi"
            : esp + "/EFI/CatOS/" + id + "/vmlinuz.efi";
        if (QDir::cleanPath(binary) != binary || !QDir::cleanPath(binary).startsWith(QDir::cleanPath(esp) + "/EFI/")
            || !BootMenus::safePath(binary, esp) || hostPath(root, data.value("kernel").toString()) == binary
            || hostPath(root, data.value("initramfs").toString()) == QFileInfo(binary).absolutePath() + "/initramfs.img"
            || !QDir().mkpath(QFileInfo(binary).absolutePath())) {
            *error = QObject::tr("Invalid EFI destination."); return false;
        }
        const auto kernel = data.value("kernel").toString();
        const auto initramfs = data.value("initramfs").toString();
        if (loader == QLatin1String("uki")) {
            const QString temporary = binary + QStringLiteral(".new");
            if (QFileInfo::exists(temporary) || QFileInfo(temporary).isSymLink()) { *error = QObject::tr("The output image is already being generated."); return false; }
            QStringList args{"build", "--config=/dev/null", "--linux=" + kernel, "--initrd=" + initramfs,
                "--cmdline=" + data.value("cmdline").toString(), "--output=" + relativeToRoot(root, temporary)};
            if (!data.value("version").toString().isEmpty()) args << "--uname=" + data.value("version").toString();
            if (!data.value("private-key").toString().isEmpty())
                args << "--secureboot-private-key=" + data.value("private-key").toString()
                     << "--secureboot-certificate=" + data.value("certificate").toString();
            if (!command(root, "ukify", args, error)) { QFile::remove(temporary); return false; }
            if (!copy(temporary, binary, error)) { QFile::remove(temporary); return false; }
            if (!QFile::remove(temporary)) { *error = QObject::tr("Unable to finish writing the UKI image."); return false; }
        } else {
            if (!copy(hostPath(root, kernel), binary, error)
                || !copy(hostPath(root, initramfs), QFileInfo(binary).absolutePath() + "/initramfs.img", error)) return false;
        }
        data.insert("binary", binary);
        data.insert("root", root);
        data.insert("rootSource", mountValue(root, "SOURCE"));
        data.insert("rootUuid", mountValue(root, "UUID"));
        data.insert("rootFsRoot", mountValue(root, "FSROOT"));
        if (!BootMenus::save(profilePath(binary), QString::fromUtf8(QJsonDocument(data).toJson()), error)) return false;
    }
    if (data.value("fallback").toBool()) {
        const auto destination = esp + "/EFI/BOOT";
        const auto filename = arch == QLatin1String("aa64") ? QStringLiteral("BOOTAA64.EFI")
            : arch == QLatin1String("ia32") ? QStringLiteral("BOOTIA32.EFI") : QStringLiteral("BOOTX64.EFI");
        if (!copy(binary, destination + '/' + filename, error)) return false;
        if (loader == QLatin1String("refind") && !copyDirectory(esp + "/EFI/refind", destination, error)) return false;
    }
    QString options;
    if (loader == QLatin1String("efistub")) options = "initrd=" + (QLatin1Char('/') + QDir(esp).relativeFilePath(QFileInfo(binary).absolutePath() + "/initramfs.img")).replace('/', '\\')
        + ' ' + data.value("cmdline").toString();
    return registerEntry(data, binary, options, error);
}

bool BootDeploy::writeSetting(LoaderKind kind, const QString &path, const QString &key, const QString &value, QString *error)
{
    auto data = profile(path, error);
    if (!error->isEmpty()) return false;
    const QString binary = path.left(path.size() - QStringLiteral(".catos.json").size());
    if (data.isEmpty()) {
        QString esp;
        for (const auto &mount : BootProbe::espMounts()) if (binary.startsWith(mount + '/')) esp = mount;
        if (esp.isEmpty()) { *error = QObject::tr("Select a mounted EFI partition."); return false; }
        data = {{"loader", loaderId(kind)}, {"binary", binary}, {"esp", esp},
            {"id", QFileInfo(binary).baseName()}, {"register", false}, {"fallback", false}};
    }
    if (key == QLatin1String("root") && data.value(key).toString() != value) {
        data.remove("rootSource"); data.remove("rootUuid"); data.remove("rootFsRoot");
    }
    data.insert(key, value);
    return BootMenus::save(path, QString::fromUtf8(QJsonDocument(data).toJson()), error);
}

namespace {
QJsonObject rebuildData(const QString &path, const QMap<QString, QString> &values, const BootSnapshot &snapshot, QString *error)
{
    auto data = BootDeploy::profile(path, error);
    if (!error->isEmpty()) return {};
    const QString binary = path.left(path.size() - QStringLiteral(".catos.json").size());
    const BootInstallation *installation = nullptr;
    for (const auto &loader : snapshot.installations)
        if (BootDeploy::directImage(loader.kind) && loader.config == path && loader.binary == binary) installation = &loader;
    if (!installation || (!data.isEmpty() && data.value("loader").toString() != BootDeploy::loaderId(installation->kind))) {
        *error = QObject::tr("The selected image changed. Refresh and try again."); return {};
    }
    if (data.isEmpty()) data = {{"id", QFileInfo(binary).baseName()}, {"label", loaderName(installation->kind)}};
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (it.key() == QLatin1String("root") && data.value("root").toString() != it.value()) {
            data.remove("rootSource"); data.remove("rootUuid"); data.remove("rootFsRoot");
        }
        data.insert(it.key(), it.value());
    }
    data.insert("loader", BootDeploy::loaderId(installation->kind));
    data.insert("binary", binary);
    data.insert("esp", installation->esp);
    if (!error->isEmpty() || data.isEmpty()) { *error = QObject::tr("Save the generation settings first."); return {}; }
    const auto root = BootDeploy::targetRoot(data, snapshot.repairRoots, error);
    if (!error->isEmpty()) return {};
    data.insert("root", root);
    return data;
}
}

QString BootDeploy::targetRoot(const QJsonObject &data, const QStringList &candidates, QString *error)
{
    error->clear();
    QString root;
    for (const auto &candidate : candidates) {
        const auto uuid = data.value("rootUuid").toString();
        const auto source = data.value("rootSource").toString();
        const bool matches = !uuid.isEmpty() ? mountValue(candidate, "UUID") == uuid
            : !source.isEmpty() ? mountValue(candidate, "SOURCE") == source
            : candidate == data.value("root").toString();
        if (matches && (data.value("rootFsRoot").toString().isEmpty()
            || mountValue(candidate, "FSROOT") == data.value("rootFsRoot").toString())) {
            if (!root.isEmpty()) { *error = QObject::tr("Select the target system for this image."); return QString(); }
            root = candidate;
        }
    }
    if (root.isEmpty()) { *error = QObject::tr("Mount the target system and refresh detection."); return QString(); }
    return root;
}

bool BootDeploy::validateRebuild(const QString &path, const QMap<QString, QString> &values, const BootSnapshot &snapshot, QString *error)
{
    const auto data = rebuildData(path, values, snapshot, error);
    if (!error->isEmpty()) return false;
    const auto root = data.value("root").toString();
    if (!validateInputs(data, root, error)) return false;
    if (!snapshot.espMounts.contains(data.value("esp").toString())) { *error = QObject::tr("Select a mounted EFI partition."); return false; }
    if (data.value("loader").toString() == QLatin1String("uki")) {
        const auto executable = root == QLatin1String("/") ? QStandardPaths::findExecutable("ukify") : root + "/usr/bin/ukify";
        if (executable.isEmpty() || !QFileInfo(executable).isExecutable()) { *error = QObject::tr("Install ukify in the target system."); return false; }
    }
    return true;
}

bool BootDeploy::rebuild(const QString &path, QString *error)
{
    BootSnapshot snapshot;
    snapshot.repairRoots = BootRepair::mountedTargets();
    snapshot.espMounts = BootProbe::espMounts();
    const QString binary = path.left(path.size() - QStringLiteral(".catos.json").size());
    auto data = profile(path, error);
    if (!error->isEmpty()) return false;
    const auto kind = data.value("loader").toString() == QLatin1String("uki") ? LoaderKind::Uki : LoaderKind::EfiStub;
    for (const auto &esp : snapshot.espMounts) if (binary.startsWith(esp + "/EFI/")) {
        BootInstallation loader; loader.kind = kind; loader.binary = binary; loader.config = path; loader.esp = esp;
        snapshot.installations.append(loader);
    }
    data = rebuildData(path, {}, snapshot, error);
    return error->isEmpty() && deploy(data, data.value("root").toString(), error);
}

void BootDeploy::populate(BootSnapshot &snapshot)
{
    snapshot.kernels = {};
    QSet<QString> seen;
    for (const auto &root : snapshot.repairRoots) {
        const QString prefix = root == QLatin1String("/") ? QString() : root;
        QString ignored;
        const QString machineId = BootMenus::readText(prefix + "/etc/machine-id", &ignored).trimmed();
        const QString cmdline = BootMenus::readText(prefix + "/etc/kernel/cmdline", &ignored).trimmed();
        const QString modules = prefix + "/usr/lib/modules";
        for (const auto &version : QDir(modules).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString identity = root + '|' + version;
            if (seen.contains(identity)) continue;
            const QString package = BootMenus::readText(modules + '/' + version + "/pkgbase", &ignored).trimmed();
            const QString kernel = QStringLiteral("/usr/lib/modules/") + version + "/vmlinuz";
            const QString kernelHost = hostPath(root, kernel);
            if (!QRegularExpression("^[A-Za-z0-9_.+-]+$").match(package).hasMatch() || !QFileInfo(kernelHost).isFile()) continue;
            QString initramfs = "/boot/initramfs-" + package + ".img";
            if (!QFileInfo(hostPath(root, initramfs)).isFile()) initramfs.clear();
            if (initramfs.isEmpty() && QRegularExpression("^[a-fA-F0-9]{32}$").match(machineId).hasMatch()) {
                QFile installed(kernelHost);
                QCryptographicHash installedHash(QCryptographicHash::Sha256);
                if (installed.open(QIODevice::ReadOnly) && installedHash.addData(&installed)) {
                    for (const auto &esp : snapshot.espMounts) {
                        if (!prefix.isEmpty() && !esp.startsWith(prefix + '/')) continue;
                        const QString directory = esp + '/' + machineId + '/' + package;
                        const QString deployedKernel = hostPath(root, relativeToRoot(root, directory + "/vmlinuz"));
                        const QString deployedInitrd = relativeToRoot(root, directory + "/initramfs");
                        if (!QFileInfo(hostPath(root, deployedInitrd)).isFile()) continue;
                        QFile deployed(deployedKernel);
                        QCryptographicHash deployedHash(QCryptographicHash::Sha256);
                        if (deployed.open(QIODevice::ReadOnly) && deployedHash.addData(&deployed)
                            && deployedHash.result() == installedHash.result()) {
                            initramfs = deployedInitrd;
                            break;
                        }
                    }
                }
            }
            seen.insert(identity);
            snapshot.kernels.append(QJsonObject{{"root", root}, {"name", package}, {"version", version},
                {"kernel", kernel}, {"initramfs", initramfs}, {"cmdline", cmdline}});
        }
    }
}
