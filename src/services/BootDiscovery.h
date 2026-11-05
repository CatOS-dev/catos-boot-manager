#pragma once

#include "model/BootTypes.h"
#include <functional>

namespace BootDiscovery {
struct Esp {
    QString mount;
    QString partitionUuid;
};
struct Image {
    QString type;
    QString architecture;
};

QString architectureFromObjdump(const QString &output);

void populate(BootSnapshot &snapshot, const QList<Esp> &partitions, const QString &systemRoot,
              const std::function<Image(const QString &)> &identifyKernel);
}
