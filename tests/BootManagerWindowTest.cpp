#include "app/BootManagerWindow.h"
#include "pages/MenuPage.h"
#include "pages/SettingsPage.h"
#include "pages/PendingPage.h"
#include "pages/SecurityPage.h"
#include "pages/FirmwarePage.h"
#include "dialogs/ApplyDialog.h"
#include <QLabel>
#include <QTableWidget>
#include "services/BootSession.h"
#include "model/BootSnapshotCodec.h"
#include <QApplication>
#include <QAbstractButton>
#include <QCloseEvent>
#include <QDialog>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QCheckBox>
#include <QFrame>
#include <QPlainTextEdit>
#include <QTreeWidget>
#include <QJsonArray>
#include <QStackedWidget>
#include <QTabWidget>
#include <QDir>
#include <QtTest>

class BootManagerWindowTest : public QObject {
    Q_OBJECT
private slots:
    void protectDraftsOnRefreshAndClose();
    void invalidEditsBlockApplyAndConfirmDiscard();
    void confirmImmediateExecutionBeforeStarting();
    void retainFailedTransaction_data();
    void retainFailedTransaction();
    void separateConfigurationAndRecoveryPages();
    void queueBackupRestore();
    void readOnlyModeUntilAuthorized();
    void explainDisabledActions();
    void showMokCertificates();
    void confirmHighRiskWrites();
};

static BootSnapshot settingsSnapshot()
{
    BootInstallation loader;
    loader.kind = LoaderKind::Grub; loader.id = "grub";
    loader.config = "/etc/default/grub";
    loader.configPresent = true; loader.settingsEditable = true;
    loader.settingValues = {{"GRUB_TIMEOUT", "3"}};
    BootSnapshot snapshot; snapshot.installations = {loader};
    return snapshot;
}

void BootManagerWindowTest::protectDraftsOnRefreshAndClose()
{
    BootSession session;
    BootManagerWindow window(&session);
    QCoreApplication::processEvents();
    emit session.snapshotReady(settingsSnapshot());
    auto *settings = window.findChild<SettingsPage *>(); QVERIFY(settings);
    auto *timeout = settings->findChild<QLineEdit *>("GRUB_TIMEOUT"); QVERIFY(timeout);
    QCOMPARE(timeout->placeholderText(), QString());
    QCOMPARE(settings->findChild<QLabel *>("GRUB_TIMEOUTHelp")->text(), QString());
    timeout->setText("7");
    QTimer::singleShot(0, &window, [] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
        box->button(QMessageBox::No)->click();
    });
    window.findChild<QPushButton *>("refreshDetection")->click();
    QCOMPARE(timeout->text(), QString("7"));
    QVERIFY(settings->hasDraftChanges());
    QTimer::singleShot(0, &window, [] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
        box->button(QMessageBox::Cancel)->click();
    });
    QCloseEvent cancelled;
    QApplication::sendEvent(&window, &cancelled);
    QVERIFY(!cancelled.isAccepted());
    QCOMPARE(timeout->text(), QString("7"));
    QTimer::singleShot(0, &window, [] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
        box->button(QMessageBox::Discard)->click();
    });
    QCloseEvent discarded;
    QApplication::sendEvent(&window, &discarded);
    QVERIFY(discarded.isAccepted());
}

void BootManagerWindowTest::invalidEditsBlockApplyAndConfirmDiscard()
{
    BootSession session;
    BootManagerWindow window(&session);
    QCoreApplication::processEvents();
    emit session.snapshotReady(settingsSnapshot());
    auto *settings = window.findChild<SettingsPage *>(); QVERIFY(settings);
    auto *timeout = settings->findChild<QLineEdit *>("GRUB_TIMEOUT"); QVERIFY(timeout);
    auto *apply = window.findChild<QPushButton *>("applyTransaction"); QVERIFY(apply);
    auto *discard = window.findChild<QPushButton *>("discardAllChanges"); QVERIFY(discard);
    auto *pending = window.findChild<QListWidget *>("pendingOperations"); QVERIFY(pending);
    timeout->setText("5");
    QCOMPARE(pending->count(), 1);
    QVERIFY(apply->isEnabled());
    timeout->setText("bad");
    QVERIFY(settings->hasInvalidDrafts());
    QVERIFY(!apply->isEnabled());
    QVERIFY(discard->isEnabled());
    QVERIFY(settings->findChild<QLabel *>("GRUB_TIMEOUTHelp")->text().startsWith("Invalid value."));
    QTimer::singleShot(0, &window, [] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
        box->button(QMessageBox::Cancel)->click();
    });
    discard->click();
    QCOMPARE(timeout->text(), QString("bad"));
    QCOMPARE(pending->count(), 1);
    QTimer::singleShot(0, &window, [] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
        box->button(QMessageBox::Discard)->click();
    });
    discard->click();
    QCOMPARE(pending->count(), 0);
    QVERIFY(!settings->hasDraftChanges());
    QVERIFY(!settings->hasInvalidDrafts());
    QVERIFY(!discard->isEnabled());
}

