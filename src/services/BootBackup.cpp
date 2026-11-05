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
        *error = QStringLiteral("Cannot create a private backup root");
        return false;
    }
    directory_ = QDir(root).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkdir(directory_) || !QFile::setPermissions(directory_, privateDirectory)) {
        *error = QStringLiteral("Cannot create a private transaction backup");
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
    }
    QJsonArray files;
    QStringList ordered = paths.values();
    ordered.sort();
    for (const auto &path : ordered) {
        const QFileInfo info(path);
        if (!info.isAbsolute() || info.isSymLink() || (info.exists() && !info.isFile())) {
            *error = QStringLiteral("Unsafe configuration backup source: ") + path;
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
        *error = QStringLiteral("Cannot write transaction manifest");
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
