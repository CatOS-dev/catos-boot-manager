#pragma once

#include "model/BootTypes.h"
#include <QJsonObject>

namespace BootDeploy {
QString loaderId(LoaderKind kind);
bool directImage(LoaderKind kind);
QString configurationPath(const QString &root);
QJsonObject profile(const QString &path, QString *error);
bool validateInputs(const QJsonObject &data, const QString &root, QString *error);
// Creates a firmware boot entry from efibootmgr arguments (without a create
// flag) and appends it to the end of BootOrder, leaving the default unchanged.
bool createFirmwareEntry(const QStringList &arguments, QString *error);
bool deploy(const QJsonObject &data, const QString &root, QString *error);
QByteArray firmwareData(const QByteArray &original, const QString &label, const QString &options, QString *error);
bool validateRebuild(LoaderKind kind, const QString &path, const QMap<QString, QString> &values, const BootSnapshot &snapshot, QString *error);
bool rebuild(LoaderKind kind, const QString &path, QString *error);
bool writeSetting(LoaderKind kind, const QString &path, const QString &key, const QString &value, QString *error);
void populate(BootSnapshot &snapshot);
}
