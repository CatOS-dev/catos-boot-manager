#pragma once

#include "model/BootTransaction.h"

class QWidget;

enum class ApplyResult { Cancelled, Applied, Failed };

namespace ApplyDialog {
ApplyResult execute(QWidget *parent, const BootTransaction &transaction);
}
