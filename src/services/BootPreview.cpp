#include "BootPreview.h"
#include "BootBackup.h"
#include "BootDeploy.h"
#include "BootMenus.h"
#include "BootSettings.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QObject>

namespace {
struct FileChange {
    QString before;
    QString after;
    bool existed = false;
    bool removed = false;
};

std::optional<LoaderKind> settingKind(const QString &kind)
{
    for (const auto loader : loaderKinds)
        if (BootSettings::operationKind(loader) == kind) return loader;
    return std::nullopt;
}

bool load(QMap<QString, FileChange> &files, QStringList &order, const QString &path, QString *error)
{
    if (files.contains(path)) return true;
    FileChange change;
    change.existed = QFileInfo::exists(path);
    if (change.existed) {
        change.before = BootMenus::readText(path, error);
        if (!error->isEmpty()) return false;
    }
    change.after = change.before;
    files.insert(path, change);
    order.append(path);
    return true;
}
}

QJsonArray BootPreview::files(const QList<BootOperation> &operations, QString *error)
{
    error->clear();
    QMap<QString, FileChange> files;
    QStringList order;
    QMap<QString, QJsonArray> providerSettings;
    // Same order as execution: restores, menu files, then settings.
    for (const auto &op : operations) {
        if (op.kind != QLatin1String("restore-backup")) continue;
        for (const auto &saved : BootBackup::savedFiles(BootBackup::defaultRoot(), op.target, error)) {
            if (!load(files, order, saved.path, error)) return {};
            files[saved.path].after = QString::fromUtf8(saved.data);
            files[saved.path].removed = !saved.existed;
        }
        if (!error->isEmpty()) return {};
    }
    for (const auto &op : operations) {
        if (op.kind != QLatin1String("menu-write")) continue;
        if (!load(files, order, op.target, error)) return {};
        auto &change = files[op.target];
        const auto request = QJsonDocument::fromJson(op.value.toUtf8()).object();
        if (request.value("remove").toBool()) { change.after.clear(); change.removed = true; continue; }
        const QString format = request.value("format").toString();
        change.after = format == QLatin1String("limine") ? BootMenus::renderLimine(change.after, request.value("entries").toArray(), error)
            : format == QLatin1String("refind") ? BootMenus::renderRefind(change.after, request.value("entries").toArray(), error)
            : BootMenus::renderBls(change.after, request.value("values").toObject(), error);
        if (!error->isEmpty()) return {};
    }
    for (const auto &op : operations) {
        const auto kind = settingKind(op.kind);
        if (!kind) continue;
        const QString key = *kind == LoaderKind::Grub ? op.target : op.target.mid(op.target.lastIndexOf(QLatin1Char('|')) + 1);
        const QString location = *kind == LoaderKind::Grub ? QString() : op.target.left(op.target.lastIndexOf(QLatin1Char('|')));
        if (BootDeploy::directImage(*kind)) {
            providerSettings[location].append(QJsonObject{{"key", key}, {"before", op.expected}, {"after", op.value}});
            continue;
        }
        const QString path = BootSettings::configPath(*kind, location);
        if (!load(files, order, path, error)) return {};
        files[path].after = BootSettings::apply(*kind, files[path].after, key, op.value, error);
        if (!error->isEmpty()) return {};
    }
    QJsonArray result;
    for (const auto &path : order) {
        const auto &change = files[path];
        if (change.before == change.after && change.existed != change.removed) continue;
        result.append(QJsonObject{{"path", path}, {"before", change.before}, {"after", change.after},
                                  {"existed", change.existed}, {"removed", change.removed}});
    }
    for (auto it = providerSettings.begin(); it != providerSettings.end(); ++it)
        result.append(QJsonObject{{"path", it.key()}, {"settings", it.value()}});
    return result;
}
