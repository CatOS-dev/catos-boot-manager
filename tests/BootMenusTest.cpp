#include "services/BootMenus.h"
#include "services/BootContent.h"
#include "services/BootSettings.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool fixture(const QString &path, const QByteArray &content)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}
BootOperation menuOperation(const QString &path, const QString &text, const QJsonObject &request)
{
    return {"menu-write", path, QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)), BootMenus::fingerprint(text)};
}
}
class BootMenusTest : public QObject {
    Q_OBJECT
private slots:
    void limineEditNestedMenuOnSelectedEsp();
    void refindIncludesKeepOrderAndEffectiveSettings();
    void blsCreateEditDeleteAndRejectUnsafeTargets();
    void preserveMultilineBlsOptions();
};

void BootMenusTest::limineEditNestedMenuOnSelectedEsp()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString original = "${ROOT}=boot():\ntimeout: 5\n/Linux\n//Main\nprotocol: linux\npath: ${ROOT}/vmlinuz\nunknown: keep\n/Windows\nprotocol: efi\npath: boot():/EFI/windows.efi\n";
    const auto path = dir.path() + "/esp/limine.conf";
    const auto other = dir.path() + "/other/limine.conf";
    QVERIFY(fixture(path, original.toUtf8())); QVERIFY(fixture(other, original.toUtf8()));
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::Limine, "one", dir.path() + "/esp", {}, {}, path, true},
        {LoaderKind::Limine, "two", dir.path() + "/other", {}, {}, other, true}};
    BootContent::populate(snapshot);
    QVERIFY(snapshot.installations[0].settingsEditable);
    auto menu = QJsonDocument::fromJson(snapshot.installations[0].menuContent.toUtf8()).object();
    auto entries = menu.value("entries").toArray();
    auto linuxGroup = entries[0].toObject();
    auto children = linuxGroup.value("children").toArray();
    auto main = children[0].toObject(); main.insert("name", "Updated"); children[0] = main;
    linuxGroup.insert("children", children); entries[0] = entries[1]; entries[1] = linuxGroup;
    const auto op = menuOperation(path, original, {{"format", "limine"}, {"entries", entries}});
    QString error;
    QVERIFY2(BootMenus::validate(op, snapshot, &error), qPrintable(error));
    QVERIFY2(BootMenus::execute(op, &error), qPrintable(error));
    const auto changed = BootMenus::readText(path, &error);
    QVERIFY(error.isEmpty()); QVERIFY(changed.contains("//Updated")); QVERIFY(changed.contains("unknown: keep"));
    QVERIFY(changed.contains("${ROOT}=boot():")); QVERIFY(changed.indexOf("/Windows") < changed.indexOf("/Linux"));
    QCOMPARE(BootMenus::readText(other, &error), original); QVERIFY(error.isEmpty());
    QVERIFY(!BootMenus::validate(op, snapshot, &error)); QVERIFY(!error.isEmpty());
    QVERIFY2(BootSettings::write(LoaderKind::Limine, path, "timeout", "8", &error), qPrintable(error));
    QCOMPARE(BootSettings::read(LoaderKind::Limine, path, "timeout", &error), QString("8")); QVERIFY(error.isEmpty());
    QVERIFY(BootMenus::readText(path, &error).contains("unknown: keep"));
}

void BootMenusTest::refindIncludesKeepOrderAndEffectiveSettings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto esp = dir.path() + "/esp";
    const auto path = esp + "/EFI/refind/refind.conf";
    const auto theme = esp + "/EFI/themes/menu.conf";
    const QString original = "timeout 5\nmenuentry \"First\" {\n loader /EFI/first.efi\n submenuentry \"Rescue\" {\n  options \"single\"\n }\n unknown keep\n}\ninclude ../themes/menu.conf\nmenuentry \"Last\" {\n loader /EFI/last.efi\n}\n";
    QVERIFY(fixture(path, original.toUtf8()));
    QVERIFY(fixture(theme, "timeout 9\nmenuentry \"Theme\" {\n loader /EFI/theme.efi\n}\n"));
    QString error;
    auto menu = BootMenus::refind(path, esp, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto entries = menu.value("entries").toArray(); QCOMPARE(entries.size(), 3);
    QCOMPARE(entries[0].toObject().value("name").toString(), QString("First"));
    QCOMPARE(entries[1].toObject().value("name").toString(), QString("Theme"));
    QCOMPARE(entries[2].toObject().value("name").toString(), QString("Last"));
    QCOMPARE(menu.value("globals").toObject().value("timeout").toString(), QString("9"));
    QString source;
    QCOMPARE(BootMenus::refindValue(path, "timeout", &source, &error), QString("9")); QVERIFY(error.isEmpty());
    QCOMPARE(QDir::cleanPath(source), theme);
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::Refind, "refind", esp, {}, {}, path, true}};
    BootContent::populate(snapshot);
    QCOMPARE(QDir::cleanPath(snapshot.installations[0].settingFiles.value("timeout")), theme);
    auto first = entries[0].toObject(); first.insert("name", "Renamed");
    auto values = first.value("values").toObject(); values.insert("options", "root=UUID=test ro"); first.insert("values", values);
    auto op = menuOperation(path, original, {{"format", "refind"}, {"entries", QJsonArray{first, entries[2]}}});
    QVERIFY2(BootMenus::validate(op, snapshot, &error), qPrintable(error));
    QVERIFY2(BootMenus::execute(op, &error), qPrintable(error));
    const auto changed = BootMenus::readText(path, &error); QVERIFY(error.isEmpty());
    QVERIFY(changed.contains("submenuentry \"Rescue\"")); QVERIFY(changed.contains("unknown keep"));
    QVERIFY(changed.indexOf("Renamed") < changed.indexOf("include")); QVERIFY(changed.indexOf("include") < changed.indexOf("Last"));
    QVERIFY2(BootSettings::write(LoaderKind::Refind, theme, "timeout", "12", &error), qPrintable(error));
    QCOMPARE(BootMenus::refindValue(path, "timeout", &source, &error), QString("12")); QVERIFY(error.isEmpty());
    QVERIFY(fixture(path, "include \"\\EFI\\themes\\menu.conf\"\n"));
    auto absolute = BootMenus::refind(path, esp, &error); QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(absolute.value("globals").toObject().value("timeout").toString(), QString("12"));
    QVERIFY(fixture(theme, "include ../refind/refind.conf\n"));
    QVERIFY(BootMenus::refind(path, esp, &error).isEmpty()); QVERIFY(!error.isEmpty());
}

