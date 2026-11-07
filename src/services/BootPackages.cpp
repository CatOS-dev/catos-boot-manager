#include "BootPackages.h"
#include "BootProbe.h"

#include <QObject>
#include <QProcess>

void BootPackages::inspect(BootSnapshot &snapshot)
{
    snapshot.installedPackages.clear();
    snapshot.packageStateAvailable = false;
    QProcess process;
    process.start(QStringLiteral("pacman"), {QStringLiteral("-Qq")});
    if (!process.waitForStarted(2000) || !process.waitForFinished(6500)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        BootProbe::stop(process);
        return;
    }
    for (const auto &name : QString::fromUtf8(process.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        snapshot.installedPackages.insert(name.trimmed());
    snapshot.packageStateAvailable = true;
}

BootPackages::Plan BootPackages::plan(LoaderKind kind, bool snapshots, bool secureBoot, const BootSnapshot &snapshot)
{
    Plan result;
    switch (kind) {
    case LoaderKind::Grub: result.required = {QStringLiteral("grub")}; break;
    case LoaderKind::Limine: result.required = {QStringLiteral("limine"), QStringLiteral("limine-tool")}; break;
    case LoaderKind::Refind: result.required = {QStringLiteral("refind")}; break;
    case LoaderKind::SystemdBoot: result.required = {QStringLiteral("catos-systemd-boot-config")}; break;
    case LoaderKind::Uki: result.required = {QStringLiteral("catos-firmware-boot"), QStringLiteral("systemd-ukify")}; break;
    case LoaderKind::EfiStub: result.required = {QStringLiteral("catos-firmware-boot")}; break;
    }
    if (snapshot.uefi) result.required.append(QStringLiteral("efibootmgr"));
    else if (kind != LoaderKind::Grub) {
        result.error = QObject::tr("This boot method requires UEFI.");
        return result;
    }
    if (snapshots) {
        switch (kind) {
        case LoaderKind::Grub:
            result.required << QStringLiteral("grub-btrfs") << QStringLiteral("inotify-tools"); break;
        case LoaderKind::Limine: result.required << QStringLiteral("limine-btrfs"); break;
        case LoaderKind::SystemdBoot: result.required << QStringLiteral("sdboot-btrfs"); break;
        default:
            result.error = QObject::tr("CatOS snapshot boot integration is not available for this method.");
            return result;
        }
    }
    if (secureBoot) {
        if (kind == LoaderKind::Refind || kind == LoaderKind::EfiStub) {
            result.error = QObject::tr("CatOS automatic Secure Boot integration is not available for this method.");
            return result;
        }
        result.required.append(QStringLiteral("catos-secureboot"));
    }
    if (!snapshot.packageStateAvailable) {
        result.error = QObject::tr("Unable to read software package status.");
        return result;
    }
    for (const auto &name : result.required) {
        if (snapshot.installedPackages.contains(name)) result.installed.append(name);
        else result.missing.append(name);
    }
    return result;
}

QStringList BootPackages::installArguments(const QList<BootOperation> &operations, const BootSnapshot &snapshot, QString *error)
{
    error->clear();
    QStringList packages;
    for (const auto &operation : operations) {
        if (operation.kind != QLatin1String("install-packages")) continue;
        bool matched = false;
        for (const auto kind : loaderKinds) {
            if (operation.target != loaderName(kind)) continue;
            const auto required = plan(kind, false, false, snapshot);
            if (!required.error.isEmpty() || operation.value != required.required.join(QLatin1Char(','))) {
                *error = QObject::tr("The software package selection changed. Refresh and try again.");
                return {};
            }
            matched = true;
            for (const auto &package : required.required)
                if (!packages.contains(package)) packages.append(package);
        }
        if (!matched) {
            *error = QObject::tr("Invalid boot loader selection.");
            return {};
        }
    }
    if (packages.isEmpty()) return {};
    return QStringList{QStringLiteral("-S"), QStringLiteral("--needed"), QStringLiteral("--noconfirm"), QStringLiteral("--")} + packages;
}