void BootManagerWindowTest::confirmImmediateExecutionBeforeStarting()
{
    BootSession session;
    BootTransaction transaction;
    transaction.stage({"repair-install", "grub:uefi:/", R"({"action":"repair","root":"/target","esp":"/target/efi","register":true,"fallback":false})", "", "Repair GRUB on /target"});
    QTimer::singleShot(0, [] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
        QCOMPARE(dialog->windowTitle(), QString("Confirm immediate execution"));
        auto *confirm = dialog->findChild<QPushButton *>("confirmExecution"); QVERIFY(confirm);
        QCOMPARE(confirm->text(), QString("Repair now"));
        QVERIFY(!confirm->isDefault());
        QString text;
        for (auto *label : dialog->findChildren<QLabel *>()) text += label->text();
        QVERIFY(text.contains("/target/efi"));
        QVERIFY(text.contains("Register UEFI entry: Yes"));
        QVERIFY(text.contains("Replace removable EFI loader: No"));
        dialog->reject();
    });
    QCOMPARE(ApplyDialog::execute(nullptr, session, transaction, true), ApplyResult::Cancelled);
    QVERIFY(!session.isBusy());
    QVERIFY(!session.isAuthorized());
}

void BootManagerWindowTest::retainFailedTransaction_data()
{
    QTest::addColumn<int>("code");
    QTest::newRow("preflight") << 3;
    QTest::newRow("partial") << 4;
}

void BootManagerWindowTest::retainFailedTransaction()
{
    QFETCH(int, code);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    QFile launcher(directory.filePath("pkexec")); QVERIFY(launcher.open(QIODevice::WriteOnly));
    const QByteArray script = "#!/bin/sh\nexec \"$CATOS_WINDOW_TEST_BINARY\" --fixture-session\n";
    QCOMPARE(launcher.write(script), script.size()); launcher.close();
    QVERIFY(launcher.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", directory.path().toUtf8() + ':' + oldPath);
    qputenv("CATOS_WINDOW_TEST_BINARY", QCoreApplication::applicationFilePath().toUtf8());
    qputenv("CATOS_WINDOW_TEST_CODE", QByteArray::number(code));
    const auto restore = qScopeGuard([&] {
        qputenv("PATH", oldPath);
        qunsetenv("CATOS_WINDOW_TEST_BINARY"); qunsetenv("CATOS_WINDOW_TEST_CODE");
    });
    BootSession session;
    QSignalSpy authorized(&session, &BootSession::authorized);
    session.start(); QTRY_COMPARE(authorized.count(), 1);
    BootManagerWindow window(&session);
    QCoreApplication::processEvents();
    QTRY_VERIFY(!session.isBusy());
    emit session.snapshotReady(settingsSnapshot());
    auto *menu = window.findChild<MenuPage *>(); QVERIFY(menu);
    emit menu->operationRequested({"grub-setting", "GRUB_TIMEOUT", "5", "3", "Change timeout"});
    auto *pending = window.findChild<QListWidget *>("pendingOperations"); QVERIFY(pending);
    QCOMPARE(pending->count(), 1);
    bool sawWarning = false;
    bool sawDiff = false;
    QTimer modal;
    connect(&modal, &QTimer::timeout, &window, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        if (auto *box = qobject_cast<QMessageBox *>(dialog)) {
            sawWarning = true;
            QVERIFY(box->text().contains("retained"));
            box->accept();
            return;
        }
        if (auto *changes = dialog->findChild<QPlainTextEdit *>("changeDiff"))
            sawDiff |= changes->toPlainText().contains("-GRUB_TIMEOUT='3'") && changes->toPlainText().contains("+GRUB_TIMEOUT='5'");
        for (auto *button : dialog->findChildren<QPushButton *>())
            if ((button->text() == "Apply" || button->text() == "Close") && button->isEnabled()) {
                if (button->text() == "Close") {
                    auto *results = dialog->findChild<QTableWidget *>("executionResults"); QVERIFY(results);
                    QCOMPARE(results->rowCount(), 1);
                    QCOMPARE(results->item(0, 0)->text(), QString("Change timeout"));
                    QCOMPARE(results->item(0, 1)->text(), code == 3 ? QString("Not executed") : QString("Failed; changes may be partial"));
                }
                button->click(); return;
            }
    });
    modal.start(10);
    window.findChild<QPushButton *>("applyTransaction")->click();
    modal.stop();
    QVERIFY(sawWarning);
    QVERIFY(sawDiff);
    QCOMPARE(session.lastApplyCode(), code);
    QCOMPARE(pending->count(), 1);
    QVERIFY(window.findChild<QPushButton *>("applyTransaction")->isEnabled());
}

