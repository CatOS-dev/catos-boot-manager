#pragma once

#include "model/BootTransaction.h"

class QWidget;
class BootSession;

enum class ApplyResult { Cancelled, Applied, Failed };

namespace ApplyDialog {
ApplyResult execute(QWidget *parent, BootSession &session, const BootTransaction &transaction, bool review = true);
}
