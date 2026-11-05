#include "BootSettings.h"
#include "BootProbe.h"
#include "BootMenus.h"
#include "BootDeploy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>

namespace {
QString parseValue(QString text)
{
    text = text.trimmed();
    if (text.size() >= 2
        && ((text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"')))
            || (text.startsWith(QLatin1Char('\'')) && text.endsWith(QLatin1Char('\'')))))
        text = text.mid(1, text.size() - 2);
    return text;
}

QString settingValue(const QString &line, LoaderKind loader, const QString &key, bool *matched)
{
    const QString trimmed = line.trimmed();
    *matched = false;
    if (trimmed.startsWith(QLatin1Char('#'))) return {};
    if (loader == LoaderKind::SystemdBoot) {
        const auto fields = trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (!fields.isEmpty() && fields[0] == key) { *matched = true; return trimmed.mid(key.size()).trimmed(); }
        return {};
    }
    if (!trimmed.startsWith(key)) return {};
    QString rest = trimmed.mid(key.size());
    if (rest.startsWith(QLatin1Char('='))) rest.remove(0, 1);
    else return {};
    *matched = true;
    return parseValue(rest);
}

QString formattedLine(LoaderKind loader, const QString &key, const QString &value)
{
    if (loader == LoaderKind::SystemdBoot) return key + QLatin1Char(' ') + value;
    QString quoted = value;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return key + QStringLiteral("='") + quoted + QLatin1Char('\'');
}
}

QList<BootSettings::Setting> BootSettings::settings(LoaderKind loader)
{
    switch (loader) {
    case LoaderKind::Refind:
        return {{QStringLiteral("timeout"), QObject::tr("Menu timeout (seconds)"), QObject::tr("Seconds, -1 for immediate boot; empty uses the loader default")},
                {QStringLiteral("default_selection"), QObject::tr("Default entry"), QObject::tr("rEFInd default selection; empty uses the loader default")},
                {QStringLiteral("resolution"), QObject::tr("Menu resolution"), QObject::tr("For example: 1920 1080; empty uses the loader default")},
                {"scanfor", QObject::tr("Scan for boot entries"), QObject::tr("For example: internal,external,optical,manual")},
                {"scan_delay", QObject::tr("Scan delay (seconds)"), QObject::tr("Seconds to wait before scanning")},
                {"use_graphics_for", QObject::tr("Graphical boot entries"), QObject::tr("For example: linux,windows,osx")},
                {"icons_dir", QObject::tr("Icon directory"), QObject::tr("Directory containing menu icons")},
                {"banner", QObject::tr("Menu background"), QObject::tr("Path to the background image")}};
    case LoaderKind::Uki:
    case LoaderKind::EfiStub: {
        QList<Setting> result{{"label_prefix", QObject::tr("Boot entry name prefix"), QObject::tr("Name displayed before the kernel name")},
                {"cmdline", QObject::tr("Kernel parameters"), QObject::tr("Linux startup parameters")}};
        if (loader == LoaderKind::Uki) result.append({"default_kernel", QObject::tr("Removable boot kernel"), QObject::tr("Kernel used for the removable boot entry")});
        return result;
    }
    case LoaderKind::Grub:
        return {{QStringLiteral("GRUB_DEFAULT"), QObject::tr("Default entry"), QObject::tr("Entry index, ID or saved")},
                {QStringLiteral("GRUB_TIMEOUT"), QObject::tr("Menu timeout (seconds)"), QObject::tr("Time to wait before starting the default entry")},
                {QStringLiteral("GRUB_CMDLINE_LINUX_DEFAULT"), QObject::tr("Kernel parameters"), QObject::tr("Linux kernel parameters for normal boot")},
                {QStringLiteral("GRUB_GFXMODE"), QObject::tr("Menu resolution"), QObject::tr("For example: auto or 1920x1080")},
                {QStringLiteral("GRUB_THEME"), QObject::tr("GRUB theme"), QObject::tr("Absolute path to the theme file")},
                {QStringLiteral("GRUB_DISABLE_OS_PROBER"), QObject::tr("Disable other OS detection"), QObject::tr("true / false")},
                {QStringLiteral("GRUB_PRELOAD_MODULES"), QObject::tr("Preloaded GRUB modules"), QObject::tr("Space-separated module names")},
                {QStringLiteral("GRUB_DISABLE_LINUX_UUID"), QObject::tr("Disable Linux UUID"), QObject::tr("true / false")}};
    case LoaderKind::Limine:
        return {{"default_entry", QObject::tr("Default entry"), QObject::tr("Boot entry index, starting at 1")},
                {"timeout", QObject::tr("Menu timeout (seconds)"), QObject::tr("Seconds or no for manual selection")},
                {"quiet", QObject::tr("Quiet boot"), QObject::tr("yes / no")},
                {"graphics", QObject::tr("Graphical menu"), QObject::tr("yes / no")},
                {"remember_last_entry", QObject::tr("Remember last selection"), QObject::tr("yes / no")},
                {"wallpaper", QObject::tr("Menu background"), QObject::tr("Path to the background image")},
                {"wallpaper_style", QObject::tr("Background style"), QObject::tr("centered, stretched or tiled")},
                {"interface_resolution", QObject::tr("Menu resolution"), QObject::tr("For example: 1920x1080")},
                {"interface_branding", QObject::tr("Menu heading"), QObject::tr("Text displayed above boot entries")}};
    case LoaderKind::SystemdBoot:
        return {{QStringLiteral("default"), QObject::tr("Default entry"), QObject::tr("BLS entry ID or matching pattern")},
                {QStringLiteral("timeout"), QObject::tr("Menu timeout (seconds)"), QObject::tr("Seconds or a bootctl special timeout value")},
                {QStringLiteral("console-mode"), QObject::tr("Console mode"), QObject::tr("auto, keep, max or a mode number")},
                {QStringLiteral("editor"), QObject::tr("Allow kernel parameter edits"), QObject::tr("yes / no")},
                {"auto-entries", QObject::tr("Discover automatic entries"), QObject::tr("yes / no")},
                {"auto-firmware", QObject::tr("Show firmware settings"), QObject::tr("yes / no")},
                {"beep", QObject::tr("Menu sound"), QObject::tr("yes / no")}};
    }
    return {};
}

QString BootSettings::configPath(LoaderKind loader, const QString &esp)
{
    switch (loader) {
    case LoaderKind::Refind:
    case LoaderKind::Uki:
    case LoaderKind::EfiStub:
    case LoaderKind::Limine:
        return esp;
    case LoaderKind::Grub: return QStringLiteral("/etc/default/grub");
    case LoaderKind::SystemdBoot: return QDir(esp).filePath(QStringLiteral("loader/loader.conf"));
    }
    return {};
}

QString BootSettings::read(LoaderKind loader, const QString &esp, const QString &key, QString *error)
{
    error->clear();
    if (BootDeploy::directImage(loader)) return BootDeploy::profile(esp, error).value(key).toString();
    if (loader == LoaderKind::Refind) {
        QString source;
        return BootMenus::refindValue(esp, key, &source, error);
    }
    if (loader == LoaderKind::Limine) {
        const auto text = BootMenus::readText(esp, error);
        if (!error->isEmpty()) return {};
        return BootMenus::limine(text, error).value("globals").toObject().value(key).toString();
    }
    QFile file(configPath(loader, esp));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = QObject::tr("Unable to read configuration: %1").arg(file.fileName());
        return {};
    }
    QString result;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        bool matched = false;
        const QString value = settingValue(stream.readLine(), loader, key, &matched);
        if (matched) result = value;
    }
    if (file.error() != QFileDevice::NoError) {
        *error = QObject::tr("Unable to read configuration: %1").arg(file.fileName());
        return {};
    }
    return result;
}

