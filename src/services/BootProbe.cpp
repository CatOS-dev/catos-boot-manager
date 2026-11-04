#include "BootProbe.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace {
QString cleanPath(QString path)
{
    return QDir::cleanPath(path).replace(QLatin1Char('\\'), QLatin1Char('/'));
}

QString firmwareLoader(const QString &path, const QString &label)
{
    const QString name = path.toLower();
    if (name.contains(QLatin1String("limine"))) return QStringLiteral("Limine");
    if (name.contains(QLatin1String("systemd-boot"))) return QStringLiteral("systemd-boot");
    if (name.contains(QLatin1String("grub"))) return QStringLiteral("GRUB");
    if (name.contains(QLatin1String("shim"))) return QObject::tr("shim (may chainload GRUB)");
    if (name.contains(QLatin1String("bootx64.efi"))) return QObject::tr("EFI fallback (not identified)");
    if (label.contains(QLatin1String("limine"), Qt::CaseInsensitive)) return QObject::tr("Limine (firmware label only)");
    return {};
}

QString readLoaderInfo()
{
    const QString directory = QStringLiteral("/sys/firmware/efi/efivars");
    const QStringList files = QDir(directory).entryList({QStringLiteral("LoaderInfo-*")}, QDir::Files);
    for (const auto &name : files) {
        QFile file(directory + QLatin1Char('/') + name);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray raw = file.readAll();
        if (raw.size() <= 4) continue;
        QString text;
        for (int i = 4; i + 1 < raw.size(); i += 2) {
            const auto value = char16_t(quint8(raw.at(i)) | (quint16(quint8(raw.at(i + 1))) << 8));
            if (!value) break;
            text.append(QChar(value));
        }
        if (!text.isEmpty()) return text;
    }
    return {};
}

void addInstallation(BootSnapshot &state, LoaderKind kind, const QString &esp,
                     const QString &binary, const QString &config)
{
    const QString id = loaderName(kind) + QLatin1Char(':') + esp;
    for (auto &entry : state.installations) {
        if (entry.id != id) continue;
        if (entry.binary.isEmpty()) entry.binary = binary;
        if (entry.config.isEmpty()) entry.config = config;
        entry.binaryPresent |= !binary.isEmpty();
        entry.configPresent |= QFileInfo::exists(config);
        return;
    }
    QString partuuid;
    if (!esp.isEmpty()) {
        const QString source = BootProbe::runReadOnly(QStringLiteral("findmnt"),
            {QStringLiteral("-rn"), QStringLiteral("-T"), esp,
             QStringLiteral("-o"), QStringLiteral("SOURCE")}).trimmed();
        if (!source.isEmpty()) {
            partuuid = BootProbe::runReadOnly(QStringLiteral("lsblk"),
                {QStringLiteral("-dn"), QStringLiteral("-o"), QStringLiteral("PARTUUID"), source})
                .trimmed().toLower();
        }
    }
    state.installations.append({kind, id, esp, partuuid, binary, config,
                                QFileInfo::exists(config), !binary.isEmpty(), false, false});
}
}

