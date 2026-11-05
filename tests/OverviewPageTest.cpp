#include "pages/OverviewPage.h"

#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QTranslator>
#include <QtTest>

namespace {
BootSnapshot detectedLoaders()
{
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.currentLoader = "GRUB";
    snapshot.currentEvidence = "Firmware path matched";
    BootInstallation grub;
    grub.kind = LoaderKind::Grub;
    grub.id = "grub-efi";
    grub.esp = "/boot/efi";
    grub.espPartuuid = "756fb40c-0c77-4a04-a3d9-36bb53ad3858";
    grub.binary = "/boot/efi/EFI/ARCH/grubx64.efi";
    grub.binaryPresent = true;
    grub.current = true;
    grub.evidence = "Named EFI program; bootability has not been verified";
    BootInstallation limine;
    limine.kind = LoaderKind::Limine;
    limine.id = "limine-efi";
    limine.esp = grub.esp;
    limine.espPartuuid = grub.espPartuuid;
    limine.binary = "/boot/efi/EFI/limine/limine_x64.efi";
    limine.binaryPresent = true;
    limine.config = "/boot/efi/limine.conf";
    limine.configPresent = true;
    BootInstallation grubConfig;
    grubConfig.kind = LoaderKind::Grub;
    grubConfig.id = "grub-config";
    grubConfig.config = "/etc/default/grub";
    grubConfig.configPresent = true;
    snapshot.installations = {grub, limine, grubConfig};
    return snapshot;
}
}

class OverviewPageTest : public QObject {
    Q_OBJECT
private slots:
    void mergeProgramAndConfigurationAndKeepDetailsCopyable();
    void countOnlyProgramLocationsAndKeepIssuesVisible();
};

void OverviewPageTest::mergeProgramAndConfigurationAndKeepDetailsCopyable()
{
    QTranslator translator;
    QVERIFY(translator.load(QCoreApplication::applicationDirPath() + "/catos-boot-manager_zh_CN.qm"));
    QApplication::installTranslator(&translator);
    const auto snapshot = detectedLoaders();
    OverviewPage page;
    page.resize(1040, 700);
    page.updateSnapshot(snapshot);
    page.show();
    QVERIFY(QTest::qWaitForWindowActive(&page));
    const auto names = page.findChildren<QLabel *>("overviewLoaderName");
    QCOMPARE(names.size(), 2);
    QCOMPARE(names[0]->text(), QString("GRUB"));
    QCOMPARE(names[1]->text(), QString("Limine"));
    auto *grub = page.findChild<QWidget *>("overviewLoader0");
    QVERIFY(grub);
    auto *status = grub->findChild<QLabel *>("overviewLoaderStatus");
    auto *details = grub->findChild<QPlainTextEdit *>("overviewLoaderDetails");
    auto *toggle = grub->findChild<QToolButton *>("overviewLoaderDetailsToggle");
    QVERIFY(status);
    QVERIFY(details);
    QVERIFY(toggle);
    QCOMPARE(status->text(), QString("已发现程序和配置 · 本次启动"));
    QVERIFY(details->isReadOnly());
    QVERIFY(!details->isVisible());
    QVERIFY(details->toPlainText().contains(snapshot.installations[0].binary));
    QVERIFY(details->toPlainText().contains("/etc/default/grub"));
    QVERIFY(details->toPlainText().contains(snapshot.installations[0].espPartuuid));
    QVERIFY(page.grab().save("/tmp/catos-overview-compact.png"));
    toggle->click();
    QVERIFY(details->isVisible());
    details->setFocus();
    details->selectAll();
    QApplication::clipboard()->clear();
    QTest::keySequence(details, QKeySequence::Copy);
    QCOMPARE(QApplication::clipboard()->text(), details->toPlainText());
    QVERIFY(page.grab().save("/tmp/catos-overview-details.png"));
    toggle->click();
    QVERIFY(!details->isVisible());
    page.updateSnapshot(snapshot);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(page.findChildren<QLabel *>("overviewLoaderName").size(), 2);
    QVERIFY(!page.findChild<QPlainTextEdit *>("overviewLoaderDetails")->isVisible());
    QApplication::removeTranslator(&translator);
}

void OverviewPageTest::countOnlyProgramLocationsAndKeepIssuesVisible()
{
    auto snapshot = detectedLoaders();
    BootInstallation second = snapshot.installations[0];
    second.id = "grub-second-esp";
    second.esp = "/efi";
    second.binary = "/efi/EFI/GRUB/grubx64.efi";
    second.current = false;
    second.issues = {"Firmware entry missing"};
    snapshot.installations.append(second);
    OverviewPage page;
    page.updateSnapshot(snapshot);
    auto *grub = page.findChild<QWidget *>("overviewLoader0");
    QVERIFY(grub);
    auto *status = grub->findChild<QLabel *>("overviewLoaderStatus");
    auto *details = grub->findChild<QPlainTextEdit *>("overviewLoaderDetails");
    QVERIFY(status);
    QVERIFY(details);
    QCOMPARE(status->text(), QString("Program and configuration detected · Current boot · 2 installation locations · Needs attention (1)"));
    QVERIFY(details->toPlainText().contains(second.binary));
    QVERIFY(details->toPlainText().contains("Firmware entry missing"));
    QVERIFY(!details->isVisible());
}

QTEST_MAIN(OverviewPageTest)
#include "OverviewPageTest.moc"
