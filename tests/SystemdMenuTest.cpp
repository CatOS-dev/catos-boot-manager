#include "services/BootBls.h"
#include "services/BootContent.h"
#include "model/BootPlan.h"
#include "pages/MenuPage.h"
#include "pages/SettingsPage.h"
#include "widgets/LoaderSelector.h"
#include <QLineEdit>
#include <QDialog>
#include <QAbstractButton>
#include <QMessageBox>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QTimer>
#include "services/BootMenus.h"
#include <QTabWidget>
#include <QTranslator>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

namespace {
bool writeFixture(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
}

class SystemdMenuTest : public QObject {
    Q_OBJECT
private slots:
    void displayRealBootctlEntriesAndQueueExactDefault();
    void selectGrubSubmenuDefault();
    void selectRefindDefault();
    void rejectDuplicateAndForeignEntryIdentities();
    void editLimineNestedDefaultAndPreserveDrafts();
    void addAndCancelNewBlsEntry();
};

void SystemdMenuTest::displayRealBootctlEntriesAndQueueExactDefault()
{
    QTranslator translator;
    QVERIFY(translator.load(QCoreApplication::applicationDirPath() + "/catos-boot-manager_zh_CN.qm"));
    QApplication::installTranslator(&translator);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString esp = directory.path();
    QVERIFY(QDir().mkpath(esp + "/loader/entries"));
    QVERIFY(writeFixture(esp + "/loader/loader.conf", "default catos-one.conf\ntimeout 3\n"));
    const QByteArray entry = "title CatOS Linux\nlinux /vmlinuz-test\ninitrd /initramfs-test.img\noptions root=UUID=test ro\n";
    QVERIFY(writeFixture(esp + "/loader/entries/catos-one.conf", entry));
    QVERIFY(writeFixture(esp + "/loader/entries/catos-two.conf", entry));
    QVERIFY(writeFixture(esp + "/vmlinuz-test", "kernel test asset"));
    QVERIFY(writeFixture(esp + "/initramfs-test.img", "initramfs test asset"));
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::SystemdBoot, "systemd", esp, "uuid", {}, esp + "/loader/loader.conf", true}};
    int exitCode = -1;
    bool completed = false;
    QString error;
    BootContent::populate(snapshot, [&](const QString &program, const QStringList &arguments) {
        QProcess command;
        auto environment = QProcessEnvironment::systemEnvironment();
        // Only this contract test relaxes filesystem checks for its temporary ESP fixture.
        environment.insert("SYSTEMD_RELAX_ESP_CHECKS", "1");
        environment.insert("SYSTEMD_RELAX_XBOOTLDR_CHECKS", "1");
        environment.insert("LC_ALL", "C");
        command.setProcessEnvironment(environment);
        command.start(program, arguments);
        completed = command.waitForFinished();
        exitCode = command.exitCode();
        error = QString::fromUtf8(command.readAllStandardError());
        return QString::fromUtf8(command.readAllStandardOutput());
    });
    QVERIFY2(completed, qPrintable(error));
    QCOMPARE(exitCode, 0);
    QVERIFY(snapshot.installations.first().menuMessage.isEmpty());
    QString parseError;
    const auto entries = BootBls::entries(snapshot.installations.first().menuContent, esp, &parseError);
    QVERIFY2(parseError.isEmpty(), qPrintable(parseError));
    QCOMPARE(entries.size(), 2);
    MenuPage page;
    SettingsPage settings;
    connect(&page, &MenuPage::operationRequested, &settings, &SettingsPage::reflectOperation);
    page.updateSnapshot(snapshot);
    settings.updateSnapshot(snapshot);
    settings.selector()->selectInstallation(page.selector()->method(), page.selector()->currentId());
    auto *tree = page.findChild<QTreeWidget *>("bootMenuEntries");
    auto *select = page.findChild<QComboBox *>("defaultBootEntry");
    QVERIFY(tree);
    QVERIFY(select);
    QCOMPARE(tree->topLevelItemCount(), 2);
    QVERIFY(select->isEnabled());
    QTreeWidgetItem *other = nullptr;
    QTreeWidgetItem *original = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto *item = tree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == "catos-two.conf") other = item;
        if (item->data(0, Qt::UserRole).toString() == "catos-one.conf") original = item;
    }
    QVERIFY(other);
    QVERIFY(original);
    tree->setCurrentItem(other);
    BootTransaction transaction;
    connect(&page, &MenuPage::operationRequested, &page, [&](const BootOperation &op) { transaction.stage(op); });
    connect(&settings, &SettingsPage::operationRequested, &page, [&](const BootOperation &op) { transaction.stage(op); });
    select->setCurrentIndex(select->findData(tree->currentItem()->data(0, Qt::UserRole)));
    QVERIFY(QMetaObject::invokeMethod(select, "activated", Q_ARG(int, select->currentIndex())));
    QCOMPARE(transaction.count(), 1);
    const auto operation = transaction.operations().first();
    QCOMPARE(operation.kind, QString("systemd-setting"));
    QCOMPARE(operation.target, esp + "|default");
    QCOMPARE(operation.expected, QString("catos-one.conf"));
    QCOMPARE(operation.value, QString("catos-two.conf"));
    QVERIFY(!operation.expectedMenu.isEmpty());
    QVERIFY(BootBls::defaultTargetMatches(snapshot, esp, operation.value, operation.expectedMenu));
    BootSnapshot changed = snapshot;
    changed.installations[0].menuContent = "[]";
    QVERIFY(!BootBls::defaultTargetMatches(changed, esp, operation.value, operation.expectedMenu));
    QVERIFY(!BootBls::defaultTargetMatches(snapshot, esp, "missing.conf", operation.expectedMenu));
    QVERIFY(page.findChildren<QTabWidget *>().isEmpty());
    auto *timeout = settings.findChild<QLineEdit *>("timeout");
    QVERIFY(timeout);
    timeout->setText("5");
    QCOMPARE(transaction.count(), 2);
    QCOMPARE(transaction.operations().first().value, QString("catos-two.conf"));
    QCOMPARE(transaction.operations().first().expectedMenu, operation.expectedMenu);
    timeout->setText("3");
    QCOMPARE(transaction.count(), 1);
    page.resize(1040, 700);
    page.show();
    QVERIFY(QTest::qWaitForWindowActive(&page));
    QVERIFY(page.grab().save("/tmp/catos-menu-systemd.png"));
    tree->setCurrentItem(original);
    select->setCurrentIndex(select->findData(tree->currentItem()->data(0, Qt::UserRole)));
    QVERIFY(QMetaObject::invokeMethod(select, "activated", Q_ARG(int, select->currentIndex())));
    QCOMPARE(transaction.count(), 0);
    snapshot.systemdDefaultOverride = "efi-override.conf";
    QVERIFY(!BootPlan::firmwareConflict({operation}, snapshot).isEmpty());
    page.updateSnapshot(snapshot);
    QVERIFY(!select->isEnabled());
}

