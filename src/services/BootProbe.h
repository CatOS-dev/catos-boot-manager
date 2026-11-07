#pragma once

#include "model/BootTypes.h"

#include <functional>

class QProcess;

namespace BootProbe {
// progress, when set, receives a short description of each detection stage.
BootSnapshot inspect(const std::function<void(const QString &)> &progress = {});
QStringList espMounts();
QString runReadOnly(const QString &program, const QStringList &arguments = {});
// Ends a timed-out read-only command: SIGTERM first so it can clean up,
// then SIGKILL if it does not exit within the grace period.
void stop(QProcess &process);
bool isOnEsp(const QString &path, const QStringList &mounts);
}
