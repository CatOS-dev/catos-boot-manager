#include "BootBackup.h"
#include "BootSettings.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>
#include <QObject>
#include <QRegularExpression>
#include <algorithm>

namespace {
constexpr auto privateFile = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
constexpr auto privateDirectory = privateFile | QFileDevice::ExeOwner;
}

bool BootBackup::prepare(const QList<BootOperation> &operations, const BootSnapshot &snapshot,
                         const QString &root, QString *error)
{
    directory_.clear();
    manifest_ = {};
    if (QFileInfo(root).isSymLink() || !QDir().mkpath(root)
        || !QFile::setPermissions(root, privateDirectory)) {
        *error = QObject::tr("Cannot create a private backup root");
        return false;
    }
    directory_ = QDir(root).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkdir(directory_) || !QFile::setPermissions(directory_, privateDirectory)) {
        *error = QObject::tr("Cannot create a private transaction backup");
        return false;
    }
    QSet<QString> paths;
    QJsonArray plan;
    for (const auto &op : operations) {
        plan.append(op.json());
        if (op.kind == QLatin1String("grub-setting")) paths.insert(BootSettings::configPath(LoaderKind::Grub, {}));
        if (op.kind == QLatin1String("menu-write") || op.kind == QLatin1String("rebuild-image")) paths.insert(op.target);
        if (op.kind == QLatin1String("limine-setting") || op.kind == QLatin1String("uki-setting") || op.kind == QLatin1String("efistub-setting"))
            paths.insert(op.target.left(op.target.lastIndexOf('|')));
        if ((op.kind == QLatin1String("uki-setting") || op.kind == QLatin1String("efistub-setting")) && op.target.endsWith(QLatin1String("|cmdline"))) {
            const auto config = op.target.left(op.target.lastIndexOf('|'));
            const auto prefix = config.left(config.size() - QStringLiteral("/etc/catos/firmware-boot.conf").size());
            paths.insert(prefix + QStringLiteral("/etc/kernel/cmdline"));
        }
        if (op.kind == QLatin1String("systemd-setting")) {
            const QString esp = op.target.left(op.target.lastIndexOf(QLatin1Char('|')));
            paths.insert(BootSettings::configPath(LoaderKind::SystemdBoot, esp));
        }
        if (op.kind == QLatin1String("refind-setting"))
            paths.insert(op.target.left(op.target.lastIndexOf(QLatin1Char('|'))));
        if (op.kind == QLatin1String("restore-backup")) {
            QString ignored;
            for (const auto &path : restorePaths(root, op.target, &ignored)) paths.insert(path);
        }
    }
    QJsonArray files;
    QStringList ordered = paths.values();
    ordered.sort();
    for (const auto &path : ordered) {
        const QFileInfo info(path);
        if (!info.isAbsolute() || info.isSymLink() || (info.exists() && !info.isFile())) {
            *error = QObject::tr("Unsafe configuration backup source: %1").arg(path);
            return false;
        }
        QJsonObject entry{{QStringLiteral("path"), path}, {QStringLiteral("existed"), info.exists()}};
        if (info.exists()) {
            QFile source(path);
            if (!source.open(QIODevice::ReadOnly)) { *error = source.errorString(); return false; }
            const QString filename = QString::number(files.size()) + QStringLiteral(".original");
            QSaveFile target(QDir(directory_).filePath(filename));
            if (!target.open(QIODevice::WriteOnly) || !target.setPermissions(privateFile)) {
                *error = target.errorString();
                return false;
            }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            while (!source.atEnd()) {
                const QByteArray chunk = source.read(1024 * 1024);
                if (chunk.isEmpty() && source.error() != QFileDevice::NoError) {
                    *error = source.errorString();
                    return false;
                }
                hash.addData(chunk);
                if (target.write(chunk) != chunk.size()) { *error = target.errorString(); return false; }
            }
            if (!target.commit()) { *error = target.errorString(); return false; }
            entry.insert(QStringLiteral("backup"), filename);
            entry.insert(QStringLiteral("sha256"), QString::fromLatin1(hash.result().toHex()));
            entry.insert(QStringLiteral("permissions"), static_cast<int>(info.permissions()));
        }
        files.append(entry);
    }
    QJsonArray entries;
    for (const auto &entry : snapshot.firmware)
        entries.append(QJsonObject{{QStringLiteral("number"), entry.number}, {QStringLiteral("label"), entry.label},
            {QStringLiteral("path"), entry.path}, {QStringLiteral("partitionUuid"), entry.partitionUuid},
            {QStringLiteral("enabled"), entry.enabled}});
    manifest_ = {{QStringLiteral("version"), 1}, {QStringLiteral("phase"), QStringLiteral("prepared")},
        {QStringLiteral("created"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("operations"), plan}, {QStringLiteral("files"), files},
        {QStringLiteral("firmware"), entries}, {QStringLiteral("bootCurrent"), snapshot.bootCurrent},
        {QStringLiteral("bootNext"), snapshot.bootNext},
        {QStringLiteral("bootOrder"), QJsonArray::fromStringList(snapshot.bootOrder)},
        {QStringLiteral("scope"), QStringLiteral("GUI-edited configuration files and observed firmware state; generator outputs, kernels, EFI binaries and raw NVRAM variables are not backed up")},
        {QStringLiteral("results"), QJsonArray{}}};
    return save(error);
}

