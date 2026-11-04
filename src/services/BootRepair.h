#pragma once

#include "model/BootTransaction.h"

#include <QJsonObject>
#include <QString>

namespace BootRepair {
bool supports(const QString &operation);
bool validate(const BootOperation &operation, QString *error);
bool execute(const BootOperation &operation);
QJsonObject arguments(const BootOperation &operation);
}
