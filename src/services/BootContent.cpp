#include "BootContent.h"
#include "BootSettings.h"
#include "BootBls.h"
#include "BootMenus.h"
#include "BootDeploy.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

void BootContent::populate(BootSnapshot &snapshot, const Reader &reader, const QString &systemRoot)
{
    for (auto &loader : snapshot.installations) {
        loader.settingValues.clear();
        loader.settingFiles.clear();
        loader.settingsEditable = false;
        loader.settingsMessage.clear();
        loader.menuContent.clear();
        loader.menuMessage.clear();
        const auto settings = BootSettings::settings(loader.kind);
        if (BootDeploy::directImage(loader.kind)) {
            loader.config = BootDeploy::profilePath(loader.binary);
            const auto profile = BootDeploy::profile(loader.config, &loader.settingsMessage);
            for (const auto &setting : settings) loader.settingValues.insert(setting.key, profile.value(setting.key).toString());
            if (!profile.isEmpty()) {
                QString rootError;
                const auto root = BootDeploy::targetRoot(profile, snapshot.repairRoots, &rootError);
                if (rootError.isEmpty()) loader.settingValues.insert("root", root);
            }
            loader.settingsEditable = loader.settingsMessage.isEmpty();
            loader.menuContent = QString::fromUtf8(QJsonDocument(QJsonObject{{"binary", loader.binary}, {"label", profile.value("label")}, {"fingerprint", BootMenus::fingerprint(QFileInfo::exists(loader.config) ? BootMenus::readText(loader.config, &loader.settingsMessage) : QString())}}).toJson(QJsonDocument::Compact));
            continue;
        }
        if (loader.kind == LoaderKind::Limine) {
            if (loader.esp.isEmpty()) { loader.settingsMessage = QObject::tr("Select an installed Limine menu."); continue; }
            const QString text = BootMenus::readText(loader.config, &loader.menuMessage);
            QJsonObject document;
            if (loader.menuMessage.isEmpty()) document = BootMenus::limine(text, &loader.menuMessage);
            document.insert("source", loader.config);
            document.insert("fingerprint", BootMenus::fingerprint(text));
            loader.menuContent = QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact));
            for (const auto &setting : settings) loader.settingValues.insert(setting.key, document.value("globals").toObject().value(setting.key).toString());
            loader.settingsMessage = loader.menuMessage;
            loader.settingsEditable = loader.menuMessage.isEmpty();
            continue;
        }
        if (loader.kind == LoaderKind::Refind) {
            const auto document = BootMenus::refind(loader.config, loader.esp, &loader.menuMessage);
            loader.menuContent = QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact));
            for (const auto &setting : settings) {
                loader.settingValues.insert(setting.key, document.value("globals").toObject().value(setting.key).toString());
                loader.settingFiles.insert(setting.key, document.value("settingSources").toObject().value(setting.key).toString(loader.config));
            }
            loader.settingsMessage = loader.menuMessage;
            loader.settingsEditable = loader.menuMessage.isEmpty();
            continue;
        }
        const QString location = loader.kind == LoaderKind::SystemdBoot ? loader.esp : loader.config;
        const QString config = BootSettings::configPath(loader.kind, location);
        const bool bound = !config.isEmpty() && loader.config == config
            && (loader.kind == LoaderKind::SystemdBoot || loader.esp.isEmpty());
        if (!bound) loader.settingsMessage = QObject::tr("No configuration found for this installation.");
        else {
            for (const auto &setting : settings) {
                const auto value = loader.kind == LoaderKind::SystemdBoot && !loader.configPresent && !QFileInfo::exists(config)
                    ? QString() : BootSettings::read(loader.kind, location, setting.key, &loader.settingsMessage);
                if (!loader.settingsMessage.isEmpty()) { loader.settingValues.clear(); break; }
                loader.settingValues.insert(setting.key, value);
            }
            loader.settingsEditable = loader.settingsMessage.isEmpty();
        }
        if (loader.kind == LoaderKind::SystemdBoot) {
            const QString output = reader(QStringLiteral("bootctl"),
                {QStringLiteral("--esp-path=") + loader.esp, QStringLiteral("--boot-path=") + loader.esp,
                 QStringLiteral("--no-pager"), QStringLiteral("--json=short"), QStringLiteral("list")});
            auto entries = BootBls::entries(output, loader.esp, &loader.menuMessage);
            for (int i = 0; i < entries.size(); ++i) {
                auto entry = entries[i].toObject();
                if (entry.value("type").toString() == QLatin1String("type1")) {
                    QString error;
                    const auto source = BootMenus::bls(entry.value("path").toString(), &error);
                    entry.insert("configuration", source);
                    if (!error.isEmpty()) entry.insert("error", error);
                    entries[i] = entry;
                }
            }
            loader.menuContent = loader.menuMessage.isEmpty() ? QString::fromUtf8(QJsonDocument(entries).toJson(QJsonDocument::Compact)) : QString();
        } else if (loader.kind == LoaderKind::Grub) {
            const QString path = loader.esp.isEmpty() ? QDir(systemRoot).filePath(QStringLiteral("boot/grub/grub.cfg")) : loader.config;
            if (!path.isEmpty()) loader.menuContent = BootMenus::readText(path, &loader.menuMessage);
        }
    }
}
