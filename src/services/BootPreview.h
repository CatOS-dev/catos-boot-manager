#pragma once

#include "model/BootTransaction.h"

#include <QJsonArray>

namespace BootPreview {
// Computes the configuration files a transaction would change, without
// writing anything. Each element: {path, before, after, existed, removed}
// for text files, or {path, settings: [{key, before, after}]} for settings
// stored by an external provider (UKI and EFI Stub).
QJsonArray files(const QList<BootOperation> &operations, QString *error);
}