void BootManagerWindowTest::separateConfigurationAndRecoveryPages()
{
    BootSession session;
    BootManagerWindow window(&session);
    QCoreApplication::processEvents();
    emit session.snapshotReady(settingsSnapshot());
    auto *navigation = window.findChild<QListWidget *>("navigation"); QVERIFY(navigation);
    QStringList names;
    for (int i = 0; i < navigation->count(); ++i) names << navigation->item(i)->text();
    QCOMPARE(names, QStringList({"Overview", "Boot methods", "Boot menu", "UEFI entries",
        "Secure Boot", "Repair and recovery", "Pending changes"}));
    auto *pages = window.findChild<QStackedWidget *>("pages"); QVERIFY(pages);
    navigation->setCurrentRow(2);
    auto *tabs = qobject_cast<QTabWidget *>(pages->currentWidget()); QVERIFY(tabs);
    QCOMPARE(tabs->count(), 2);
    QVERIFY(qobject_cast<MenuPage *>(tabs->widget(0)));
    tabs->setCurrentIndex(1);
    QVERIFY(qobject_cast<SettingsPage *>(tabs->currentWidget()));
    QVERIFY(tabs->currentWidget()->findChild<QLineEdit *>("GRUB_TIMEOUT"));
    navigation->setCurrentRow(5);
    QVERIFY(pages->currentWidget()->findChild<QWidget *>("recoveryForms"));
    QVERIFY(!window.findChild<QPushButton *>("showConfiguration"));
    window.resize(1040, 700); window.show(); QVERIFY(QTest::qWaitForWindowActive(&window));
    QVERIFY(window.grab().save(QDir::tempPath() + "/catos-window-recovery.png"));
    navigation->setCurrentRow(2);
    QVERIFY(window.grab().save(QDir::tempPath() + "/catos-window-configuration.png"));
}

void BootManagerWindowTest::queueBackupRestore()
{
    BootSession session;
    BootManagerWindow window(&session);
    QCoreApplication::processEvents();
    auto snapshot = settingsSnapshot();
    snapshot.backups = {QJsonObject{{"id", "11111111-2222-3333-4444-555555555555"}, {"created", "2026-11-07T10:00:00Z"},
        {"phase", "completed"}, {"operations", QJsonArray{"Timeout"}}, {"digest", "abc"},
        {"files", QJsonArray{QJsonObject{{"path", "/etc/default/grub"}, {"existed", true}}}}, {"bootOrder", QJsonArray{"0001"}}}};
    emit session.snapshotReady(snapshot);
    auto *list = window.findChild<QTreeWidget *>("configurationBackups"); QVERIFY(list);
    auto *restore = window.findChild<QPushButton *>("restoreBackup"); QVERIFY(restore);
    QCOMPARE(list->topLevelItemCount(), 1);
    QVERIFY(!restore->isEnabled());
    list->setCurrentItem(list->topLevelItem(0));
    QVERIFY(restore->isEnabled());
    QVERIFY(window.findChild<QLabel *>("backupDetails")->text().contains("/etc/default/grub"));
    restore->click();
    auto *pending = window.findChild<QListWidget *>("pendingOperations"); QVERIFY(pending);
    QCOMPARE(pending->count(), 1);
    auto *apply = window.findChild<QPushButton *>("applyTransaction");
    QVERIFY(apply->isEnabled());
    window.findChild<SettingsPage *>()->findChild<QLineEdit *>("GRUB_TIMEOUT")->setText("9");
    QCOMPARE(pending->count(), 2);
    QVERIFY(!apply->isEnabled());
}

