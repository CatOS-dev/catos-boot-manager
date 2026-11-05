#include "BootMenus.h"
#include "BootBls.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace {
QString cleanLine(const QString &line)
{
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        if (line[i] == QLatin1Char('"')) quoted = !quoted;
        if (!quoted && line[i] == QLatin1Char('#')) return line.left(i);
    }
    return line;
}

int braces(const QString &line)
{
    bool quoted = false;
    int change = 0;
    for (const auto ch : cleanLine(line)) {
        if (ch == QLatin1Char('"')) quoted = !quoted;
        if (!quoted && ch == QLatin1Char('{')) ++change;
        if (!quoted && ch == QLatin1Char('}')) --change;
    }
    return change;
}

QString unquote(QString value)
{
    value = value.trimmed();
    if (value.startsWith('"') && value.endsWith('"')) value = value.mid(1, value.size() - 2);
    return value.replace(QStringLiteral("\"\""), QStringLiteral("\""));
}

QString quoted(QString value) { return QLatin1Char('"') + value.replace("\"", "\"\"") + QLatin1Char('"'); }

QJsonObject values(const QString &text, int level)
{
    QJsonObject result;
    int depth = 0;
    for (const auto &line : text.split('\n')) {
        const auto clean = cleanLine(line).trimmed();
        if (depth == level && !clean.isEmpty() && clean != QLatin1String("}")) {
            const int space = clean.indexOf(QRegularExpression(QStringLiteral("\\s")));
            const auto key = (space < 0 ? clean : clean.left(space)).toLower();
            if (key != QLatin1String("menuentry") && key != QLatin1String("submenuentry"))
                result.insert(key, space < 0 ? QString() : unquote(clean.mid(space + 1)));
        }
        depth += braces(line);
    }
    return result;
}

QJsonArray blocks(const QString &text, const QString &path, QString *error)
{
    QJsonArray entries;
    const auto lines = text.split('\n');
    int depth = 0;
    int start = -1;
    int offset = 0;
    const QRegularExpression heading(QStringLiteral("^menuentry\\s+(\"(?:[^\"]|\"\")*\"|[^\\s{]+)\\s*\\{"), QRegularExpression::CaseInsensitiveOption);
    QString title;
    for (const auto &line : lines) {
        const auto match = heading.match(cleanLine(line).trimmed());
        if (depth == 0 && match.hasMatch()) { start = offset; title = unquote(match.captured(1)); }
        depth += braces(line);
        if (depth < 0) { *error = QObject::tr("Invalid rEFInd menu configuration."); return {}; }
        const int end = qMin(offset + line.size() + 1, text.size());
        if (start >= 0 && depth == 0) {
            const QString raw = text.mid(start, end - start);
            entries.append(QJsonObject{{"name", title}, {"source", path}, {"start", start}, {"end", end},
                {"id", path + QLatin1Char('#') + QString::number(start)}, {"raw", raw}, {"values", values(raw, 1)}});
            start = -1;
        }
        offset = end;
    }
    if (depth != 0 || start >= 0) *error = QObject::tr("Invalid rEFInd menu configuration.");
    return entries;
}

bool collectRefind(const QString &path, const QString &base, QSet<QString> &active,
                   QJsonObject &document, int depth, QString *error)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (depth > 16 || active.contains(canonical) || QFileInfo(path).isSymLink()
        || canonical.isEmpty() || !BootMenus::safePath(QDir::cleanPath(path), base)) {
        *error = QObject::tr("Unable to read rEFInd configuration: %1").arg(path);
        return false;
    }
    active.insert(canonical);
    const QString text = BootMenus::readText(path, error);
    if (!error->isEmpty()) return false;
    auto sources = document.value("sources").toArray();
    sources.append(QJsonObject{{"path", path}, {"fingerprint", BootMenus::fingerprint(text)}});
    document.insert("sources", sources);
    auto entries = document.value("entries").toArray();
    const auto local = blocks(text, path, error);
    if (!error->isEmpty()) return false;
    int offset = 0;
    int nesting = 0;
    for (const auto &line : text.split('\n')) {
        for (const auto &entry : local) if (entry.toObject().value("start").toInt() == offset) {
            auto current = document.value("entries").toArray(); current.append(entry); document.insert("entries", current);
        }
        const QString clean = cleanLine(line).trimmed();
        if (nesting == 0 && !clean.isEmpty()) {
            const int space = clean.indexOf(QRegularExpression(QStringLiteral("\\s")));
            const QString key = (space < 0 ? clean : clean.left(space)).toLower();
            const QString value = space < 0 ? QString() : unquote(clean.mid(space + 1));
            if (key == QLatin1String("include")) {
                const auto include = QString(value).replace('\\', '/');
                const auto target = include.startsWith('/') ? base + include : QDir(QFileInfo(path).absolutePath()).filePath(include);
                if (!collectRefind(QDir::cleanPath(target), base, active, document, depth + 1, error)) return false;
            } else if (key != QLatin1String("menuentry")) {
                auto globals = document.value("globals").toObject();
                auto locations = document.value("settingSources").toObject();
                globals.insert(key, value);
                locations.insert(key, path);
                document.insert("globals", globals);
                document.insert("settingSources", locations);
            }
        }
        nesting += braces(line);
        offset = qMin(offset + line.size() + 1, text.size());
    }
    active.remove(canonical);
    return true;
}

