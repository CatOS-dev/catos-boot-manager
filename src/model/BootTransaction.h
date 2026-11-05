#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

struct BootOperation {
    QString kind;
    QString target;
    QString value;
    QString expected;
    QString description;

    QString key() const
    {
        return kind + QLatin1Char(':') + target
            + (kind == QLatin1String("limine-remove-entry") ? QLatin1Char(':') + value : QString());
    }
    QJsonObject json() const;
    static BootOperation fromJson(const QJsonObject &object);
};

class BootTransaction {
public:
    const QList<BootOperation> &operations() const { return operations_; }
    int count() const { return operations_.size(); }
    void clear() { operations_.clear(); }
    void stage(const BootOperation &operation);
    void remove(const QString &key);
    QByteArray encoded() const;
    static QList<BootOperation> decode(const QByteArray &data, QString *error);

private:
    QList<BootOperation> operations_;
};
