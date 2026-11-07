#include "model/BootDiff.h"
#include "services/BootPreview.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class BootPreviewTest : public QObject {
    Q_OBJECT
private slots:
    void unifiedDiff();
    void previewWithoutWriting();
};

void BootPreviewTest::unifiedDiff()
{
    QVERIFY(BootDiff::unified("a\nb\n", "a\nb\n").isEmpty());
    QCOMPARE(BootDiff::unified("a\nb\nc\n", "a\nB\nc\n"),
             QStringList({"@@ -1,3 +1,3 @@", " a", "-b", "+B", " c"}));
    QCOMPARE(BootDiff::unified({}, "new\n"), QStringList({"@@ -0,0 +1,1 @@", "+new"}));
    QCOMPARE(BootDiff::unified("old\n", {}), QStringList({"@@ -1,1 +0,0 @@", "-old"}));
    QStringList before;
    for (int i = 1; i <= 20; ++i) before << QString::number(i);
    QStringList after = before;
    after[1] = "two"; after[17] = "eighteen";
    const auto separate = BootDiff::unified(before.join('\n') + '\n', after.join('\n') + '\n', 2);
    QCOMPARE(separate.filter(QRegularExpression("^@@")).size(), 2);
    after[9] = "ten";
    const auto merged = BootDiff::unified(before.join('\n') + '\n', after.join('\n') + '\n', 4);
    QCOMPARE(merged.filter(QRegularExpression("^@@")).size(), 1);
}

void BootPreviewTest::previewWithoutWriting()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const QString esp = directory.path();
    QVERIFY(QDir().mkpath(esp + "/loader/entries"));
    QFile config(esp + "/loader/loader.conf");
    QVERIFY(config.open(QIODevice::WriteOnly)); config.write("timeout 3\neditor no\n"); config.close();
    QFile entry(esp + "/loader/entries/old.conf");
    QVERIFY(entry.open(QIODevice::WriteOnly)); entry.write("title Old\nlinux /vmlinuz\n"); entry.close();
    const QString created = esp + "/loader/entries/new.conf";
    const QJsonObject values{{"title", "New"}, {"linux", "/vmlinuz"}, {"initrd", QJsonArray{"/initramfs.img"}}, {"options", "quiet"}};
    const QList<BootOperation> operations{
        {"systemd-setting", esp + "|timeout", "8", "3", "Timeout"},
        {"menu-write", created, QJsonDocument(QJsonObject{{"format", "bls"}, {"values", values}}).toJson(QJsonDocument::Compact), "", "Add"},
        {"menu-write", esp + "/loader/entries/old.conf", R"({"format":"bls","remove":true})", "", "Remove"},
        {"efistub-setting", "/target/etc/catos/firmware-boot.conf|cmdline", "quiet", "ro", "Parameters"}};
    QString error;
    const auto files = BootPreview::files(operations, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(files.size(), 4);
    QHash<QString, QJsonObject> byPath;
    for (const auto &value : files) byPath.insert(value.toObject().value("path").toString(), value.toObject());
    const auto loader = byPath.value(esp + "/loader/loader.conf");
    QCOMPARE(loader.value("after").toString(), QString("timeout 8\neditor no\n"));
    const auto added = byPath.value(created);
    QVERIFY(!added.value("existed").toBool());
    QVERIFY(added.value("after").toString().contains("title New"));
    QVERIFY(added.value("after").toString().contains("initrd /initramfs.img"));
    QVERIFY(byPath.value(esp + "/loader/entries/old.conf").value("removed").toBool());
    const auto provider = byPath.value("/target/etc/catos/firmware-boot.conf").value("settings").toArray();
    QCOMPARE(provider.size(), 1);
    QCOMPARE(provider[0].toObject().value("after").toString(), QString("quiet"));
    // Nothing was written.
    QVERIFY(!QFileInfo::exists(created));
    QVERIFY(QFileInfo::exists(esp + "/loader/entries/old.conf"));
    QVERIFY(config.open(QIODevice::ReadOnly));
    QCOMPARE(config.readAll(), QByteArray("timeout 3\neditor no\n"));
}

QTEST_GUILESS_MAIN(BootPreviewTest)
#include "BootPreviewTest.moc"
