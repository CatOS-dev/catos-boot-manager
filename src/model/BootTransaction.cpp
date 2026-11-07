#include "BootTransaction.h"

#include <QJsonArray>
#include <QJsonDocument>

QJsonObject BootOperation::json() const
{
    return {{QStringLiteral("kind"), kind}, {QStringLiteral("target"), target},
            {QStringLiteral("value"), value}, {QStringLiteral("expected"), expected},
            {QStringLiteral("description"), description}, {QStringLiteral("expectedEntry"), expectedEntry},
            {QStringLiteral("expectedMenu"), expectedMenu}};
}

BootOperation BootOperation::fromJson(const QJsonObject &object)
{
    return {object.value(QStringLiteral("kind")).toString(),
            object.value(QStringLiteral("target")).toString(),
            object.value(QStringLiteral("value")).toString(),
            object.value(QStringLiteral("expected")).toString(),
            object.value(QStringLiteral("description")).toString(),
            object.value(QStringLiteral("expectedEntry")).toString(),
            object.value(QStringLiteral("expectedMenu")).toString()};
}

void BootTransaction::stage(const BootOperation &operation)
{
    if (operation.kind == QLatin1String("menu-write") && operation.value.isEmpty()) { remove(operation.key()); return; }
    for (int i = 0; i < operations_.size(); ++i) {
        if (operations_[i].key() != operation.key()) continue;
        if ((operation.kind.endsWith(QLatin1String("-setting"))
             || operation.kind == QLatin1String("firmware-next")
             || operation.kind == QLatin1String("firmware-order")
             || operation.kind == QLatin1String("firmware-toggle")
             || operation.kind == QLatin1String("firmware-rename"))
            && operation.value == operations_[i].expected) {
            operations_.removeAt(i);
        } else if (operation.kind == QLatin1String("firmware-rename")
                   && operation.value == operations_[i].expected.section(QLatin1Char('|'), 1, 1)) {
            operations_.removeAt(i);
        } else {
            auto replacement = operation;
            replacement.expected = operations_[i].expected;
            operations_[i] = replacement;
        }
        return;
    }
    if ((operation.kind.endsWith(QLatin1String("-setting"))
         || operation.kind == QLatin1String("firmware-next")
         || operation.kind == QLatin1String("firmware-order")
         || operation.kind == QLatin1String("firmware-toggle")
         || operation.kind == QLatin1String("firmware-rename"))
        && operation.value == operation.expected) return;
    if (operation.kind == QLatin1String("firmware-rename")
        && operation.value == operation.expected.section(QLatin1Char('|'), 1, 1)) return;
    operations_.append(operation);
}

void BootTransaction::remove(const QString &key)
{
    for (int i = 0; i < operations_.size(); ++i) {
        if (operations_[i].key() == key) {
            operations_.removeAt(i);
            return;
        }
    }
}

QByteArray BootTransaction::encoded() const
{
    QJsonArray array;
    for (const auto &item : operations_) array.append(item.json());
    return QJsonDocument(QJsonObject{{QStringLiteral("version"), 3},
                                     {QStringLiteral("operations"), array}})
        .toJson(QJsonDocument::Compact).toBase64();
}

QList<BootOperation> BootTransaction::decode(const QByteArray &data, QString *error)
{
    if (data.size() > 16 * 1024 * 1024) { if (error) *error = QStringLiteral("Transaction too large"); return {}; }
    const auto document = QJsonDocument::fromJson(QByteArray::fromBase64(data));
    const auto root = document.object();
    const auto array = root.value(QStringLiteral("operations")).toArray();
    if (!document.isObject() || root.value(QStringLiteral("version")).toInt() != 3
        || array.isEmpty() || array.size() > 64) {
        if (error) *error = QStringLiteral("Invalid transaction payload");
        return {};
    }
    QList<BootOperation> result;
    for (const auto &value : array) {
        if (!value.isObject()) { if (error) *error = QStringLiteral("Invalid operation"); return {}; }
        const auto operation = BootOperation::fromJson(value.toObject());
        if (operation.kind.isEmpty() || operation.kind.size() > 64
            || operation.target.size() > 1024 || operation.value.size() > (operation.kind == QLatin1String("menu-write") ? 2 * 1024 * 1024 : 4096)
            || operation.expected.size() > 4096 || operation.description.size() > 512
            || !value.toObject().value(QStringLiteral("expectedEntry")).isString()
            || operation.expectedEntry.size() > 4096
            || !value.toObject().value(QStringLiteral("expectedMenu")).isString()
            || operation.expectedMenu.size() > 64) {
            if (error) *error = QStringLiteral("Invalid operation fields");
            return {};
        }
        result.append(operation);
    }
    return result;
}