bool BootBackup::save(QString *error)
{
    QSaveFile file(QDir(directory_).filePath(QStringLiteral("manifest.json")));
    if (directory_.isEmpty() || !file.open(QIODevice::WriteOnly) || !file.setPermissions(privateFile)) {
        *error = QObject::tr("Cannot write transaction manifest");
        return false;
    }
    const QByteArray contents = QJsonDocument(manifest_).toJson();
    if (file.write(contents) != contents.size() || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool BootBackup::record(const QString &action, bool success, QString *error)
{
    QJsonArray results = manifest_.value(QStringLiteral("results")).toArray();
    results.append(QJsonObject{{QStringLiteral("action"), action}, {QStringLiteral("success"), success},
        {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
    manifest_.insert(QStringLiteral("results"), results);
    manifest_.insert(QStringLiteral("phase"), success ? QStringLiteral("applying") : QStringLiteral("incomplete"));
    return save(error);
}

bool BootBackup::finish(bool success, QString *error)
{
    manifest_.insert(QStringLiteral("phase"), success ? QStringLiteral("completed") : QStringLiteral("incomplete"));
    return save(error);
}

namespace {
QJsonObject readManifest(const QString &root, const QString &id, QByteArray *raw, QString *error)
{
    static const QRegularExpression valid(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    const QString directory = QDir(root).filePath(id);
    if (!valid.match(id).hasMatch() || QFileInfo(directory).isSymLink() || !QFileInfo(directory).isDir()) {
        *error = QObject::tr("The selected backup is not available.");
        return {};
    }
    QFile file(QDir(directory).filePath(QStringLiteral("manifest.json")));
    if (QFileInfo(file.fileName()).isSymLink() || !file.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("The selected backup is not available.");
        return {};
    }
    *raw = file.read(4 * 1024 * 1024);
    const auto document = QJsonDocument::fromJson(*raw);
    if (!document.isObject() || document.object().value(QStringLiteral("version")).toInt() != 1) {
        *error = QObject::tr("The selected backup is damaged.");
        return {};
    }
    error->clear();
    return document.object();
}

QString digest(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
}

QJsonArray BootBackup::list(const QString &root, int limit)
{
    QList<QPair<QString, QJsonObject>> found;
    for (const auto &id : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QByteArray raw;
        QString error;
        const auto manifest = readManifest(root, id, &raw, &error);
        if (!error.isEmpty()) continue;
        QStringList descriptions;
        for (const auto &value : manifest.value(QStringLiteral("operations")).toArray())
            descriptions.append(value.toObject().value(QStringLiteral("description")).toString());
        QJsonArray files;
        for (const auto &value : manifest.value(QStringLiteral("files")).toArray()) {
            const auto file = value.toObject();
            files.append(QJsonObject{{QStringLiteral("path"), file.value(QStringLiteral("path"))},
                                     {QStringLiteral("existed"), file.value(QStringLiteral("existed"))}});
        }
        found.append({manifest.value(QStringLiteral("created")).toString(), QJsonObject{
            {QStringLiteral("id"), id}, {QStringLiteral("created"), manifest.value(QStringLiteral("created"))},
            {QStringLiteral("phase"), manifest.value(QStringLiteral("phase"))},
            {QStringLiteral("operations"), QJsonArray::fromStringList(descriptions)},
            {QStringLiteral("files"), files}, {QStringLiteral("bootOrder"), manifest.value(QStringLiteral("bootOrder"))},
            {QStringLiteral("bootNext"), manifest.value(QStringLiteral("bootNext"))},
            {QStringLiteral("digest"), digest(raw)}}});
    }
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QJsonArray result;
    for (const auto &item : found) {
        if (result.size() >= limit) break;
        result.append(item.second);
    }
    return result;
}

QStringList BootBackup::restorePaths(const QString &root, const QString &id, QString *error)
{
    QByteArray raw;
    const auto manifest = readManifest(root, id, &raw, error);
    if (!error->isEmpty()) return {};
    QStringList paths;
    for (const auto &value : manifest.value(QStringLiteral("files")).toArray())
        paths.append(value.toObject().value(QStringLiteral("path")).toString());
    return paths;
}

bool BootBackup::validateRestore(const QString &root, const QString &id, const QString &expected, QString *error)
{
    QByteArray raw;
    const auto manifest = readManifest(root, id, &raw, error);
    if (!error->isEmpty()) return false;
    if (digest(raw) != expected) { *error = QObject::tr("The selected backup changed. Refresh and try again."); return false; }
    const auto files = manifest.value(QStringLiteral("files")).toArray();
    if (files.isEmpty()) { *error = QObject::tr("This backup contains no configuration files."); return false; }
    for (const auto &value : files) {
        const auto file = value.toObject();
        const QString path = file.value(QStringLiteral("path")).toString();
        const QFileInfo target(path);
        if (!target.isAbsolute() || QDir::cleanPath(path) != path || target.isSymLink() || (target.exists() && !target.isFile())) {
            *error = QObject::tr("Unable to restore %1: the destination is not a regular file.").arg(path);
            return false;
        }
        if (!file.value(QStringLiteral("existed")).toBool()) continue;
        const QString name = file.value(QStringLiteral("backup")).toString();
        QFile saved(QDir(QDir(root).filePath(id)).filePath(name));
        if (name.contains(QLatin1Char('/')) || !saved.open(QIODevice::ReadOnly)
            || digest(saved.readAll()) != file.value(QStringLiteral("sha256")).toString()) {
            *error = QObject::tr("The saved copy of %1 is damaged.").arg(path);
            return false;
        }
        if (!target.dir().exists()) {
            *error = QObject::tr("Unable to restore %1: its directory no longer exists.").arg(path);
            return false;
        }
    }
    return true;
}

bool BootBackup::restore(const QString &root, const QString &id, QString *error)
{
    QByteArray raw;
    const auto manifest = readManifest(root, id, &raw, error);
    if (!error->isEmpty()) return false;
    for (const auto &value : manifest.value(QStringLiteral("files")).toArray()) {
        const auto file = value.toObject();
        const QString path = file.value(QStringLiteral("path")).toString();
        if (QFileInfo(path).isSymLink()) { *error = QObject::tr("Unable to restore %1.").arg(path); return false; }
        if (!file.value(QStringLiteral("existed")).toBool()) {
            if (QFileInfo::exists(path) && !QFile::remove(path)) { *error = QObject::tr("Unable to remove %1.").arg(path); return false; }
            continue;
        }
        QFile saved(QDir(QDir(root).filePath(id)).filePath(file.value(QStringLiteral("backup")).toString()));
        QSaveFile target(path);
        if (!saved.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)) {
            *error = QObject::tr("Unable to restore %1.").arg(path); return false;
        }
        const QByteArray data = saved.readAll();
        if (target.write(data) != data.size() || !target.commit()) { *error = QObject::tr("Unable to restore %1.").arg(path); return false; }
        if (file.contains(QStringLiteral("permissions")))
            QFile::setPermissions(path, QFileDevice::Permissions(file.value(QStringLiteral("permissions")).toInt()));
    }
    return true;
}

QList<BootBackup::SavedFile> BootBackup::savedFiles(const QString &root, const QString &id, QString *error)
{
    QByteArray raw;
    const auto manifest = readManifest(root, id, &raw, error);
    if (!error->isEmpty()) return {};
    QList<SavedFile> result;
    for (const auto &value : manifest.value(QStringLiteral("files")).toArray()) {
        const auto file = value.toObject();
        SavedFile saved{file.value(QStringLiteral("path")).toString(), file.value(QStringLiteral("existed")).toBool(), {}};
        if (saved.existed) {
            QFile original(QDir(QDir(root).filePath(id)).filePath(file.value(QStringLiteral("backup")).toString()));
            if (!original.open(QIODevice::ReadOnly)) { *error = QObject::tr("The selected backup is damaged."); return {}; }
            saved.data = original.readAll();
        }
        result.append(saved);
    }
    return result;
}
