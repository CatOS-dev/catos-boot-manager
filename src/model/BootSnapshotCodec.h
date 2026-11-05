#pragma once

#include "BootTypes.h"
#include <QByteArray>

namespace BootSnapshotCodec {
QByteArray encode(const BootSnapshot &snapshot);
bool decode(const QByteArray &data, BootSnapshot *snapshot);
}
