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
QString read(LoaderKind loader, const QString &esp, const QString &key);
bool allowed(LoaderKind loader, const QString &key, const QString &value);
bool write(LoaderKind loader, const QString &esp, const QString &key,
           const QString &value, QString *error);
}