bool BootSettings::allowed(LoaderKind loader, const QString &key, const QString &value)
{
    bool known = false;
    for (const auto &spec : settings(loader)) if (spec.key == key) known = true;
    if (!known || value.size() > 2048 || value.contains(QLatin1Char('\n'))
        || value.contains(QLatin1Char('\r')) || value.contains(QChar::Null)) return false;
    for (QChar ch : value) if (ch.unicode() < 32 || ch.unicode() == 127) return false;
    if (BootDeploy::directImage(loader)) return true;
    if (loader == LoaderKind::Limine) {
        if (value.isEmpty()) return true;
        if (key == QLatin1String("default_entry")) { bool ok = false; const int index = value.toInt(&ok); return ok && index > 0 && index <= 10000; }
        if (key == QLatin1String("timeout")) { bool ok = false; const auto seconds = value.toDouble(&ok); return value == QLatin1String("no") || (ok && seconds >= 0 && seconds <= 9999); }
        if (QStringList{"quiet", "graphics", "remember_last_entry"}.contains(key)) return value == QLatin1String("yes") || value == QLatin1String("no");
        if (key == QLatin1String("wallpaper_style")) return QStringList{"centered", "stretched", "tiled"}.contains(value);
        return true;
    }
    if (loader == LoaderKind::Refind) {
        if (value.isEmpty()) return true;
        if (key == QLatin1String("scan_delay")) return QRegularExpression(QStringLiteral("^[0-9]{1,4}$")).match(value).hasMatch();
        if (key == QLatin1String("timeout"))
            return QRegularExpression(QStringLiteral("^(?:[0-9]{1,4}|-1)$")).match(value).hasMatch();
        if (key == QLatin1String("resolution"))
            return QRegularExpression(QStringLiteral("^[0-9]{1,5}(?: [0-9]{1,5})?$")).match(value).hasMatch();
        return !value.contains(QLatin1Char('"')) && !value.contains(QLatin1Char('#'))
            && !value.contains(QLatin1Char('{')) && !value.contains(QLatin1Char('}'));
    }
    if (key == QLatin1String("GRUB_TIMEOUT"))
        return QRegularExpression(QStringLiteral("^(?:[0-9]{1,4}|-1)$")).match(value).hasMatch();
    if (loader == LoaderKind::SystemdBoot && value.isEmpty()) return true;
    if (loader == LoaderKind::SystemdBoot && key == QLatin1String("console-mode"))
        return QRegularExpression(QStringLiteral("^(?:auto|keep|max|[0-9]+)$")).match(value).hasMatch();
    if (loader == LoaderKind::SystemdBoot && key == QLatin1String("timeout"))
        return QRegularExpression(QStringLiteral("^(?:[0-9]{1,4}|menu-force|menu-hidden|menu-disabled)$"))
            .match(value).hasMatch();
    if (key == QLatin1String("GRUB_DISABLE_OS_PROBER")
        || key == QLatin1String("GRUB_DISABLE_LINUX_UUID"))
        return value.isEmpty() || value == QLatin1String("true") || value == QLatin1String("false");
    if (loader == LoaderKind::SystemdBoot && QStringList{"editor", "auto-entries", "auto-firmware", "beep"}.contains(key))
        return value.isEmpty() || value == QLatin1String("yes") || value == QLatin1String("no");
    return true;
}

