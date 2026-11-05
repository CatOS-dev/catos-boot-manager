#pragma once

#include "model/BootTypes.h"
#include "model/BootTransaction.h"

namespace BootPackages {
struct Plan {
    QStringList required;
    QStringList missing;
    QStringList installed;
    QString error;
};

QStringList installArguments(const QList<BootOperation> &operations, const BootSnapshot &snapshot, QString *error);
void inspect(BootSnapshot &snapshot);
Plan plan(LoaderKind kind, bool snapshots, bool secureBoot, const BootSnapshot &snapshot);
}
