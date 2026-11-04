#pragma once

#include "model/BootTypes.h"

namespace BootProbe {
BootSnapshot inspect();
QStringList espMounts();
QString runReadOnly(const QString &program, const QStringList &arguments = {});
bool isOnEsp(const QString &path, const QStringList &mounts);
}
