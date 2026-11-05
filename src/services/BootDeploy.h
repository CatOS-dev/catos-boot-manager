#pragma once

#include "model/BootTypes.h"
#include <QJsonObject>

namespace BootDeploy {
QString loaderId(LoaderKind kind);
bool directImage(LoaderKind kind);
QString profilePath(const QString &binary);
QJsonObject profile(const QString &path, QString *error);
QString targetRoot(const QJsonObject &data, const QStringList &candidates, QString *error);
bool validateInputs(const QJsonObject &data, const QString &root, QString *error);
bool deploy(const QJsonObject &data, const QString &root, QString *error);
QByteArray firmwareData(const QByteArray &original, const QString &label, const QString &options, QString *error);
bool validateRebuild(const QString &path, const QMap<QString, QString> &values, const BootSnapshot &snapshot, QString *error);
bool rebuild(const QString &path, QString *error);
bool writeSetting(LoaderKind kind, const QString &path, const QString &key, const QString &value, QString *error);
void populate(BootSnapshot &snapshot);
}
