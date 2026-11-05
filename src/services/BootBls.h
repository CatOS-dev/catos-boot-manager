#pragma once
#include "model/BootTypes.h"

#include <QJsonArray>
#include <QString>

namespace BootBls {
QJsonArray entries(const QString &json, const QString &esp, QString *error);
bool validId(const QString &id);
QString fingerprint(const QString &json);
bool defaultTargetMatches(const BootSnapshot &snapshot, const QString &esp, const QString &id,
                          const QString &expectedMenu);
}
