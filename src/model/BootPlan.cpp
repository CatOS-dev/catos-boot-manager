#include "BootPlan.h"

#include <QObject>
#include <QSet>

QString BootPlan::firmwareConflict(const QList<BootOperation> &operations, const BootSnapshot &snapshot)
{
    QSet<QString> removed;
    QSet<QString> disabled;
    QString next = snapshot.bootNext;
    QStringList order;
    bool orderChanged = false;
    for (const auto &op : operations) {
        if (op.kind == QLatin1String("firmware-remove")) removed.insert(op.target);
        if (op.kind == QLatin1String("firmware-toggle") && op.value == QLatin1String("0"))
            disabled.insert(op.target);
        if (op.kind == QLatin1String("firmware-next")) next = op.value;
        if (op.kind == QLatin1String("firmware-order")) {
            order = op.value.split(QLatin1Char(','), Qt::SkipEmptyParts);
            orderChanged = true;
        }
    }
    if (!next.isEmpty() && removed.contains(next))
        return QObject::tr("Boot%1 is selected for the next boot and queued for removal. Clear or change the next boot selection.").arg(next);
    if (!next.isEmpty() && disabled.contains(next))
        return QObject::tr("Boot%1 is selected for the next boot and queued to be disabled.").arg(next);
    for (const auto &op : operations) {
        if ((op.kind == QLatin1String("firmware-rename") || op.kind == QLatin1String("firmware-toggle"))
            && removed.contains(op.target))
            return QObject::tr("Boot%1 cannot be edited and removed in the same plan.").arg(op.target);
    }
    if (orderChanged) {
        QSet<QString> surviving(snapshot.bootOrder.begin(), snapshot.bootOrder.end());
        surviving.subtract(removed);
        const QSet<QString> requested(order.begin(), order.end());
        if (order.size() != requested.size() || requested != surviving)
            return QObject::tr("The pending boot order must contain each remaining BootOrder entry exactly once.");
    }
    return {};
}
