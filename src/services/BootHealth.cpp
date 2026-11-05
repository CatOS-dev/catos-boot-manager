#include "BootHealth.h"

#include <QFileInfo>
#include <QObject>
#include <QStorageInfo>

void BootHealth::inspect(BootSnapshot &snapshot)
{
    for (auto &installation : snapshot.installations) {
        installation.issues.clear();
        installation.espBytesAvailable = -1;
        if (!installation.binary.isEmpty()) {
            if (!installation.binaryPresent || !QFileInfo(installation.binary).isFile())
                installation.issues.append(QObject::tr("The registered or discovered EFI program is missing."));
            else {
                if (QFileInfo(installation.binary).size() == 0)
                    installation.issues.append(QObject::tr("The EFI program file is empty."));
                if (installation.imageType != QLatin1String("pe") && installation.imageType != QLatin1String("uki"))
                    installation.issues.append(QObject::tr("Unknown EFI program format."));
                if (snapshot.uefi && (snapshot.efiArchitecture.isEmpty() || installation.architecture.isEmpty()))
                    installation.issues.append(QObject::tr("The program or firmware architecture is unknown."));
                else if (snapshot.uefi && installation.architecture != snapshot.efiArchitecture)
                    installation.issues.append(QObject::tr("The program architecture does not match the running firmware."));
                if (snapshot.secureBoot == SecureBootState::Enabled && installation.signature == SignatureState::Absent)
                    installation.issues.append(QObject::tr("Secure Boot is enabled but this EFI program has no signature table."));
            }
        }
        if (!installation.config.isEmpty() && !QFileInfo(installation.config).isFile())
            installation.issues.append(QObject::tr("The expected configuration file is missing."));
        if (!installation.esp.isEmpty()) {
            const QStorageInfo storage(installation.esp);
            if (storage.isValid() && storage.isReady()) {
                installation.espBytesAvailable = storage.bytesAvailable();
                if (storage.isReadOnly()) installation.issues.append(QObject::tr("The ESP filesystem is read-only."));
                if (storage.bytesAvailable() == 0) installation.issues.append(QObject::tr("No writable space remains on the ESP."));
            } else installation.issues.append(QObject::tr("The ESP filesystem information could not be read."));
        }
    }
    const auto *next = snapshot.entry(snapshot.bootNext);
    if (!snapshot.bootNext.isEmpty() && (!next || !next->enabled))
        snapshot.messages.append(QObject::tr("BootNext refers to a missing or disabled firmware entry."));
    for (const auto &number : snapshot.bootOrder)
        if (!snapshot.entry(number)) snapshot.messages.append(QObject::tr("BootOrder references missing firmware entry Boot%1.").arg(number));
}
