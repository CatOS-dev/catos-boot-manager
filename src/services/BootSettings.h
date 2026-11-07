#pragma once

#include "model/BootTypes.h"

#include <QStringList>

namespace BootSettings {
struct Setting {
    QString key;
    QString title;
    QString description;
};

QList<Setting> settings(LoaderKind loader);
QString configPath(LoaderKind loader, const QString &esp);
QString read(LoaderKind loader, const QString &esp, const QString &key, QString *error);
bool allowed(LoaderKind loader, const QString &key, const QString &value);
// Returns the configuration text with one setting changed (not for UKI or EFI Stub).
QString apply(LoaderKind loader, const QString &text, const QString &key, const QString &value, QString *error);
bool write(LoaderKind loader, const QString &esp, const QString &key,
           const QString &value, QString *error);
bool refindEditable(const QString &path, QString *error);
QString operationKind(LoaderKind kind);
QString operationTarget(const BootInstallation &loader, const QString &key);
}
