#pragma once

#include "model/BootTypes.h"

namespace BootPackages {
struct Plan {
    QStringList required;
    QStringList missing;
    QStringList installed;
    QString error;
};

void inspect(BootSnapshot &snapshot);
Plan plan(LoaderKind kind, bool snapshots, bool secureBoot, const BootSnapshot &snapshot);
}