QJsonObject nativeMenu(const QJsonObject &request, QString *error)
{
    const QDir directory(QCoreApplication::applicationDirPath());
    const auto executable = directory.dirName() == QLatin1String("bin")
        ? QDir::cleanPath(directory.filePath("../lib/catos/catos-limine-menu"))
        : directory.filePath("catos-limine-menu");
    QProcess process;
    process.start(executable, {});
    if (!process.waitForStarted(5000)) { *error = QObject::tr("Unable to read the Limine boot menu."); return {}; }
    process.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
    process.closeWriteChannel();
    if (!process.waitForFinished(10000)) {
        process.kill(); process.waitForFinished();
        *error = QObject::tr("Unable to read the Limine boot menu."); return {};
    }
    const auto result = QJsonDocument::fromJson(process.readAllStandardOutput());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !result.isObject()) {
        *error = QObject::tr("Invalid Limine menu configuration."); return {};
    }
    return result.object();
}

QString replaceValues(QString text, const QJsonObject &replacement, int level, bool refind)
{
    for (auto it = replacement.begin(); it != replacement.end(); ++it) {
        auto lines = text.split('\n');
        int depth = 0;
        bool found = false;
        for (int i = 0; i < lines.size(); ++i) {
            const QString clean = cleanLine(lines[i]).trimmed();
            const auto key = clean.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0);
            const int change = braces(lines[i]);
            if (depth == level && key.compare(it.key(), Qt::CaseInsensitive) == 0) {
                lines[i] = (level ? QStringLiteral("    ") : QString()) + it.key()
                    + QLatin1Char(' ') + (refind ? quoted(it.value().toString()) : it.value().toString());
                if (found || it.value().toString().isEmpty()) lines.removeAt(i--);
                found = true;
            }
            depth += change;
        }
        if (!found && !it.value().toString().isEmpty()) {
            const QString addition = (level ? QStringLiteral("    ") : QString()) + it.key() + QLatin1Char(' ')
                + (refind ? quoted(it.value().toString()) : it.value().toString());
            if (level) {
                int end = lines.size() - 1;
                while (end >= 0 && lines[end].trimmed() != QLatin1String("}")) --end;
                lines.insert(qMax(0, end), addition);
            } else lines.append(addition);
        }
        text = lines.join('\n');
    }
    return text;
}
}

QString BootMenus::limineGeneratorPath(QString *error)
{
    error->clear();
    return nativeMenu({{"action", "generator-path"}}, error).value("path").toString();
}

bool BootMenus::safePath(const QString &path, const QString &base)
{
    if (!QDir::isAbsolutePath(path) || QDir::cleanPath(path) != path || !QDir::isAbsolutePath(base)
        || !path.startsWith(QDir::cleanPath(base) + '/')) return false;
    QString current = path;
    while (current != base) {
        if (QFileInfo(current).isSymLink()) return false;
        const auto parent = QFileInfo(current).absolutePath();
        if (parent == current) return false;
        current = parent;
    }
    return !QFileInfo(base).isSymLink();
}

