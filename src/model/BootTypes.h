#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <array>

enum class LoaderKind { Grub, Limine, Refind, SystemdBoot, Uki, EfiStub };

inline constexpr std::array<LoaderKind, 6> loaderKinds{
    LoaderKind::Grub, LoaderKind::Limine, LoaderKind::Refind,
    LoaderKind::SystemdBoot, LoaderKind::Uki, LoaderKind::EfiStub
};

inline QString loaderName(LoaderKind kind)
{
    switch (kind) {
    case LoaderKind::Limine: return QStringLiteral("Limine");
    case LoaderKind::Grub: return QStringLiteral("GRUB");
    case LoaderKind::SystemdBoot: return QStringLiteral("systemd-boot");
    case LoaderKind::Refind: return QStringLiteral("rEFInd");
    case LoaderKind::Uki: return QStringLiteral("UKI");
    case LoaderKind::EfiStub: return QStringLiteral("EFI Stub");
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
    QStringList firmwareEntries;
    bool defaultEntry = false;
    bool nextEntry = false;
    QString evidence;
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
        return installations;
    }

    const FirmwareEntry *entry(const QString &number) const
    {
        for (const auto &item : firmware) if (item.number == number) return &item;
        return nullptr;
    }
};