void SystemdMenuTest::selectGrubSubmenuDefault()
{
    BootInstallation loader;
    loader.kind = LoaderKind::Grub; loader.id = "grub"; loader.config = "/etc/default/grub";
    loader.configPresent = true; loader.settingsEditable = true;
    loader.settingValues.insert("GRUB_DEFAULT", "0");
    loader.menuContent = "menuentry 'CatOS {Linux}' --id 'main' {\n echo \"ignore }\" # }\n}\n"
        "submenu 'Advanced' $menuentry_id_option 'advanced' {\n"
        " menuentry 'Linux LTS' --id=\"lts\" {\n  echo '${root}'\n }\n"
        " menuentry 'Recovery' {\n }\n}\nmenuentry 'Windows' {\n}\n";
    BootSnapshot snapshot; snapshot.installations = {loader};
    MenuPage page; page.updateSnapshot(snapshot);
    SettingsPage settings; settings.updateSnapshot(snapshot);
    auto *tree = page.findChild<QTreeWidget *>("bootMenuEntries"); QVERIFY(tree);
    QCOMPARE(tree->topLevelItemCount(), 3);
    QCOMPARE(tree->topLevelItem(1)->childCount(), 2);
    auto *choice = page.findChild<QComboBox *>("defaultBootEntry"); QVERIFY(choice);
    QCOMPARE(choice->currentText(), QString("CatOS {Linux}"));
    QVERIFY(settings.findChild<QLineEdit *>("GRUB_TIMEOUT"));
    QVERIFY(!settings.findChild<QLineEdit *>("GRUB_DEFAULT"));
    BootTransaction transaction;
    connect(&page, &MenuPage::operationRequested, &page, [&](const BootOperation &op) { transaction.stage(op); page.updateTransaction(transaction); });
    const int lts = choice->findText("Advanced / Linux LTS"); QVERIFY(lts >= 0);
    choice->setCurrentIndex(lts);
    QVERIFY(QMetaObject::invokeMethod(choice, "activated", Q_ARG(int, lts)));
    QCOMPARE(transaction.count(), 1);
    QCOMPARE(transaction.operations()[0].target, QString("GRUB_DEFAULT"));
    QCOMPARE(transaction.operations()[0].value, QString("advanced>lts"));
    page.selectMethod(LoaderKind::Limine); page.selectMethod(LoaderKind::Grub);
    QCOMPARE(choice->currentText(), QString("Advanced / Linux LTS"));
    transaction.clear(); page.updateTransaction(transaction);
    QCOMPARE(choice->currentText(), QString("CatOS {Linux}"));
    loader.settingValues["GRUB_DEFAULT"] = "1>1";
    snapshot.installations = {loader}; page.updateSnapshot(snapshot);
    QCOMPARE(choice->currentText(), QString("Advanced / Recovery"));
    const int recovery = choice->currentIndex();
    QVERIFY(QMetaObject::invokeMethod(choice, "activated", Q_ARG(int, recovery)));
    QCOMPARE(transaction.operations()[0].value, QString("advanced>1"));
}

