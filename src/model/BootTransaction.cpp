#include "BootTransaction.h"

#include <QJsonArray>
#include <QJsonDocument>

QJsonObject BootOperation::json() const
{
    return {{QStringLiteral("kind"), kind}, {QStringLiteral("target"), target},
            {QStringLiteral("value"), value}, {QStringLiteral("expected"), expected},
            {QStringLiteral("description"), description}};
}

BootOperation BootOperation::fromJson(const QJsonObject &object)
{
    return {object.value(QStringLiteral("kind")).toString(),
            object.value(QStringLiteral("target")).toString(),
            object.value(QStringLiteral("value")).toString(),
            object.value(QStringLiteral("expected")).toString(),
            object.value(QStringLiteral("description")).toString()};
}

void BootTransaction::stage(const BootOperation &operation)
{
    for (int i = 0; i < operations_.size(); ++i) {
        if (operations_[i].key() != operation.key()) continue;
        if (operation.kind.endsWith(QLatin1String("-setting"))
            && operation.value == operations_[i].expected) {
            operations_.removeAt(i);
        } else {
            auto replacement = operation;
            replacement.expected = operations_[i].expected;
            operations_[i] = replacement;
        }
        return;
    }
    if (operation.kind.endsWith(QLatin1String("-setting"))
        && operation.value == operation.expected) return;
    operations_.append(operation);
}

QByteArray BootTransaction::encoded() const
{
    QJsonArray array;
    for (const auto &item : operations_) array.append(item.json());
    return QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                     {QStringLiteral("operations"), array}})
        .toJson(QJsonDocument::Compact).toBase64();
}

QList<BootOperation> BootTransaction::decode(const QByteArray &data, QString *error)
{
    const auto document = QJsonDocument::fromJson(QByteArray::fromBase64(data));
    const auto root = document.object();
    const auto array = root.value(QStringLiteral("operations")).toArray();
    if (!document.isObject() || root.value(QStringLiteral("version")).toInt() != 1
        || array.isEmpty() || array.size() > 64) {
        if (error) *error = QStringLiteral("Invalid transaction payload");
        return {};
    }
    QList<BootOperation> result;
    for (const auto &value : array) {
        if (!value.isObject()) { if (error) *error = QStringLiteral("Invalid operation"); return {}; }
        const auto operation = BootOperation::fromJson(value.toObject());
        if (operation.kind.isEmpty() || operation.kind.size() > 64
            || operation.target.size() > 1024 || operation.value.size() > 4096
            || operation.expected.size() > 4096 || operation.description.size() > 512) {
            if (error) *error = QStringLiteral("Invalid operation fields");
            return {};
        }
        result.append(operation);
    }
    return result;
}
