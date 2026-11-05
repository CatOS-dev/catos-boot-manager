#pragma once

#include "BootProbe.h"
#include <functional>

namespace BootContent {
using Reader = std::function<QString(const QString &, const QStringList &)>;
void populate(BootSnapshot &snapshot, const Reader &reader = BootProbe::runReadOnly,
              const QString &systemRoot = QStringLiteral("/"));
}
