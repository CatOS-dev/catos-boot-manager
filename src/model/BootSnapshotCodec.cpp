#include "BootSnapshotCodec.h"

#include <QDataStream>
#include <QIODevice>

static QDataStream &operator<<(QDataStream &s, const FirmwareEntry &v)
{
    return s << v.number << v.label << v.path << v.partitionUuid << v.enabled << v.current;
}

static QDataStream &operator>>(QDataStream &s, FirmwareEntry &v)
{
    return s >> v.number >> v.label >> v.path >> v.partitionUuid >> v.enabled >> v.current;
}

static QDataStream &operator<<(QDataStream &s, const BootDevice &v)
{
    return s << v.path << v.type << v.filesystem << v.size << v.partuuid << v.description;
}

static QDataStream &operator>>(QDataStream &s, BootDevice &v)
{
    return s >> v.path >> v.type >> v.filesystem >> v.size >> v.partuuid >> v.description;
}

static QDataStream &operator<<(QDataStream &s, const BootInstallation &v)
{
    return s << v.kind << v.id << v.esp << v.espPartuuid << v.binary << v.config
        << v.configPresent << v.binaryPresent << v.firmwareRegistered << v.current
        << v.firmwareEntries << v.defaultEntry << v.nextEntry << v.evidence
        << v.imageType << v.architecture << v.signature << v.signatureDetails
        << v.settingValues << v.settingFiles << v.settingsEditable << v.settingsMessage << v.menuContent
        << v.menuMessage << v.issues << v.espBytesAvailable << v.binaryBytes;
}

static QDataStream &operator>>(QDataStream &s, BootInstallation &v)
{
    return s >> v.kind >> v.id >> v.esp >> v.espPartuuid >> v.binary >> v.config
        >> v.configPresent >> v.binaryPresent >> v.firmwareRegistered >> v.current
        >> v.firmwareEntries >> v.defaultEntry >> v.nextEntry >> v.evidence
        >> v.imageType >> v.architecture >> v.signature >> v.signatureDetails
        >> v.settingValues >> v.settingFiles >> v.settingsEditable >> v.settingsMessage >> v.menuContent
        >> v.menuMessage >> v.issues >> v.espBytesAvailable >> v.binaryBytes;
}
QByteArray BootSnapshotCodec::encode(const BootSnapshot &v)
{
    QByteArray data;
    QDataStream s(&data, QIODevice::WriteOnly);
    s.setVersion(QDataStream::Qt_6_0);
    s << quint32(4) << v.uefi << v.efiArchitecture << v.bootCurrent << v.bootNext
        << v.bootOrder << v.firmware << v.installations << v.currentLoader
        << v.currentEvidence << v.messages << v.secureBoot << v.installedPackages
        << v.packageStateAvailable << v.setupMode << v.moduleSignatureEnforcement
        << v.enrolledMok << v.pendingMok << v.securityMessages << v.systemdDefaultOverride
        << v.devices << v.repairRoots << v.espMounts << v.mountSources << v.kernels << v.backups;
    return data.toBase64();
}

bool BootSnapshotCodec::decode(const QByteArray &data, BootSnapshot *snapshot)
{
    QDataStream s(QByteArray::fromBase64(data));
    s.setVersion(QDataStream::Qt_6_0);
    quint32 version = 0;
    BootSnapshot v;
    s >> version;
    if (version != 4) return false;
    s >> v.uefi >> v.efiArchitecture >> v.bootCurrent >> v.bootNext
        >> v.bootOrder >> v.firmware >> v.installations >> v.currentLoader
        >> v.currentEvidence >> v.messages >> v.secureBoot >> v.installedPackages
        >> v.packageStateAvailable >> v.setupMode >> v.moduleSignatureEnforcement
        >> v.enrolledMok >> v.pendingMok >> v.securityMessages >> v.systemdDefaultOverride
        >> v.devices >> v.repairRoots >> v.espMounts >> v.mountSources >> v.kernels >> v.backups;
    if (s.status() != QDataStream::Ok || !s.atEnd()) return false;
    *snapshot = std::move(v);
    return true;
}
