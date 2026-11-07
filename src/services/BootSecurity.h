#pragma once

#include "model/BootTypes.h"
#include <QJsonObject>
#include <functional>

namespace BootSecurity {
struct CommandResult {
    bool completed = false;
    int exitCode = -1;
    QString output;
    QString error;
};
using Reader = std::function<CommandResult(const QString &, const QStringList &)>;

struct Certificate {
    QString subject;
    QString commonName;
    QString issuer;
    QString notAfter;
    QString fingerprint;
};
// Parses `mokutil --list-enrolled` / `--list-new` output.
QList<Certificate> certificates(const QString &mokutilOutput);

// Privileged MOK operations. request: {"operation": "import", "certificate":
// base64 DER, "password": ...} or {"operation": "revoke-import"}. The
// password is passed to mokutil on standard input and never logged.
bool mokAvailable(QString *error);
bool validateMokRequest(const QJsonObject &request, QString *error);
bool executeMokRequest(const QJsonObject &request, QString *error);

CommandResult readCommand(const QString &program, const QStringList &arguments);
void inspect(BootSnapshot &snapshot, const QString &root = QStringLiteral("/"),
             const Reader &reader = readCommand);
}
