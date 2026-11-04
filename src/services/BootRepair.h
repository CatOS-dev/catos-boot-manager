#pragma once

#include "model/BootTransaction.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace BootRepair {
bool supports(const QString &operation);
bool validate(const BootOperation &operation, QString *error);
bool execute(const BootOperation &operation, QString *error);
QJsonObject arguments(const BootOperation &operation);
QStringList preview(const BootOperation &operation);
QStringList mountedTargets();
}