QString BootProbe::runReadOnly(const QString &program, const QStringList &arguments)
{
    QString executable = program;
    QStringList args = arguments;
    if (executable == QLatin1String("limine-list")) {
        executable = QStringLiteral("limine-entry-tool");
        args.prepend(QStringLiteral("--tree"));
    }
    if (executable == QLatin1String("limine-entry-tool")) {
        // Older Limine packages have a bash launcher which asks for sudo even
        // for harmless queries. Query its underlying CLI directly so opening
        // the graphical manager never triggers a second authorization.
        const QString launcher = QStandardPaths::findExecutable(executable);
        QFile file(launcher);
        if (file.open(QIODevice::ReadOnly) && file.read(2) == QByteArrayLiteral("#!")) {
            const QString legacy = QStringLiteral("/usr/lib/limine/limine-entry-tool");
            if (QFileInfo(legacy).isExecutable()) executable = legacy;
        }
    }
    QProcess process;
    process.start(executable, args);
    if (!process.waitForStarted(2000) || !process.waitForFinished(6500)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (process.state() != QProcess::NotRunning) process.kill();
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}

QStringList BootProbe::espMounts()
{
    const auto output = runReadOnly(QStringLiteral("findmnt"),
                                    {QStringLiteral("-rn"), QStringLiteral("-t"),
                                     QStringLiteral("vfat"), QStringLiteral("-o"), QStringLiteral("TARGET")});
    QSet<QString> mounts;
    static const QString efiPartitionType = QStringLiteral("c12a7328-f81f-11d2-ba4b-00a0c93ec93b");
    for (const auto &line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString path = cleanPath(line.trimmed());
        const QString source = runReadOnly(QStringLiteral("findmnt"),
            {QStringLiteral("-rn"), QStringLiteral("-M"), path,
             QStringLiteral("-o"), QStringLiteral("SOURCE")}).trimmed();
        const QString partitionType = runReadOnly(QStringLiteral("lsblk"),
            {QStringLiteral("-dn"), QStringLiteral("-o"), QStringLiteral("PARTTYPE"), source}).trimmed().toLower();
        if (QDir(path + QStringLiteral("/EFI")).exists()
            || QFileInfo::exists(path + QStringLiteral("/limine.conf"))
            || QDir(path + QStringLiteral("/loader")).exists()
            || partitionType == efiPartitionType) mounts.insert(path);
    }
    QStringList result = mounts.values();
    result.sort();
    return result;
}

bool BootProbe::isOnEsp(const QString &path, const QStringList &mounts)
{
    const QString normalized = QFileInfo(path).canonicalFilePath();
    if (normalized.isEmpty()) return false;
    for (const auto &mount : mounts) {
        const QString root = QFileInfo(mount).canonicalFilePath();
        if (!root.isEmpty() && normalized.startsWith(root + QLatin1Char('/'))) return true;
    }
    return false;
}

BootSnapshot BootProbe::inspect()
{
    BootSnapshot state;
    state.uefi = QDir(QStringLiteral("/sys/firmware/efi")).exists();
    const QString efidata = state.uefi
        ? runReadOnly(QStringLiteral("efibootmgr"), {QStringLiteral("-v")}) : QString();
    static const QRegularExpression currentPattern(QStringLiteral("^BootCurrent:\\s*([0-9A-Fa-f]{4})$"));
    static const QRegularExpression nextPattern(QStringLiteral("^BootNext:\\s*([0-9A-Fa-f]{4})$"));
    static const QRegularExpression orderPattern(QStringLiteral("^BootOrder:\\s*(.*)$"));
    static const QRegularExpression entryPattern(QStringLiteral("^Boot([0-9A-Fa-f]{4})(\\*)?\\s+(.+)$"));
    static const QRegularExpression gptPattern(QStringLiteral("HD\\([^,]+,GPT,([a-fA-F0-9-]+)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression filePattern(QStringLiteral("File\\(([^)]+)\\)"), QRegularExpression::CaseInsensitiveOption);
    for (const auto &raw : efidata.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString line = raw.trimmed();
        auto match = currentPattern.match(line);
        if (match.hasMatch()) { state.bootCurrent = match.captured(1).toUpper(); continue; }
        match = nextPattern.match(line);
        if (match.hasMatch()) { state.bootNext = match.captured(1).toUpper(); continue; }
        match = orderPattern.match(line);
        if (match.hasMatch()) {
            for (const auto &id : match.captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts))
                state.bootOrder.append(id.trimmed().toUpper());
            continue;
        }
        match = entryPattern.match(line);
        if (!match.hasMatch()) continue;
        const QString rest = match.captured(3);
        const auto pathMatch = filePattern.match(rest);
        const auto guidMatch = gptPattern.match(rest);
        int labelEnd = rest.size();
        for (const auto &token : {QStringLiteral("HD("), QStringLiteral("File("), QStringLiteral("PciRoot("), QStringLiteral("VenHw(")}) {
            const int pos = rest.indexOf(token, 0, Qt::CaseInsensitive);
            if (pos >= 0) labelEnd = qMin(labelEnd, pos);
        }
        state.firmware.append({match.captured(1).toUpper(), rest.left(labelEnd).trimmed(),
                              pathMatch.captured(1), guidMatch.captured(1).toLower(),
                              !match.captured(2).isEmpty(), false});
    }
    for (auto &entry : state.firmware) entry.current = (entry.number == state.bootCurrent);
    const QString loaderInfo = readLoaderInfo();
    if (!loaderInfo.isEmpty()) {
        state.currentLoader = loaderInfo;
        state.currentEvidence = QStringLiteral("Boot Loader Interface");
    } else if (const auto *entry = state.entry(state.bootCurrent)) {
        state.currentLoader = firmwareLoader(entry->path, entry->label);
        state.currentEvidence = QObject::tr("UEFI BootCurrent (chainloading is possible)");
    }
    if (state.currentLoader.isEmpty()) {
        state.currentLoader = QObject::tr("Unknown");
        state.currentEvidence = QObject::tr("No reliable loader identity was reported");
    }
    if (state.uefi && efidata.isEmpty())
        state.messages.append(QObject::tr("Could not read UEFI variables; firmware management is unavailable."));

    const QStringList mounts = espMounts();
    for (const auto &esp : mounts) {
        QDirIterator iterator(esp + QStringLiteral("/EFI"), QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            const QString lower = path.toLower();
            if (!lower.endsWith(QLatin1String(".efi"))) continue;
            if (lower.contains(QLatin1String("limine")))
                addInstallation(state, LoaderKind::Limine, esp, path, esp + QStringLiteral("/limine.conf"));
            else if (lower.endsWith(QLatin1String("/grubx64.efi")))
                addInstallation(state, LoaderKind::Grub, esp, path, QStringLiteral("/etc/default/grub"));
            else if (lower.endsWith(QLatin1String("/systemd-bootx64.efi")))
                addInstallation(state, LoaderKind::SystemdBoot, esp, path,
                                esp + QStringLiteral("/loader/loader.conf"));
        }
        if (QFileInfo::exists(esp + QStringLiteral("/limine.conf")))
            addInstallation(state, LoaderKind::Limine, esp, {}, esp + QStringLiteral("/limine.conf"));
        if (QFileInfo::exists(esp + QStringLiteral("/loader/loader.conf")))
            addInstallation(state, LoaderKind::SystemdBoot, esp, {}, esp + QStringLiteral("/loader/loader.conf"));
    }
    if (QFileInfo::exists(QStringLiteral("/etc/default/grub"))
        || QFileInfo::exists(QStringLiteral("/boot/grub/grub.cfg")))
        addInstallation(state, LoaderKind::Grub, QString(), QString(), QStringLiteral("/etc/default/grub"));
    if (QFileInfo::exists(QStringLiteral("/etc/default/limine"))
        || QFileInfo::exists(QStringLiteral("/boot/limine.conf")))
        addInstallation(state, LoaderKind::Limine, QString(), QString(), QStringLiteral("/etc/default/limine"));

    for (auto &loader : state.installations) {
        if (loader.esp.isEmpty() || loader.espPartuuid.isEmpty() || loader.binary.isEmpty()) continue;
        QString relative = QDir(loader.esp).relativeFilePath(loader.binary);
        relative.replace(QLatin1Char('/'), QLatin1Char('\\'));
        relative.prepend(QLatin1Char('\\'));
        for (const auto &entry : state.firmware) {
            if (!entry.partitionUuid.isEmpty()
                && entry.partitionUuid == loader.espPartuuid
                && relative.compare(entry.path, Qt::CaseInsensitive) == 0) {
                loader.firmwareRegistered = true;
                loader.current |= entry.current;
            }
        }
    }
    if (!state.uefi) state.messages.append(QObject::tr("This system was not booted in UEFI mode."));
    if (mounts.isEmpty()) state.messages.append(QObject::tr("No mounted EFI system partition was detected."));
    return state;
}