bool BootSettings::write(LoaderKind loader, const QString &esp, const QString &key,
                         const QString &value, QString *error)
{
    if (!allowed(loader, key, value)) { if (error) *error = QStringLiteral("Unsupported setting or value"); return false; }
    if (BootDeploy::directImage(loader)) return BootDeploy::writeSetting(loader, esp, key, value, error);
    const QString path = configPath(loader, esp);
    if (loader == LoaderKind::Limine || loader == LoaderKind::Refind) {
        if (loader == LoaderKind::Refind && !refindEditable(path, error)) return false;
        const auto text = BootMenus::readText(path, error);
        if (!error->isEmpty()) return false;
        const auto changed = loader == LoaderKind::Limine ? BootMenus::setLimineValue(text, key, value, error)
            : BootMenus::setRefindValue(text, key, value);
        return error->isEmpty() && BootMenus::save(path, changed, error);
    }
    const QFileInfo info(path);
    if (path.isEmpty() || info.isSymLink() || !info.dir().exists()) {
        if (error) *error = QStringLiteral("Configuration path is unavailable");
        return false;
    }
    QStringList lines;
    QFile original(path);
    if (original.exists()) {
        if (!original.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error) *error = original.errorString();
            return false;
        }
        QTextStream reader(&original);
        while (!reader.atEnd()) lines.append(reader.readLine());
    }
    const QString replacement = formattedLine(loader, key, value);
    bool found = false;
    for (int i = 0; i < lines.size(); ++i) {
        bool matched = false;
        settingValue(lines[i], loader, key, &matched);
        if (matched) {
            if (value.isEmpty()) lines.removeAt(i--);
            else lines[i] = replacement;
            found = true;
        }
    }
    if (!found && !value.isEmpty()) lines.append(replacement);
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = output.errorString();
        return false;
    }
    const QByteArray contents = (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
    if (output.write(contents) != contents.size() || !output.commit()) {
        if (error) *error = output.errorString();
        return false;
    }
    return true;
}

bool BootSettings::refindEditable(const QString &path, QString *error)
{
    QString source;
    BootMenus::refindValue(path, "timeout", &source, error);
    return error->isEmpty();
}

QString BootSettings::operationKind(LoaderKind kind)
{
    return (kind == LoaderKind::SystemdBoot ? QStringLiteral("systemd") : BootDeploy::loaderId(kind)) + QStringLiteral("-setting");
}

QString BootSettings::operationTarget(const BootInstallation &loader, const QString &key)
{
    if (loader.kind == LoaderKind::Grub) return key;
    const auto location = loader.kind == LoaderKind::SystemdBoot ? loader.esp : loader.settingFiles.value(key, loader.config);
    return location + QLatin1Char('|') + key;
}
