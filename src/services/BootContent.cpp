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
    QMap<QString, QJsonObject> configurations;
    for (const auto &root : snapshot.repairRoots) {
        const auto path = BootDeploy::configurationPath(root);
        if (!QFileInfo::exists(path)) continue;
        QString error;
        const auto configuration = BootDeploy::profile(path, &error);
        if (!error.isEmpty()) continue;
        configurations.insert(path, configuration);
        for (int i = snapshot.installations.size() - 1; i >= 0; --i)
            if (BootDeploy::directImage(snapshot.installations[i].kind) && snapshot.installations[i].binary.isEmpty()
                && snapshot.installations[i].config == path) snapshot.installations.removeAt(i);
        for (const auto kind : {LoaderKind::Uki, LoaderKind::EfiStub}) {
            BootInstallation installation;
            installation.kind = kind;
            installation.id = BootDeploy::loaderId(kind) + ':' + path;
            installation.esp = configuration.value("esp").toString();
            installation.config = path;
            installation.configPresent = true;
            snapshot.installations.append(installation);
        }
    }
    for (auto &loader : snapshot.installations) {
        loader.settingValues.clear();
        loader.settingFiles.clear();
        loader.settingsEditable = false;
        loader.settingsMessage.clear();
        loader.menuContent.clear();
        loader.menuMessage.clear();
        const auto settings = BootSettings::settings(loader.kind);
        if (BootDeploy::directImage(loader.kind)) {
            QJsonObject configuration;
            for (auto it = configurations.begin(); it != configurations.end(); ++it) {
                const auto destination = it.value().value("esp").toString();
                if (destination == loader.esp || (!loader.binary.isEmpty() && loader.binary.startsWith(destination + '/'))) {
                    if (!loader.config.isEmpty() && loader.config != it.key()) continue;
                    loader.config = it.key(); loader.esp = destination; configuration = it.value(); break;
                }
            }
            if (configuration.isEmpty()) { loader.settingsMessage = QObject::tr("No boot configuration found."); continue; }
            for (const auto &setting : settings) loader.settingValues.insert(setting.key, configuration.value(setting.key).toString());
            loader.settingValues.insert("root", configuration.value("root").toString());
            loader.settingsEditable = true;
            QString error;
            const auto text = BootMenus::readText(loader.config, &error);
            if (!error.isEmpty()) { loader.settingsMessage = error; loader.settingsEditable = false; continue; }
            loader.menuContent = QString::fromUtf8(QJsonDocument(QJsonObject{{"binary", loader.binary},
                {"label", configuration.value("label_prefix")}, {"fingerprint", BootMenus::fingerprint(text + configuration.value("cmdline").toString())}}).toJson(QJsonDocument::Compact));
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
