#include "BootDiscovery.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <optional>

namespace {
QString normalized(QString path)
{
    return QDir::cleanPath(path.replace(QLatin1Char('\\'), QLatin1Char('/'))).toLower();
}

std::optional<LoaderKind> namedLoader(const QString &path)
{
    const QString name = QFileInfo(path).fileName().toLower();
    static const QRegularExpression grub(QStringLiteral("^grub(?:x64|aa64|ia32)\\.efi$"));
    static const QRegularExpression systemd(QStringLiteral("^systemd-boot(?:x64|aa64|ia32)\\.efi$"));
    static const QRegularExpression refind(QStringLiteral("^refind_(?:x64|aa64|ia32)\\.efi$"));
    if (grub.match(name).hasMatch()) return LoaderKind::Grub;
    if (systemd.match(name).hasMatch()) return LoaderKind::SystemdBoot;
    if (refind.match(name).hasMatch()) return LoaderKind::Refind;
    if (name == QLatin1String("limine.efi") || name == QLatin1String("liminex64.efi")
        || name == QLatin1String("limine_x64.efi")) return LoaderKind::Limine;
    return std::nullopt;
}
}

void BootDiscovery::populate(BootSnapshot &snapshot, const QList<Esp> &partitions,
                             const QString &systemRoot,
                             const std::function<QString(const QString &)> &identifyKernel)
{
    snapshot.installations.clear();
    QString defaultNumber;
    for (const auto &number : snapshot.bootOrder) {
        const auto *entry = snapshot.entry(number);
        if (entry && entry->enabled) { defaultNumber = number; break; }
    }
    for (const auto &partition : partitions) {
        const QString esp = QDir::cleanPath(partition.mount);
        const QString uuid = partition.partitionUuid.toLower();
        auto append = [&](LoaderKind kind, const QString &binary, const QString &config,
                          const QString &evidence) {
            const QString relative = QDir(esp).relativeFilePath(binary.isEmpty() ? config : binary);
            const QString id = loaderName(kind) + QLatin1Char(':') + uuid
                + QLatin1Char(':') + (binary.isEmpty() ? QStringLiteral("config:") : QString())
                + normalized(relative);
            for (const auto &existing : snapshot.installations)
                if (existing.id == id && existing.esp == esp) return;
            BootInstallation installation;
            installation.kind = kind;
            installation.id = uuid.isEmpty() ? id + QLatin1Char(':') + esp : id;
            installation.esp = esp;
            installation.espPartuuid = uuid;
            installation.binary = binary;
            installation.config = config;
            installation.binaryPresent = !binary.isEmpty() && QFileInfo(binary).isFile();
            installation.configPresent = !config.isEmpty() && QFileInfo(config).isFile();
            installation.evidence = evidence;
            for (const auto &entry : snapshot.firmware) {
                if (binary.isEmpty() || uuid.isEmpty() || entry.partitionUuid.toLower() != uuid
                    || normalized(entry.path) != normalized(QLatin1Char('/') + relative)) continue;
                installation.firmwareEntries.append(entry.number);
                installation.firmwareRegistered = true;
                installation.current |= entry.current;
                installation.defaultEntry |= entry.number == defaultNumber;
                installation.nextEntry |= entry.number == snapshot.bootNext;
            }
            snapshot.installations.append(installation);
        };
        QDirIterator iterator(esp + QStringLiteral("/EFI"), QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            if (!path.endsWith(QLatin1String(".efi"), Qt::CaseInsensitive)) continue;
            const QString parent = QFileInfo(path).absolutePath();
            const auto named = namedLoader(path);
            if (named) {
                QString config;
                if (*named == LoaderKind::Refind) config = parent + QStringLiteral("/refind.conf");
                if (*named == LoaderKind::SystemdBoot) config = esp + QStringLiteral("/loader/loader.conf");
                if (*named == LoaderKind::Limine) {
                    const QString local = parent + QStringLiteral("/limine.conf");
                    config = QFileInfo(local).isFile() ? local : esp + QStringLiteral("/limine.conf");
                }
                append(*named, path, config, QObject::tr("Named EFI program; bootability has not been verified"));
                continue;
            }
            const QString type = identifyKernel(path).trimmed();
            if (type == QLatin1String("uki")) {
                append(LoaderKind::Uki, path, {}, QObject::tr("UKI identified by bootctl; firmware registration is separate"));
            } else if (type == QLatin1String("pe")
                       && QFileInfo(path).fileName().compare(QLatin1String("vmlinuz.efi"), Qt::CaseInsensitive) == 0
                       && normalized(QDir(esp).relativeFilePath(path)).startsWith(QLatin1String("efi/catos/"))
                       && QFileInfo(parent + QStringLiteral("/initramfs.img")).isFile()) {
                append(LoaderKind::EfiStub, path, {}, QObject::tr("CatOS EFI Stub deployment layout with PE image and initramfs; bootability is unverified"));
            }
        }
        for (const auto kind : {LoaderKind::SystemdBoot, LoaderKind::Limine}) {
            const QString config = esp + (kind == LoaderKind::SystemdBoot
                ? QStringLiteral("/loader/loader.conf") : QStringLiteral("/limine.conf"));
            bool found = false;
            for (const auto &installation : snapshot.installations)
                found |= installation.esp == esp && installation.kind == kind;
            if (!found && QFileInfo(config).isFile())
                append(kind, {}, config, QObject::tr("Configuration found without a matching EFI program"));
        }
        for (const auto &entry : snapshot.firmware) {
            if (uuid.isEmpty() || entry.partitionUuid.toLower() != uuid) continue;
            const QString relative = normalized(entry.path);
            if (!relative.startsWith(QLatin1String("/efi/"))) continue;
            const auto kind = namedLoader(relative);
            if (kind) append(*kind, esp + relative, {}, QObject::tr("Firmware entry points to this EFI program"));
        }
    }
    for (const auto kind : {LoaderKind::Grub, LoaderKind::Limine}) {
        const QString relative = kind == LoaderKind::Grub
            ? QStringLiteral("etc/default/grub") : QStringLiteral("etc/default/limine");
        const QString config = QDir(systemRoot).filePath(relative);
        if (!QFileInfo(config).isFile()) continue;
        BootInstallation installation;
        installation.kind = kind;
        installation.id = loaderName(kind) + QStringLiteral(":config:") + config;
        installation.config = config;
        installation.configPresent = true;
        installation.evidence = QObject::tr("System generator configuration; EFI deployment is listed separately");
        snapshot.installations.append(installation);
    }
}
