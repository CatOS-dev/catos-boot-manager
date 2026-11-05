#include "BootPlan.h"

#include <QObject>
#include <QFileInfo>
#include <QSet>

QString BootPlan::firmwareConflict(const QList<BootOperation> &operations, const BootSnapshot &snapshot)
{
    QSet<QString> removed;
    QSet<QString> disabled;
    QString next = snapshot.bootNext;
    QStringList order;
    bool orderChanged = false;
    for (const auto &op : operations) {
        if (op.kind == QLatin1String("systemd-setting") && op.target.endsWith(QLatin1String("|default"))
            && !snapshot.systemdDefaultOverride.isEmpty())
            return QObject::tr("Change the default boot entry in firmware settings.");
        if (!op.expectedEntry.isEmpty()) {
            const QString number = op.expectedEntry.left(4);
            if ((op.kind != QLatin1String("firmware-order") && op.kind != QLatin1String("firmware-next"))
                || (op.kind == QLatin1String("firmware-next") && op.value != number)
                || (op.kind == QLatin1String("firmware-order") && op.value.section(QLatin1Char(','), 0, 0) != number))
                return QObject::tr("The selected firmware entry does not match the requested boot selection.");
            const auto *entry = snapshot.entry(number);
            if (!entry || entry->identity() != op.expectedEntry)
                return QObject::tr("The selected firmware entry changed. Refresh detection and review the target again.");
            int matches = 0;
            for (const auto &installation : snapshot.installations)
                if (installation.firmwareEntries.contains(number) && installation.binaryPresent
                    && !installation.espPartuuid.isEmpty()
                    && !snapshot.efiArchitecture.isEmpty()
                    && installation.architecture == snapshot.efiArchitecture
                    && (installation.imageType == QLatin1String("pe") || installation.imageType == QLatin1String("uki"))
                    && QFileInfo(installation.binary).size() > 0) ++matches;
            if (matches != 1)
                return QObject::tr("Unable to select this boot entry. Refresh and try again.");
            if (snapshot.secureBoot != SecureBootState::Disabled)
                return QObject::tr("Unable to select this boot entry with the current Secure Boot settings.");
        }
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
        if (op.expectedEntry.isEmpty()) continue;
        const QString number = op.expectedEntry.left(4);
        if (removed.contains(number) || disabled.contains(number))
            return QObject::tr("The selected boot instance is also queued for removal or disabling.");
    }
    for (const auto &op : operations) {
        if ((op.kind == QLatin1String("firmware-rename") || op.kind == QLatin1String("firmware-toggle"))
            && removed.contains(op.target))
            return QObject::tr("Boot%1 cannot be edited and removed in the same plan.").arg(op.target);
    }
    if (orderChanged) {
        QSet<QString> surviving(snapshot.bootOrder.begin(), snapshot.bootOrder.end());
        surviving.subtract(removed);
        const QSet<QString> requested(order.begin(), order.end());
        QSet<QString> missing = surviving;
        missing.subtract(requested);
        bool invalid = order.size() != requested.size() || !missing.isEmpty();
        for (const auto &number : requested)
            invalid |= removed.contains(number) || !snapshot.entry(number);
        if (invalid)
            return QObject::tr("The pending boot order must contain each remaining BootOrder entry exactly once.");
    }
    return {};
}