void BootManagerWindowTest::readOnlyModeUntilAuthorized()
{
    BootSession session;
    int scans = 0;
    BootManagerWindow window(&session, nullptr, [&scans] { ++scans; return settingsSnapshot(); });
    window.setReadOnly(true);
    auto *banner = window.findChild<QFrame *>("readOnlyBanner"); QVERIFY(banner);
    QVERIFY(banner->isVisibleTo(&window));
    auto *settings = window.findChild<SettingsPage *>(); QVERIFY(settings);
    QTRY_VERIFY(settings->findChild<QLineEdit *>("GRUB_TIMEOUT"));
    QCOMPARE(scans, 1);
    settings->findChild<QLineEdit *>("GRUB_TIMEOUT")->setText("9");
    auto *pending = window.findChild<QListWidget *>("pendingOperations");
    QCOMPARE(pending->count(), 1);
    auto *apply = window.findChild<QPushButton *>("applyTransaction");
    QVERIFY(!apply->isEnabled());
    QVERIFY(!apply->toolTip().isEmpty());
    // Authorizing switches over and keeps the prepared change.
    emit session.authorized();
    QVERIFY(!window.isReadOnly());
    QVERIFY(!banner->isVisibleTo(&window));
    QCOMPARE(pending->count(), 1);
}

void BootManagerWindowTest::explainDisabledActions()
{
    BootSession session;
    BootManagerWindow window(&session);
    QCoreApplication::processEvents();
    auto snapshot = settingsSnapshot();
    snapshot.uefi = true;
    snapshot.bootCurrent = "0001";
    snapshot.bootOrder = {"0001", "0002"};
    snapshot.firmware = {{"0001", "Current", "\\EFI\\a.efi", "uuid", true, true}, {"0002", "Other", "\\EFI\\b.efi", "uuid", true, false}};
    emit session.snapshotReady(snapshot);
    auto *apply = window.findChild<QPushButton *>("applyTransaction");
    QVERIFY(!apply->isEnabled());
    QCOMPARE(apply->toolTip(), QString("There are no pending changes."));
    auto *table = window.findChild<FirmwarePage *>()->findChild<QTableWidget *>(); QVERIFY(table);
    table->selectRow(0);
    QPushButton *remove = nullptr;
    for (auto *button : window.findChild<FirmwarePage *>()->findChildren<QPushButton *>())
        if (button->text() == "Remove entry") remove = button;
    QVERIFY(remove);
    QVERIFY(!remove->isEnabled());
    QCOMPARE(remove->toolTip(), QString("The entry used to start this system cannot be removed."));
    table->selectRow(1);
    QVERIFY(remove->isEnabled());
    QVERIFY(remove->toolTip().isEmpty());
    auto *move = window.findChild<QPushButton *>("moveBootEntryDown");
    QVERIFY(!move->isEnabled());
    QVERIFY(!move->toolTip().isEmpty());
}

void BootManagerWindowTest::showMokCertificates()
{
    BootSession session;
    BootManagerWindow window(&session);
    QCoreApplication::processEvents();
    auto snapshot = settingsSnapshot();
    snapshot.uefi = true;
    snapshot.enrolledMok = "[key 1]\nSHA1 Fingerprint: 11:22\n        Issuer: CN = Vendor\n        Subject: CN = Vendor Key\n";
    snapshot.pendingMok = "[key 1]\nSHA1 Fingerprint: 33:44\n        Issuer: CN = Me\n        Subject: CN = My Key\n";
    emit session.snapshotReady(snapshot);
    auto *security = window.findChild<SecurityPage *>(); QVERIFY(security);
    auto *enrolled = security->findChild<QTreeWidget *>("enrolledCertificates"); QVERIFY(enrolled);
    auto *pending = security->findChild<QTreeWidget *>("pendingCertificates"); QVERIFY(pending);
    QCOMPARE(enrolled->topLevelItemCount(), 1);
    QCOMPARE(enrolled->topLevelItem(0)->text(0), QString("Vendor Key"));
    QCOMPARE(pending->topLevelItem(0)->text(3), QString("33:44"));
    QVERIFY(security->findChild<QLabel *>("mokGuidance")->text().contains("MokManager"));
    auto *revoke = security->findChild<QPushButton *>("revokeMokEnrollment");
    QVERIFY(revoke->isEnabled());
    QSignalSpy requested(security, &SecurityPage::mokRequested);
    QTimer::singleShot(0, &window, [] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
        box->button(QMessageBox::No)->click();
    });
    revoke->click();
    QCOMPARE(requested.count(), 0);
    snapshot.pendingMok.clear();
    emit session.snapshotReady(snapshot);
    QVERIFY(!revoke->isEnabled());
}