QString BootMenus::readText(const QString &path, QString *error)
{
    error->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { *error = QObject::tr("Unable to read configuration: %1").arg(path); return {}; }
    const auto data = file.read(2 * 1024 * 1024 + 1);
    if (file.error() != QFileDevice::NoError || data.size() > 2 * 1024 * 1024) {
        *error = QObject::tr("Unable to read configuration: %1").arg(path); return {};
    }
    return QString::fromUtf8(data);
}

QString BootMenus::fingerprint(const QString &text) { return BootBls::fingerprint(text); }

QJsonObject BootMenus::limine(const QString &content, QString *error)
{
    error->clear();
    return nativeMenu({{"action", "inspect"}, {"content", content}}, error);
}

QJsonObject BootMenus::refind(const QString &path, const QString &esp, QString *error)
{
    error->clear();
    QSet<QString> active;
    QJsonObject result;
    const QString base = QFileInfo(esp).canonicalFilePath();
    if (base.isEmpty() || !collectRefind(path, base, active, result, 0, error)) return {};
    return result;
}

QString BootMenus::refindValue(const QString &path, const QString &key, QString *source, QString *error)
{
    QSet<QString> active;
    QJsonObject result;
    error->clear();
    QString base = QFileInfo(path).absolutePath();
    const int marker = path.indexOf("/EFI/", 0, Qt::CaseInsensitive);
    if (marker > 0) base = path.left(marker);
    if (!collectRefind(path, base, active, result, 0, error)) return {};
    *source = result.value("settingSources").toObject().value(key).toString(path);
    return result.value("globals").toObject().value(key).toString();
}

QString BootMenus::setRefindValue(const QString &content, const QString &key, const QString &value)
{
    const bool quotes = key == QLatin1String("default_selection") || key == QLatin1String("banner")
        || key == QLatin1String("icons_dir") || key == QLatin1String("font");
    return replaceValues(content, {{key, quotes && !value.isEmpty() ? quoted(value) : value}}, 0, false);
}

QJsonObject BootMenus::bls(const QString &path, QString *error)
{
    const auto text = readText(path, error);
    QJsonObject fields;
    QJsonArray initrds;
    for (const auto &line : text.split('\n')) {
        const auto clean = line.trimmed();
        if (clean.isEmpty() || clean.startsWith('#')) continue;
        const int space = clean.indexOf(QRegularExpression(QStringLiteral("\\s")));
        if (space < 0) continue;
        const QString key = clean.left(space).toLower();
        const QString value = clean.mid(space + 1).trimmed();
        if (key == QLatin1String("initrd")) initrds.append(value);
        else fields.insert(key, value);
    }
    fields.insert("initrd", initrds);
    return {{"values", fields}, {"source", path}, {"fingerprint", fingerprint(text)}};
}

QString BootMenus::renderLimine(const QString &content, const QJsonArray &entries, QString *error)
{
    error->clear();
    return nativeMenu({{"action", "render"}, {"content", content}, {"entries", entries}}, error).value("content").toString();
}

QString BootMenus::setLimineValue(const QString &content, const QString &key, const QString &value, QString *error)
{
    error->clear();
    return nativeMenu({{"action", "setting"}, {"content", content}, {"key", key}, {"value", value}}, error).value("content").toString();
}