void BootMenusTest::blsCreateEditDeleteAndRejectUnsafeTargets()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto esp = dir.path() + "/esp";
    const auto path = esp + "/loader/entries/linux.conf";
    QVERIFY(QDir().mkpath(esp + "/loader/entries"));
    QVERIFY(fixture(esp + "/vmlinuz", "kernel")); QVERIFY(fixture(esp + "/microcode", "microcode")); QVERIFY(fixture(esp + "/initrd", "initramfs"));
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::SystemdBoot, "systemd", esp}};
    QJsonObject fields{{"title", "Linux"}, {"linux", "/vmlinuz"}, {"initrd", QJsonArray{"/microcode", "/initrd"}}, {"options", "root=UUID=test ro"}};
    auto op = menuOperation(path, {}, {{"format", "bls"}, {"values", fields}});
    QString error;
    QVERIFY2(BootMenus::validate(op, snapshot, &error), qPrintable(error));
    QVERIFY2(BootMenus::execute(op, &error), qPrintable(error));
    auto document = BootMenus::bls(path, &error); QVERIFY(error.isEmpty());
    QCOMPARE(document.value("values").toObject(), fields);
    auto original = BootMenus::readText(path, &error); QVERIFY(error.isEmpty());
    QVERIFY(fixture(path, original.toUtf8() + "architecture x64\n# retained\n"));
    original = BootMenus::readText(path, &error); QVERIFY(error.isEmpty());
    fields = BootMenus::bls(path, &error).value("values").toObject(); QVERIFY(error.isEmpty()); fields.insert("title", "New Linux");
    op = menuOperation(path, original, {{"format", "bls"}, {"values", fields}});
    QVERIFY2(BootMenus::validate(op, snapshot, &error), qPrintable(error)); QVERIFY2(BootMenus::execute(op, &error), qPrintable(error));
    auto changed = BootMenus::readText(path, &error); QVERIFY(error.isEmpty());
    QVERIFY(changed.contains("architecture x64")); QVERIFY(changed.contains("# retained"));
    QCOMPARE(BootMenus::bls(path, &error).value("values").toObject().value("initrd").toArray(), QJsonArray({"/microcode", "/initrd"})); QVERIFY(error.isEmpty());
    fields.remove("linux"); fields.insert("efi", "/vmlinuz");
    auto switchType = menuOperation(path, changed, {{"format", "bls"}, {"values", fields}});
    QVERIFY2(BootMenus::validate(switchType, snapshot, &error), qPrintable(error));
    QVERIFY2(BootMenus::execute(switchType, &error), qPrintable(error));
    const auto switched = BootMenus::bls(path, &error).value("values").toObject(); QVERIFY(error.isEmpty());
    QVERIFY(!switched.contains("linux")); QCOMPARE(switched.value("efi").toString(), QString("/vmlinuz"));
    changed = BootMenus::readText(path, &error); QVERIFY(error.isEmpty());
    fields.insert("title", "Injected\nlinux /other");
    auto bad = menuOperation(path, changed, {{"format", "bls"}, {"values", fields}});
    QVERIFY(!BootMenus::validate(bad, snapshot, &error)); QVERIFY(!error.isEmpty());
    fields.insert("title", "Linux"); fields.insert("linux", "/../outside");
    bad = menuOperation(path, changed, {{"format", "bls"}, {"values", fields}});
    QVERIFY(!BootMenus::validate(bad, snapshot, &error)); QVERIFY(!error.isEmpty());
    QVERIFY(QDir().rename(esp + "/loader/entries", dir.path() + "/outside"));
    QVERIFY(QFile::link(dir.path() + "/outside", esp + "/loader/entries"));
    bad = menuOperation(path, changed, {{"format", "bls"}, {"remove", true}});
    QVERIFY(!BootMenus::validate(bad, snapshot, &error)); QVERIFY(!error.isEmpty());
    QVERIFY(QFile::remove(esp + "/loader/entries")); QVERIFY(QDir().rename(dir.path() + "/outside", esp + "/loader/entries"));
    auto remove = menuOperation(path, changed, {{"format", "bls"}, {"remove", true}});
    QVERIFY2(BootMenus::validate(remove, snapshot, &error), qPrintable(error)); QVERIFY2(BootMenus::execute(remove, &error), qPrintable(error));
    QVERIFY(!QFileInfo::exists(path));
}

void BootMenusTest::preserveMultilineBlsOptions()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = dir.filePath("entry.conf");
    QVERIFY(fixture(path, "title Linux\nlinux /vmlinuz\noptions root=UUID=test\n  ro quiet\n"));
    QString error;
    const auto values = BootMenus::bls(path, &error).value("values").toObject();
    QVERIFY(error.isEmpty());
    QCOMPARE(values.value("options").toString(), QString("root=UUID=test ro quiet"));
}
QTEST_GUILESS_MAIN(BootMenusTest)
#include "BootMenusTest.moc"
