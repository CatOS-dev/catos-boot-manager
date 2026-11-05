#pragma once

#include "model/BootTypes.h"

namespace BootDevices {
QList<BootDevice> discover();
QString fingerprint(const QList<BootDevice> &devices);
const BootDevice *byPath(const QList<BootDevice> &devices, const QString &path);
}
