#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

enum class LoaderKind { Limine, Grub, SystemdBoot };

inline QString loaderName(LoaderKind kind)
{
    switch (kind) {
    case LoaderKind::Limine: return QStringLiteral("Limine");
    case LoaderKind::Grub: return QStringLiteral("GRUB");
    case LoaderKind::SystemdBoot: return QStringLiteral("systemd-boot");
    }
    return {};
}

struct FirmwareEntry {
    QString number;
    QString label;
    QString path;
    QString partitionUuid;
    bool enabled = false;
    bool current = false;

    QString identity() const
    {
        return number + QLatin1Char('|') + label + QLatin1Char('|') + path
            + QLatin1Char('|') + partitionUuid + QLatin1Char('|')
            + (enabled ? QLatin1String("1") : QLatin1String("0"));
    }
};

struct BootInstallation {
    LoaderKind kind = LoaderKind::Limine;
    QString id;
    QString esp;
    QString espPartuuid;
    QString binary;
    QString config;
    bool configPresent = false;
    bool binaryPresent = false;
    bool firmwareRegistered = false;
    bool current = false;
};

struct BootSnapshot {
    bool uefi = false;
    QString bootCurrent;
    QString bootNext;
    QStringList bootOrder;
    QList<FirmwareEntry> firmware;
    QList<BootInstallation> installations;
    QString currentLoader;
    QString currentEvidence;
    QStringList messages;

    QList<BootInstallation> manageableInstallations() const
    {
        QList<BootInstallation> result;
        QSet<QString> globalKinds;
        for (const auto &loader : installations) {
            const QString kind = loaderName(loader.kind);
            if (loader.kind != LoaderKind::SystemdBoot) {
                if (globalKinds.contains(kind)) continue;
                globalKinds.insert(kind);
            }
            result.append(loader);
        }
        return result;
    }

    const FirmwareEntry *entry(const QString &number) const
    {
        for (const auto &item : firmware) if (item.number == number) return &item;
        return nullptr;
    }
};
