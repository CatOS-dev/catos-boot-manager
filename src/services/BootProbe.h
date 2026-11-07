#pragma once

#include "model/BootTypes.h"

class QProcess;

namespace BootProbe {
BootSnapshot inspect();
QStringList espMounts();
QString runReadOnly(const QString &program, const QStringList &arguments = {});
// Ends a timed-out read-only command: SIGTERM first so it can clean up,
// then SIGKILL if it does not exit within the grace period.
void stop(QProcess &process);
bool isOnEsp(const QString &path, const QStringList &mounts);
}
