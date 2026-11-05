#include "BootBls.h"

#include <QDir>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QSet>

bool BootBls::validId(const QString &id)
{
    return QRegularExpression(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.+@#-]{0,254}$")).match(id).hasMatch();
}

QJsonArray BootBls::entries(const QString &json, const QString &esp, QString *error)
{
    error->clear();
    const auto document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isArray()) {
        *error = QObject::tr("bootctl did not return a structured entry list. Check the selected ESP and refresh detection.");
        return {};
    }
    QJsonArray result;
    QSet<QString> identities;
    for (const auto &value : document.array()) {
        const auto entry = value.toObject();
        const QString type = entry.value(QStringLiteral("type")).toString();
        if (type != QLatin1String("type1") && type != QLatin1String("type2")) continue;
        if (QDir::cleanPath(entry.value(QStringLiteral("root")).toString()) != QDir::cleanPath(esp)) continue;
        const QString id = entry.value(QStringLiteral("id")).toString();
        const QString path = QDir::cleanPath(entry.value(QStringLiteral("path")).toString());
        if (!validId(id) || !path.startsWith(QDir::cleanPath(esp) + QLatin1Char('/')) || identities.contains(id)) {
            *error = QObject::tr("The structured entry list contains an invalid or duplicate identity.");
            return {};
        }
        identities.insert(id);
        result.append(entry);
    }
    return result;
}

QString BootBls::fingerprint(const QString &json)
{
    return QString::fromLatin1(QCryptographicHash::hash(json.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool BootBls::defaultTargetMatches(const BootSnapshot &snapshot, const QString &esp,
                                  const QString &id, const QString &expectedMenu)
{
    if (!validId(id)) return false;
    for (const auto &installation : snapshot.installations) {
        if (installation.kind != LoaderKind::SystemdBoot || installation.esp != esp
            || !installation.menuMessage.isEmpty() || installation.menuContent.isEmpty()
            || fingerprint(installation.menuContent) != expectedMenu) continue;
        QString error;
        for (const auto &value : entries(installation.menuContent, esp, &error))
            if (value.toObject().value(QStringLiteral("id")).toString() == id) return true;
    }
    return false;
}
