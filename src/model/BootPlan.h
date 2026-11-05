#pragma once

#include "BootTransaction.h"
#include "BootTypes.h"

namespace BootPlan {
QString firmwareConflict(const QList<BootOperation> &operations, const BootSnapshot &snapshot);
}