QString BootMenus::renderRefind(const QString &content, const QJsonArray &entries, QString *error)
{
    error->clear();
    const auto original = blocks(content, {}, error);
    if (!error->isEmpty()) return {};
    QStringList rendered;
    QList<int> origins;
    bool ordered = true;
    int lastOrigin = -1;
    for (const auto &entry : entries) {
        const auto object = entry.toObject();
        const QString name = object.value("name").toString();
        if (name.trimmed().isEmpty() || name.contains('\n') || name.contains('\r') || name.contains(QChar::Null)) { *error = QObject::tr("Enter a boot entry name."); return {}; }
        QString raw = object.value("raw").toString();
        if (raw.isEmpty()) raw = QStringLiteral("menuentry ") + quoted(name) + QStringLiteral(" {\n}\n");
        else {
            const auto parsed = blocks(raw, {}, error);
            if (!error->isEmpty() || parsed.size() != 1 || parsed[0].toObject().value("start").toInt() != 0
                || parsed[0].toObject().value("end").toInt() != raw.size()) { *error = QObject::tr("Invalid rEFInd menu configuration."); return {}; }
            const QRegularExpression heading(QStringLiteral("^menuentry\\s+(\"(?:[^\"]|\"\")*\"|[^\\s{]+)\\s*\\{"), QRegularExpression::CaseInsensitiveOption);
            const auto match = heading.match(raw.trimmed());
            raw.replace(raw.indexOf(match.captured(1)), match.captured(1).size(), quoted(name));
        }
        const auto replacement = object.value("values").toObject();
        for (auto it = replacement.begin(); it != replacement.end(); ++it) {
            if (!QRegularExpression(QStringLiteral("^[a-z][a-z0-9_-]*$")).match(it.key()).hasMatch() || !it.value().isString()
                || it.value().toString().contains('\n') || it.value().toString().contains('\r') || it.value().toString().contains(QChar::Null)) {
                *error = QObject::tr("Invalid boot entry settings."); return {};
            }
        }
        if (replacement.value("loader").toString().isEmpty() && !replacement.contains("firmware_bootnum")) {
            *error = QObject::tr("Choose a kernel or EFI program."); return {};
        }
        const auto originalValues = values(raw, 1);
        auto changes = replacement;
        for (auto it = replacement.begin(); it != replacement.end(); ++it)
            if (originalValues.value(it.key()) == it.value()) changes.remove(it.key());
        raw = replaceValues(raw, changes, 1, true);
        int origin = -1;
        for (int i = 0; i < original.size(); ++i)
            if (object.contains("start") && object.value("start") == original[i].toObject().value("start")) origin = i;
        if (origin >= 0) { ordered &= origin > lastOrigin; lastOrigin = origin; }
        origins.append(origin);
        rendered.append(raw.endsWith('\n') ? raw : raw + '\n');
    }
    QString result;
    int position = 0;
    for (int i = 0; i < original.size(); ++i) {
        const auto block = original[i].toObject();
        result += content.mid(position, block.value("start").toInt() - position);
        const int replacement = ordered ? origins.indexOf(i) : i;
        if (replacement >= 0 && replacement < rendered.size()) result += rendered[replacement];
        position = block.value("end").toInt();
    }
    result += content.mid(position);
    for (int i = 0; i < rendered.size(); ++i) {
        if (ordered ? origins[i] >= 0 : i < original.size()) continue;
        if (!result.isEmpty() && !result.endsWith('\n')) result += '\n';
        result += rendered[i];
    }
    blocks(result, {}, error);
    return error->isEmpty() ? result : QString();
}

QString BootMenus::renderBls(const QString &content, const QJsonObject &fields, QString *error)
{
    error->clear();
    if (fields.value("title").toString().trimmed().isEmpty() || fields.value("linux").toString().isEmpty() == fields.value("efi").toString().isEmpty()
        || !fields.value("initrd").isArray()) { *error = QObject::tr("Enter a name and choose a kernel or EFI program."); return {}; }
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        if (!QRegularExpression(QStringLiteral("^[a-z][a-z0-9_-]*$")).match(it.key()).hasMatch()) { *error = QObject::tr("Invalid boot entry settings."); return {}; }
        const auto list = it.key() == QLatin1String("initrd") ? it.value().toArray() : QJsonArray{it.value()};
        for (const auto &value : list) if (!value.isString() || value.toString().contains('\n') || value.toString().contains('\r') || value.toString().contains(QChar::Null)) {
            *error = QObject::tr("Invalid boot entry settings."); return {};
        }
    }
    auto scalar = fields;
    if (!scalar.contains("linux")) scalar.insert("linux", "");
    if (!scalar.contains("efi")) scalar.insert("efi", "");
    const auto initrd = scalar.take("initrd").toArray();
    QString result = replaceValues(content, scalar, 0, false);
    auto lines = result.split('\n');
    for (int i = lines.size() - 1; i >= 0; --i)
        if (lines[i].trimmed().section(QRegularExpression(QStringLiteral("\\s+")), 0, 0) == QLatin1String("initrd")) lines.removeAt(i);
    for (const auto &image : initrd) if (!image.toString().isEmpty()) lines.append(QStringLiteral("initrd ") + image.toString());
    result = lines.join('\n');
    return result.endsWith('\n') ? result : result + '\n';
}

