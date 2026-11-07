#include "BootDiff.h"

#include <QList>
#include <QVector>

namespace {
QStringList lines(const QString &text)
{
    if (text.isEmpty()) return {};
    QStringList result = text.split(QLatin1Char('\n'));
    if (text.endsWith(QLatin1Char('\n'))) result.removeLast();
    return result;
}

struct Edit {
    QChar kind;
    QString text;
    int oldLine;
    int newLine;
};
}

QStringList BootDiff::unified(const QString &before, const QString &after, int context)
{
    if (before == after) return {};
    const QStringList a = lines(before);
    const QStringList b = lines(after);
    int prefix = 0;
    while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix]) ++prefix;
    int suffix = 0;
    while (suffix < a.size() - prefix && suffix < b.size() - prefix
           && a[a.size() - 1 - suffix] == b[b.size() - 1 - suffix]) ++suffix;
    const int n = a.size() - prefix - suffix;
    const int m = b.size() - prefix - suffix;
    QList<Edit> edits;
    for (int i = 0; i < prefix; ++i) edits.append({QLatin1Char(' '), a[i], i, i});
    // Longest common subsequence over the changed middle. Very large changes
    // fall back to replacing the whole middle section.
    if (qint64(n) * m <= 4'000'000) {
        QVector<int> table((n + 1) * (m + 1), 0);
        const auto at = [&](int i, int j) -> int & { return table[i * (m + 1) + j]; };
        for (int i = n - 1; i >= 0; --i)
            for (int j = m - 1; j >= 0; --j)
                at(i, j) = a[prefix + i] == b[prefix + j] ? at(i + 1, j + 1) + 1 : qMax(at(i + 1, j), at(i, j + 1));
        int i = 0, j = 0;
        while (i < n || j < m) {
            if (i < n && j < m && a[prefix + i] == b[prefix + j]) {
                edits.append({QLatin1Char(' '), a[prefix + i], prefix + i, prefix + j}); ++i; ++j;
            } else if (i < n && (j == m || at(i + 1, j) >= at(i, j + 1))) {
                edits.append({QLatin1Char('-'), a[prefix + i], prefix + i, prefix + j}); ++i;
            } else {
                edits.append({QLatin1Char('+'), b[prefix + j], prefix + i, prefix + j}); ++j;
            }
        }
    } else {
        for (int i = 0; i < n; ++i) edits.append({QLatin1Char('-'), a[prefix + i], prefix + i, prefix + m});
        for (int j = 0; j < m; ++j) edits.append({QLatin1Char('+'), b[prefix + j], prefix + n, prefix + j});
    }
    for (int i = 0; i < suffix; ++i)
        edits.append({QLatin1Char(' '), a[a.size() - suffix + i], a.size() - suffix + i, b.size() - suffix + i});

    QStringList result;
    int index = 0;
    while (index < edits.size()) {
        while (index < edits.size() && edits[index].kind == QLatin1Char(' ')) ++index;
        if (index == edits.size()) break;
        int start = qMax(0, index - context);
        int end = index;
        // Extend the hunk while changes are separated by at most 2 * context lines.
        while (end < edits.size()) {
            int next = end;
            while (next < edits.size() && edits[next].kind != QLatin1Char(' ')) ++next;
            int gap = next;
            while (gap < edits.size() && edits[gap].kind == QLatin1Char(' ')) ++gap;
            if (gap < edits.size() && gap - next <= 2 * context) { end = gap; continue; }
            end = qMin(int(edits.size()), next + context);
            break;
        }
        int oldCount = 0, newCount = 0;
        for (int k = start; k < end; ++k) {
            if (edits[k].kind != QLatin1Char('+')) ++oldCount;
            if (edits[k].kind != QLatin1Char('-')) ++newCount;
        }
        result.append(QStringLiteral("@@ -%1,%2 +%3,%4 @@").arg(edits[start].oldLine + (oldCount ? 1 : 0)).arg(oldCount)
                          .arg(edits[start].newLine + (newCount ? 1 : 0)).arg(newCount));
        for (int k = start; k < end; ++k) result.append(edits[k].kind + edits[k].text);
        index = end;
    }
    return result;
}
