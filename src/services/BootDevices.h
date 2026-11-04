#pragma once

#include <QList>
#include <QString>

struct BootDevice {
    QString path;
    QString type;
    QString filesystem;
    QString size;
    QString partuuid;
    QString description;

    QString label() const
    {
        return path + QStringLiteral(" · ") + size
            + (filesystem.isEmpty() ? QString() : QStringLiteral(" · ") + filesystem)
            + (description.isEmpty() ? QString() : QStringLiteral(" · ") + description);
    }
};

namespace BootDevices {
QList<BootDevice> discover();
QString fingerprint(const QList<BootDevice> &devices);
const BootDevice *byPath(const QList<BootDevice> &devices, const QString &path);
}
