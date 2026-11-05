#pragma once

#include "model/BootTypes.h"
#include <functional>

namespace BootSecurity {
struct CommandResult {
    bool completed = false;
    int exitCode = -1;
    QString output;
    QString error;
};
using Reader = std::function<CommandResult(const QString &, const QStringList &)>;

CommandResult readCommand(const QString &program, const QStringList &arguments);
void inspect(BootSnapshot &snapshot, const QString &root = QStringLiteral("/"),
             const Reader &reader = readCommand);
}
