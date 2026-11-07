#pragma once

#include <QString>
#include <QStringList>

namespace BootDiff {
// Line-based unified diff. Each returned line starts with ' ', '-', '+' or
// "@@" for a hunk header. Returns an empty list when the texts are equal.
QStringList unified(const QString &before, const QString &after, int context = 3);
}