void SystemdMenuTest::selectRefindDefault()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto config = dir.path() + "/refind.conf";
    QVERIFY(writeFixture(config, "default_selection Main\ntimeout 5\nmenuentry Main {\n loader /vmlinuz\n}\nmenuentry LTS {\n loader /vmlinuz-lts\n}\n"));
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::Refind, "refind", dir.path(), {}, {}, config, true}};
    BootContent::populate(snapshot);
    MenuPage page; page.updateSnapshot(snapshot);
    auto *choice = page.findChild<QComboBox *>("defaultBootEntry"); QVERIFY(choice);
    QCOMPARE(choice->currentText(), QString("Main"));
    BootTransaction transaction;
    connect(&page, &MenuPage::operationRequested, &page, [&](const BootOperation &op) { transaction.stage(op); page.updateTransaction(transaction); });
    choice->setCurrentIndex(choice->findText("LTS"));
    QVERIFY(QMetaObject::invokeMethod(choice, "activated", Q_ARG(int, choice->currentIndex())));
    QCOMPARE(transaction.count(), 1);
    QCOMPARE(transaction.operations()[0].kind, QString("refind-setting"));
    QCOMPARE(transaction.operations()[0].target, config + "|default_selection");
    QCOMPARE(transaction.operations()[0].value, QString("LTS"));
    page.selectMethod(LoaderKind::Limine); page.selectMethod(LoaderKind::Refind);
    QCOMPARE(choice->currentText(), QString("LTS"));
    transaction.clear(); page.updateTransaction(transaction);
    QCOMPARE(choice->currentText(), QString("Main"));
}

