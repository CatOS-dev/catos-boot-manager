#pragma once

#include "model/BootTransaction.h"
#include "model/BootTypes.h"
#include <QJsonArray>
#include <QJsonObject>

class BootBackup {
public:
    static QString defaultRoot() { return QStringLiteral("/var/lib/catos-boot-manager/backups"); }
    // Summaries of the newest transaction backups, newest first.
    static QJsonArray list(const QString &root, int limit = 50);
    // Configuration files a restore would write; empty if the backup is unusable.
    static QStringList restorePaths(const QString &root, const QString &id, QString *error);
    // Checks the backup still matches what was reviewed (manifest digest) and
    // that every saved original is intact.
    static bool validateRestore(const QString &root, const QString &id, const QString &digest, QString *error);
    // Writes the saved originals back and removes files that did not exist.
    static bool restore(const QString &root, const QString &id, QString *error);

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
