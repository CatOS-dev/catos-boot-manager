#include "app/BootManagerWindow.h"
#include "pages/MenuPage.h"
#include "pages/SettingsPage.h"
#include "pages/PendingPage.h"
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
#include <QtTest>

class BootManagerWindowTest : public QObject {
    Q_OBJECT
private slots:
    void protectDraftsOnRefreshAndClose();
    void retainFailedTransaction_data();
    void retainFailedTransaction();
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
        for (auto *button : dialog->findChildren<QPushButton *>())
            if ((button->text() == "Apply" || button->text() == "Close") && button->isEnabled()) {
                button->click(); return;
            }
    });
    modal.start(10);
    window.findChild<QPushButton *>("applyTransaction")->click();
    modal.stop();
    QVERIFY(sawWarning);
    QCOMPARE(session.lastApplyCode(), code);
    QCOMPARE(pending->count(), 1);
    QVERIFY(window.findChild<QPushButton *>("applyTransaction")->isEnabled());
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
            if (request.value("action").toString() == "apply") {
                const QJsonObject response{{"type", "result"}, {"success", false},
                    {"code", qEnvironmentVariableIntValue("CATOS_WINDOW_TEST_CODE")}};
                output.write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n'); output.flush();
            }
        }
    }
    BootManagerWindowTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "BootManagerWindowTest.moc"