void BootManagerWindowTest::confirmHighRiskWrites()
{
    BootSession session;
    BootTransaction transaction;
    transaction.stage({"repair-install", "grub:bios:/", R"({"action":"repair","root":"/","mode":"bios","disk":"/dev/sda","targetLabel":"/dev/sda · 1T · Samsung","register":false,"fallback":false})", "", "Repair GRUB on /dev/sda"});
    bool checked = false;
    QTimer::singleShot(0, [&checked] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
        auto *warning = dialog->findChild<QLabel *>("riskWarning"); QVERIFY(warning);
        QVERIFY(warning->text().contains("/dev/sda · 1T · Samsung"));
        auto *confirm = dialog->findChild<QPushButton *>("confirmExecution"); QVERIFY(confirm);
        auto *understood = dialog->findChild<QCheckBox *>("confirmRisk"); QVERIFY(understood);
        QVERIFY(!confirm->isEnabled());
        understood->setChecked(true);
        checked = confirm->isEnabled();
        dialog->reject();
    });
    QCOMPARE(ApplyDialog::execute(nullptr, session, transaction, true), ApplyResult::Cancelled);
    QVERIFY(checked);
    BootTransaction safe;
    safe.stage({"repair-install", "grub:uefi:/", R"({"action":"repair","root":"/","mode":"uefi","esp":"/efi","register":true,"fallback":false})", "", "Repair GRUB"});
    QTimer::singleShot(0, [] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
        QVERIFY(!dialog->findChild<QCheckBox *>("confirmRisk"));
        QVERIFY(dialog->findChild<QPushButton *>("confirmExecution")->isEnabled());
        dialog->reject();
    });
    QCOMPARE(ApplyDialog::execute(nullptr, session, safe, true), ApplyResult::Cancelled);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (app.arguments().contains("--fixture-session")) {
        QFile input, output;
        if (!input.open(stdin, QIODevice::ReadOnly) || !output.open(stdout, QIODevice::WriteOnly)) return 2;
        output.write("{\"type\":\"ready\",\"version\":1,\"uid\":0}\n"); output.flush();
        for (;;) {
            const auto line = input.readLine();
            if (line.isEmpty()) return 0;
            const auto request = QJsonDocument::fromJson(line).object();
            if (request.value("action").toString() == "scan") {
                const QJsonObject response{{"type", "snapshot"},
                    {"data", QString::fromLatin1(BootSnapshotCodec::encode(settingsSnapshot()))}};
                output.write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n'); output.flush();
            }
            if (request.value("action").toString() == "preview") {
                const QJsonObject response{{"type", "preview"}, {"error", ""}, {"files", QJsonArray{QJsonObject{
                    {"path", "/etc/default/grub"}, {"before", "GRUB_TIMEOUT='3'\n"}, {"after", "GRUB_TIMEOUT='5'\n"},
                    {"existed", true}, {"removed", false}}}}};
                output.write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n'); output.flush();
            }
            if (request.value("action").toString() == "apply") {
                const QJsonObject response{{"type", "result"}, {"success", false},
                    {"code", qEnvironmentVariableIntValue("CATOS_WINDOW_TEST_CODE")},
                    {"outcomes", QJsonObject{{"grub-setting:GRUB_TIMEOUT", qEnvironmentVariableIntValue("CATOS_WINDOW_TEST_CODE") == 3 ? "not-executed" : "failed"}}}, {"reportDirectory", ""}};
                output.write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n'); output.flush();
            }
        }
    }
    BootManagerWindowTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "BootManagerWindowTest.moc"
