#pragma once

#include "model/BootTransaction.h"
#include "model/BootTypes.h"
#include <QJsonObject>

class BootBackup {
public:
    bool prepare(const QList<BootOperation> &operations, const BootSnapshot &snapshot,
                 const QString &root, QString *error);
    bool record(const QString &action, bool success, QString *error);
    bool finish(bool success, QString *error);
    QString directory() const { return directory_; }

private:
    bool save(QString *error);
    QString directory_;
    QJsonObject manifest_;
};