void SystemdMenuTest::rejectDuplicateAndForeignEntryIdentities()
{
    QString error;
    const QString entries = R"([{"type":"type1","id":"same.conf","root":"/esp","path":"/esp/loader/entries/same.conf"},{"type":"type1","id":"same.conf","root":"/esp","path":"/esp/loader/entries/other.conf"}])";
    QVERIFY(BootBls::entries(entries, "/esp", &error).isEmpty());
    QVERIFY(!error.isEmpty());
    const QString foreign = R"([{"type":"loader","id":"reported","path":"/sys/efivars"},{"type":"type1","id":"other.conf","root":"/other-esp","path":"/other-esp/loader/entries/other.conf"}])";
    QVERIFY(BootBls::entries(foreign, "/esp", &error).isEmpty());
    QVERIFY(error.isEmpty());
    QVERIFY(!BootBls::validId("../other.conf"));
    QVERIFY(!BootBls::validId("*.conf"));
    QVERIFY(BootBls::validId("catos-linux+3.conf"));
}

void SystemdMenuTest::editLimineNestedDefaultAndPreserveDrafts()
{
    QTranslator translator;
    QVERIFY(translator.load(QCoreApplication::applicationDirPath() + "/catos-boot-manager_zh_CN.qm"));
    QApplication::installTranslator(&translator);
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = dir.path() + "/limine.conf";
    QVERIFY(writeFixture(path, "timeout: 5\ndefault_entry: 1\n/Linux\n//Main\nprotocol: linux\npath: boot():/vmlinuz\n/Windows\nprotocol: efi\npath: boot():/EFI/windows.efi\n"));
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::Limine, "limine", dir.path(), {}, {}, path, true}};
    BootContent::populate(snapshot);
    MenuPage page;
    page.updateSnapshot(snapshot);
    BootTransaction transaction;
    connect(&page, &MenuPage::operationRequested, &page, [&](const BootOperation &operation) { transaction.stage(operation); page.updateTransaction(transaction); });
    auto *tree = page.findChild<QTreeWidget *>("bootMenuEntries"); QVERIFY(tree); QCOMPARE(tree->topLevelItemCount(), 2);
    auto *entry = tree->topLevelItem(0)->child(0); QVERIFY(entry); tree->setCurrentItem(entry);
    auto *setDefault = page.findChild<QComboBox *>("defaultBootEntry");
    QVERIFY(setDefault); QVERIFY(setDefault->isEnabled());
    const int nested = setDefault->findText("Linux / Main"); QVERIFY(nested >= 0);
    setDefault->setCurrentIndex(nested);
    QVERIFY(QMetaObject::invokeMethod(setDefault, "activated", Q_ARG(int, nested)));
    QCOMPARE(transaction.count(), 2);
    QCOMPARE(transaction.operations()[0].kind, QString("menu-write"));
    QCOMPARE(transaction.operations()[1].kind, QString("limine-setting"));
    QCOMPARE(transaction.operations()[1].value, QString("2"));
    QCOMPARE(setDefault->currentText(), QString("Linux / Main"));
    auto *remove = page.findChild<QPushButton *>("removeMenuEntry"); QVERIFY(remove);
    tree->setCurrentItem(tree->topLevelItem(0));
    QTimer::singleShot(0, &page, [&] {
        auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(dialog);
        QCOMPARE(dialog->text(), QCoreApplication::translate("MenuPage", "Choose another default boot entry before removing this entry."));
        dialog->accept();
    });
    remove->click();
    QCOMPARE(tree->topLevelItemCount(), 2);
    QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
    QCOMPARE(transaction.operations()[1].value, QString("2"));
    const auto request = QJsonDocument::fromJson(transaction.operations()[0].value.toUtf8()).object();
    QVERIFY(request.value("entries").toArray()[0].toObject().value("expanded").toBool());
    auto *edit = page.findChild<QPushButton *>("editMenuEntry"); QVERIFY(edit);
    tree->setCurrentItem(tree->topLevelItem(0)->child(0));
    QTimer::singleShot(0, &page, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
        auto *name = dialog->findChild<QLineEdit *>("entryName"); QVERIFY(name); name->setText("Updated Linux");
        auto *parameters = dialog->findChild<QLineEdit *>("entryParameters"); QVERIFY(parameters); parameters->setText("root=UUID=test ro");
        dialog->accept();
    });
    edit->click();
    QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QString("Updated Linux"));
    QCOMPARE(transaction.count(), 2);
    QCOMPARE(transaction.operations()[1].value, QString("2"));
    page.selectMethod(LoaderKind::Grub); page.selectMethod(LoaderKind::Limine);
    QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QString("Updated Linux"));
    page.resize(1050, 850); page.show(); QVERIFY(QTest::qWaitForWindowActive(&page));
    QVERIFY(page.grab().save("/tmp/catos-menu-limine-complete.png"));
    transaction.clear(); page.updateTransaction(transaction);
    QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QString("Main"));
    QCOMPARE(setDefault->currentText(), QCoreApplication::translate("MenuPage", "Current default"));
    const int main = setDefault->findText("Linux / Main"); QVERIFY(main >= 0);
    setDefault->setCurrentIndex(main);
    QVERIFY(QMetaObject::invokeMethod(setDefault, "activated", Q_ARG(int, main)));
    QCOMPARE(setDefault->currentText(), QString("Linux / Main"));
    setDefault->setCurrentIndex(0);
    QVERIFY(QMetaObject::invokeMethod(setDefault, "activated", Q_ARG(int, 0)));
    QCOMPARE(setDefault->currentText(), QCoreApplication::translate("MenuPage", "Loader default"));
    QCOMPARE(setDefault->currentData().toString(), QString());
    QCOMPARE(transaction.operations().last().value, QString());
}

