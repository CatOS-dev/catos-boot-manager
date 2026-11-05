#include "BootSettings.h"
#include "BootProbe.h"

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
    if (loader == LoaderKind::SystemdBoot || loader == LoaderKind::Refind) {
        const auto fields = trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (!fields.isEmpty() && fields[0] == key) {
            *matched = true;
            QString value = trimmed.mid(key.size()).trimmed();
            if (loader == LoaderKind::Refind) {
                if (value.startsWith(QLatin1Char('"'))) return value.section(QLatin1Char('"'), 1, 1);
                value = value.section(QLatin1Char('#'), 0, 0).trimmed();
            }
            return value;
        }
        return {};
    }
    if (!trimmed.startsWith(key)) return {};
    QString rest = trimmed.mid(key.size());
    if (loader == LoaderKind::Limine && rest.startsWith(QLatin1String("+="))) rest.remove(0, 2);
    else if (rest.startsWith(QLatin1Char('='))) rest.remove(0, 1);
    else return {};
    *matched = true;
    return parseValue(rest);
}

QString formattedLine(LoaderKind loader, const QString &key, const QString &value)
{
    if (loader == LoaderKind::SystemdBoot) return key + QLatin1Char(' ') + value;
    if (loader == LoaderKind::Refind)
        return key + QLatin1Char(' ') + (key == QLatin1String("default_selection")
            ? QLatin1Char('"') + value + QLatin1Char('"') : value);
    if (loader == LoaderKind::Limine) {
        if (key.startsWith(QLatin1String("KERNEL_CMDLINE"))) return key + QLatin1Char('=') + value;
        return key + QStringLiteral("=\"") + value + QLatin1Char('"');
    }
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
                {QStringLiteral("resolution"), QObject::tr("Menu resolution"), QObject::tr("For example: 1920 1080; empty uses the loader default")}};
    case LoaderKind::Uki:
    case LoaderKind::EfiStub:
        return {};
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
        return {{QStringLiteral("KERNEL_CMDLINE[default]"), QObject::tr("Default kernel parameters"), QObject::tr("Regenerate kernel entries with limine-tool")},
                {QStringLiteral("BOOT_ORDER"), QObject::tr("Kernel menu order"), QObject::tr("For example: *, *fallback, Snapshots")},
                {QStringLiteral("FIND_BOOTLOADERS"), QObject::tr("Discover other boot loaders"), QObject::tr("yes / no")},
                {QStringLiteral("MKINITCPIO_FALLBACK"), QObject::tr("Fallback initramfs"), QObject::tr("yes / no or a kernel package name")}};
    case LoaderKind::SystemdBoot:
        return {{QStringLiteral("default"), QObject::tr("Default entry"), QObject::tr("BLS entry ID or matching pattern")},
                {QStringLiteral("timeout"), QObject::tr("Menu timeout (seconds)"), QObject::tr("Seconds or a bootctl special timeout value")},
                {QStringLiteral("console-mode"), QObject::tr("Console mode"), QObject::tr("auto, keep, max or a mode number")},
                {QStringLiteral("editor"), QObject::tr("Allow kernel parameter edits"), QObject::tr("yes / no")}};
    }
    return {};
}

QString BootSettings::configPath(LoaderKind loader, const QString &esp)
{
    switch (loader) {
    case LoaderKind::Refind:
        return esp;
    case LoaderKind::Uki:
    case LoaderKind::EfiStub:
        return {};
    case LoaderKind::Grub: return QStringLiteral("/etc/default/grub");
    case LoaderKind::Limine: return QStringLiteral("/etc/default/limine");
    case LoaderKind::SystemdBoot: return QDir(esp).filePath(QStringLiteral("loader/loader.conf"));
    }
    return {};
}

