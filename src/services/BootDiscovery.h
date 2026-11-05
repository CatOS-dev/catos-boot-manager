#pragma once

#include "model/BootTypes.h"
#include <functional>

namespace BootDiscovery {
struct Esp {
    QString mount;
    QString partitionUuid;
};

void populate(BootSnapshot &snapshot, const QList<Esp> &partitions, const QString &systemRoot,
              const std::function<QString(const QString &)> &identifyKernel);
}
