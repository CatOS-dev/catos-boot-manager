#include "BootContent.h"
#include "BootSettings.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

namespace {
QString readText(const QString &path)
{
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}
}

void BootContent::populate(BootSnapshot &snapshot, const Reader &reader, const QString &systemRoot)
{
    for (auto &loader : snapshot.installations) {
        loader.settingValues.clear();
        loader.settingsEditable = false;
        loader.settingsMessage.clear();
        loader.menuContent.clear();
        loader.menuMessage.clear();
        const QString location = loader.kind == LoaderKind::Refind ? loader.config : loader.esp;
        const QString config = BootSettings::configPath(loader.kind, location);
        const auto settings = BootSettings::settings(loader.kind);
        const bool bound = !config.isEmpty() && loader.config == config
            && (loader.kind == LoaderKind::SystemdBoot || loader.kind == LoaderKind::Refind || loader.esp.isEmpty());
        QString refindError;
        const bool refindReady = loader.kind != LoaderKind::Refind || !bound
            || BootSettings::refindEditable(config, &refindError);
        if (settings.isEmpty()) loader.settingsMessage = QObject::tr("Configuration editing is not available for this method.");
        else if (!bound) loader.settingsMessage = QObject::tr("This EFI program is not associated with the current system's generator configuration.");
        else if (!refindReady) loader.settingsMessage = refindError;
        else if (loader.kind == LoaderKind::Limine) {
            const auto document = QJsonDocument::fromJson(reader(QStringLiteral("limine-entry-tool"),
                {QStringLiteral("--config-json")}).toUtf8());
            if (!document.isObject() || !document.object().value(QStringLiteral("values")).isObject()
                || !document.object().value(QStringLiteral("kernel_cmdlines")).isObject())
                loader.settingsMessage = QObject::tr("The structured Limine settings query failed. Check limine-tool and refresh detection.");
            else {
                const auto root = document.object();
                const auto values = root.value(QStringLiteral("values")).toObject();
                const auto cmdlines = root.value(QStringLiteral("kernel_cmdlines")).toObject();
                for (const auto &setting : settings) {
                    const QString value = setting.key == QLatin1String("KERNEL_CMDLINE[default]")
                        ? cmdlines.value(QStringLiteral("default")).toString() : values.value(setting.key).toString();
                    loader.settingValues.insert(setting.key, value);
                }
                loader.settingsEditable = true;
            }
        } else {
            for (const auto &setting : settings)
                loader.settingValues.insert(setting.key, BootSettings::read(loader.kind, location, setting.key));
            loader.settingsEditable = true;
        }
        if ((loader.kind == LoaderKind::Grub || loader.kind == LoaderKind::Limine) && !loader.esp.isEmpty()) {
            loader.menuContent = readText(loader.config);
            loader.menuMessage = QObject::tr("This EFI installation is separate from the current system's generated menu.");
        } else if (loader.kind == LoaderKind::Limine) {
            loader.menuContent = reader(QStringLiteral("limine-entry-tool"), {QStringLiteral("--tree-json")});
            const auto document = QJsonDocument::fromJson(loader.menuContent.toUtf8());
            if (!document.isObject() || !document.object().value(QStringLiteral("entries")).isArray()
                || !document.object().value(QStringLiteral("globals")).isObject()) {
                loader.menuContent.clear();
                loader.menuMessage = QObject::tr("The structured Limine menu query failed. Check limine-tool and refresh detection.");
            }
        } else if (loader.kind == LoaderKind::Grub) {
            loader.menuContent = readText(QDir(systemRoot).filePath(QStringLiteral("boot/grub/grub.cfg")));
        } else if (loader.kind == LoaderKind::SystemdBoot) {
            loader.menuContent = reader(QStringLiteral("bootctl"),
                {QStringLiteral("--esp-path=") + loader.esp, QStringLiteral("--no-pager"), QStringLiteral("list")});
        } else if (loader.kind == LoaderKind::Refind) {
            loader.menuContent = readText(loader.config);
            loader.menuMessage = QObject::tr("rEFInd configuration source; automatic entries are discovered by rEFInd at boot.");
        } else {
            loader.menuMessage = QObject::tr("This direct boot method uses firmware entries rather than an internal boot menu.");
        }
        if (loader.menuContent.isEmpty() && loader.menuMessage.isEmpty())
            loader.menuMessage = QObject::tr("No menu content could be read. Check the target and refresh detection.");
    }
}