void SystemdMenuTest::addAndCancelNewBlsEntry()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto esp = dir.path(); QVERIFY(QDir().mkpath(esp + "/loader/entries"));
    QVERIFY(writeFixture(esp + "/loader/loader.conf", "timeout 3\n"));
    BootSnapshot snapshot;
    snapshot.installations = {{LoaderKind::SystemdBoot, "systemd", esp, {}, {}, esp + "/loader/loader.conf", true}};
    BootContent::populate(snapshot, [](const QString &, const QStringList &) { return QString("[]"); });
    MenuPage page; page.updateSnapshot(snapshot);
    BootTransaction transaction;
    connect(&page, &MenuPage::operationRequested, &page, [&](const BootOperation &operation) { transaction.stage(operation); page.updateTransaction(transaction); });
    auto *add = page.findChild<QPushButton *>("addMenuEntry"); QVERIFY(add);
    QTimer::singleShot(0, &page, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
        auto *name = dialog->findChild<QLineEdit *>("entryName"); QVERIFY(name); name->setText("New Linux");
        auto *id = dialog->findChild<QLineEdit *>("entryId"); QVERIFY(id); id->setText("new.conf");
        auto *kernel = dialog->findChild<QLineEdit *>("entryKernel"); QVERIFY(kernel); kernel->setText("/vmlinuz");
        auto *initrd = dialog->findChild<QPlainTextEdit *>("entryInitramfs"); QVERIFY(initrd); initrd->setPlainText("/microcode\n/initrd");
        dialog->accept();
    });
    add->click(); QCOMPARE(transaction.count(), 1);
    auto *tree = page.findChild<QTreeWidget *>("bootMenuEntries"); QVERIFY(tree); QCOMPARE(tree->topLevelItemCount(), 1);
    QCOMPARE(tree->topLevelItem(0)->text(0), QString("New Linux"));
    QVERIFY(!QFileInfo::exists(esp + "/loader/entries/new.conf"));
    tree->setCurrentItem(tree->topLevelItem(0));
    auto *remove = page.findChild<QPushButton *>("removeMenuEntry"); QVERIFY(remove); QVERIFY(remove->isEnabled());
    QTimer::singleShot(0, &page, [&] {
        auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(message); auto *yes = message->button(QMessageBox::Yes); QVERIFY(yes); yes->click();
    });
    remove->click(); QCOMPARE(transaction.count(), 0); QCOMPARE(tree->topLevelItemCount(), 0);
}

QTEST_MAIN(SystemdMenuTest)
#include "SystemdMenuTest.moc"
