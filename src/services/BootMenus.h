#pragma once

#include "model/BootTypes.h"
#include "model/BootTransaction.h"
#include <QJsonArray>
#include <QJsonObject>

namespace BootMenus {
QString limineGeneratorPath(QString *error);
bool safePath(const QString &path, const QString &base);
QString readText(const QString &path, QString *error);
QString fingerprint(const QString &text);
QJsonObject limine(const QString &content, QString *error);
QJsonObject refind(const QString &path, const QString &esp, QString *error);
QJsonObject bls(const QString &path, QString *error);
QString renderLimine(const QString &content, const QJsonArray &entries, QString *error);
QString setLimineValue(const QString &content, const QString &key, const QString &value, QString *error);
QString renderRefind(const QString &content, const QJsonArray &entries, QString *error);
QString renderBls(const QString &content, const QJsonObject &values, QString *error);
QString refindValue(const QString &path, const QString &key, QString *source, QString *error);
QString setRefindValue(const QString &content, const QString &key, const QString &value);
bool validate(const BootOperation &operation, const BootSnapshot &snapshot, QString *error);
bool execute(const BootOperation &operation, QString *error);
bool save(const QString &path, const QString &content, QString *error);
}