QString BootSettings::read(LoaderKind loader, const QString &esp, const QString &key)
{
    if (loader == LoaderKind::Limine) {
        const auto output = BootProbe::runReadOnly(QStringLiteral("limine-entry-tool"),
                                                  {QStringLiteral("--config-json")});
        const auto document = QJsonDocument::fromJson(output.toUtf8());
        if (document.isObject()) {
            const QString prefix = QStringLiteral("KERNEL_CMDLINE[");
            if (key.startsWith(prefix) && key.endsWith(QLatin1Char(']'))) {
                const QString kernel = key.mid(prefix.size(), key.size() - prefix.size() - 1);
                return document.object().value(QStringLiteral("kernel_cmdlines"))
                    .toObject().value(kernel).toString();
            }
            return document.object().value(QStringLiteral("values"))
                .toObject().value(key).toString();
        }
    }
    QFile file(configPath(loader, esp));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString result;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        bool matched = false;
        const QString value = settingValue(stream.readLine(), loader, key, &matched);
        if (matched) result = value;
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
    if (loader == LoaderKind::Refind) {
        if (value.isEmpty()) return true;
        if (key == QLatin1String("timeout"))
            return QRegularExpression(QStringLiteral("^(?:[0-9]{1,4}|-1)$")).match(value).hasMatch();
        if (key == QLatin1String("resolution"))
            return QRegularExpression(QStringLiteral("^[0-9]{1,5}(?: [0-9]{1,5})?$")).match(value).hasMatch();
        return !value.contains(QLatin1Char('"')) && !value.contains(QLatin1Char('#'))
            && !value.contains(QLatin1Char('{')) && !value.contains(QLatin1Char('}'));
    }
    if (key == QLatin1String("GRUB_TIMEOUT"))
        return QRegularExpression(QStringLiteral("^(?:[0-9]{1,4}|-1)$")).match(value).hasMatch();
    if (loader == LoaderKind::SystemdBoot && key == QLatin1String("timeout"))
        return QRegularExpression(QStringLiteral("^(?:[0-9]{1,4}|menu-force|menu-hidden|menu-disabled)$"))
            .match(value).hasMatch();
    if (key == QLatin1String("GRUB_DISABLE_OS_PROBER")
        || key == QLatin1String("GRUB_DISABLE_LINUX_UUID"))
        return value == QLatin1String("true") || value == QLatin1String("false");
    if (key == QLatin1String("FIND_BOOTLOADERS") || key == QLatin1String("editor"))
        return value == QLatin1String("yes") || value == QLatin1String("no");
    if (key == QLatin1String("MKINITCPIO_FALLBACK") && value.isEmpty()) return false;
    if (loader == LoaderKind::Limine && value.contains(QLatin1Char('"'))
        && !key.startsWith(QLatin1String("KERNEL_CMDLINE"))) return false;
    return true;
}

bool BootSettings::write(LoaderKind loader, const QString &esp, const QString &key,
                         const QString &value, QString *error)
{
    if (!allowed(loader, key, value)) { if (error) *error = QStringLiteral("Unsupported setting or value"); return false; }
    const QString path = configPath(loader, esp);
    if (loader == LoaderKind::Refind && !refindEditable(path, error)) return false;
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
            if (loader == LoaderKind::Refind && value.isEmpty()) lines.removeAt(i--);
            else lines[i] = replacement;
            found = true;
        }
    }
    if (!found && !(loader == LoaderKind::Refind && value.isEmpty())) lines.append(replacement);
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
    QFile file(path);
    const QFileInfo info(path);
    if (info.fileName() != QLatin1String("refind.conf") || info.isSymLink()
        || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QObject::tr("The selected rEFInd configuration cannot be read safely.");
        return false;
    }
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QString directive = line.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0);
        if (directive.compare(QLatin1String("include"), Qt::CaseInsensitive) == 0
            || line.contains(QLatin1Char('{')) || line.contains(QLatin1Char('}'))) {
            if (error) *error = QObject::tr("This rEFInd configuration contains includes or manual entry blocks. Basic editing is unavailable because effective values require source-aware editing.");
            return false;
        }
    }
    return true;
}
