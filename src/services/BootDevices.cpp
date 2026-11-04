#include "BootDevices.h"
#include "BootProbe.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
void appendDevices(const QJsonArray &items, QList<BootDevice> &devices)
{
    for (const auto &item : items) {
        const auto device = item.toObject();
        const auto get = [&device](const char *key) {
            return device.value(QLatin1String(key)).toString();
        };
        const BootDevice entry{get("name"), get("type"), get("fstype"), get("size"),
                               get("partuuid"), get("model").trimmed()};
        if (!entry.path.isEmpty() && (entry.type == QLatin1String("disk")
            || entry.type == QLatin1String("part") || entry.type == QLatin1String("crypt")))
            devices.append(entry);
        appendDevices(device.value(QStringLiteral("children")).toArray(), devices);
    }
}
}

QList<BootDevice> BootDevices::discover()
{
    const QString output = BootProbe::runReadOnly(QStringLiteral("lsblk"),
        {QStringLiteral("--json"), QStringLiteral("-p"), QStringLiteral("-o"),
         QStringLiteral("NAME,TYPE,FSTYPE,SIZE,PARTUUID,MODEL")});
    const auto document = QJsonDocument::fromJson(output.toUtf8());
    QList<BootDevice> result;
    appendDevices(document.object().value(QStringLiteral("blockdevices")).toArray(), result);
    return result;
}

QString BootDevices::fingerprint(const QList<BootDevice> &devices)
{
    QStringList records;
    for (const auto &entry : devices)
        records.append(entry.path + QLatin1Char('|') + entry.type + QLatin1Char('|')
            + entry.filesystem + QLatin1Char('|') + entry.partuuid);
    records.sort();
    return QString::fromLatin1(QCryptographicHash::hash(
        records.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256).toHex());
}

const BootDevice *BootDevices::byPath(const QList<BootDevice> &devices, const QString &path)
{
    for (const auto &entry : devices) if (entry.path == path) return &entry;
    return nullptr;
}