bool BootMenus::save(const QString &path, const QString &content, QString *error)
{
    if (QFileInfo(path).isSymLink() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = QObject::tr("Unable to save configuration: %1").arg(path); return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { *error = file.errorString(); return false; }
    const auto data = content.toUtf8();
    if (file.write(data) != data.size() || !file.commit()) { *error = file.errorString(); return false; }
    return true;
}

bool BootMenus::validate(const BootOperation &op, const BootSnapshot &snapshot, QString *error)
{
    error->clear();
    if (op.kind != QLatin1String("menu-write")) { *error = QObject::tr("Invalid menu operation."); return false; }
    const auto request = QJsonDocument::fromJson(op.value.toUtf8()).object();
    const QString format = request.value("format").toString();
    bool bound = false;
    QString esp;
    for (const auto &loader : snapshot.installations) {
        if (format == QLatin1String("limine") && loader.kind == LoaderKind::Limine && loader.config == op.target && !loader.esp.isEmpty()) { bound = true; esp = loader.esp; }
        if (format == QLatin1String("refind") && loader.kind == LoaderKind::Refind) {
            for (const auto &source : QJsonDocument::fromJson(loader.menuContent.toUtf8()).object().value("sources").toArray())
                if (source.toObject().value("path").toString() == op.target) { bound = true; esp = loader.esp; }
        }
        if (format == QLatin1String("bls") && loader.kind == LoaderKind::SystemdBoot) {
            const QString parent = QDir::cleanPath(loader.esp + QStringLiteral("/loader/entries"));
            if (QFileInfo(op.target).absolutePath() == parent && op.target.endsWith(QLatin1String(".conf"))
                && BootBls::validId(QFileInfo(op.target).fileName())) { bound = true; esp = loader.esp; }
        }
    }
    if (!bound || !safePath(op.target, esp) || request.isEmpty()) { *error = QObject::tr("The selected boot configuration changed. Refresh and try again."); return false; }
    const QString text = QFileInfo::exists(op.target) ? readText(op.target, error) : QString();
    if (!error->isEmpty()) return false;
    if (fingerprint(text) != op.expected) { *error = QObject::tr("The boot menu changed. Refresh and try again."); return false; }
    if (request.value("remove").toBool()) {
        if (format == QLatin1String("bls") && QFileInfo(op.target).isFile()) return true;
        *error = QObject::tr("The boot entry no longer exists."); return false;
    }
    QString rendered;
    if (format == QLatin1String("limine")) rendered = renderLimine(text, request.value("entries").toArray(), error);
    else if (format == QLatin1String("refind")) rendered = renderRefind(text, request.value("entries").toArray(), error);
    else {
        const auto fields = request.value("values").toObject();
        rendered = renderBls(text, fields, error);
        QJsonArray paths = fields.value("initrd").toArray();
        const auto executable = fields.value(fields.value("linux").toString().isEmpty() ? "efi" : "linux").toString();
        paths.append(executable);
        for (const auto &value : paths) {
            const auto path = value.toString();
            if (!path.startsWith('/') || !safePath(esp + path, esp) || !QFileInfo(esp + path).isFile() || QFileInfo(esp + path).size() == 0) {
                *error = QObject::tr("Choose existing kernel and initramfs files on the EFI partition."); return false;
            }
        }
    }
    if (rendered.contains(QChar::Null) || rendered.size() > 1024 * 1024) *error = QObject::tr("Invalid boot menu configuration.");
    return error->isEmpty();
}

bool BootMenus::execute(const BootOperation &op, QString *error)
{
    error->clear();
    const auto request = QJsonDocument::fromJson(op.value.toUtf8()).object();
    const QString text = QFileInfo::exists(op.target) ? readText(op.target, error) : QString();
    if (!error->isEmpty()) return false;
    if (fingerprint(text) != op.expected) { *error = QObject::tr("The boot menu changed. Refresh and try again."); return false; }
    if (request.value("remove").toBool()) {
        if (!QFile::remove(op.target)) { *error = QObject::tr("Unable to remove boot entry."); return false; }
        return true;
    }
    const QString format = request.value("format").toString();
    const QString rendered = format == QLatin1String("limine") ? renderLimine(text, request.value("entries").toArray(), error)
        : format == QLatin1String("refind") ? renderRefind(text, request.value("entries").toArray(), error)
        : renderBls(text, request.value("values").toObject(), error);
    return error->isEmpty() && save(op.target, rendered, error);
}
