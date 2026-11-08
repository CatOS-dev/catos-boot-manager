#include "BootManagerWindow.h"
#include "model/BootPlan.h"
#include "dialogs/ApplyDialog.h"
#include "pages/FirmwarePage.h"
#include "pages/MenuPage.h"
#include "pages/OverviewPage.h"
#include "pages/MethodsPage.h"
#include "pages/SecurityPage.h"
#include "pages/PendingPage.h"
#include "pages/RecoveryPage.h"
#include "pages/SettingsPage.h"
#include "services/BootSession.h"
#include "services/BootProbe.h"
#include "services/BootScan.h"

#include <QApplication>
#include <QEventLoop>
#include <QJsonObject>
#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QShortcut>
#include "widgets/LoaderSelector.h"
#include "widgets/BootUi.h"
#include <QStackedWidget>
#include <QTimer>
#include <QStatusBar>
#include <QVBoxLayout>

BootManagerWindow::BootManagerWindow(BootSession *session, QWidget *parent, std::function<BootSnapshot()> localInspect)
    : QMainWindow(parent), session_(session)
{
    localScan_ = new BootScan(localInspect ? std::move(localInspect) : std::function<BootSnapshot()>([] { return BootProbe::inspect(); }), this);
    setWindowTitle(tr("CatOS Boot Manager"));
    resize(1040, 700);
    setMinimumSize(760, 520);
    setWindowIcon(QIcon::fromTheme(QStringLiteral("catos-logo"),
                                   QIcon::fromTheme(QStringLiteral("drive-harddisk"))));

    auto *root = new QWidget(this);
    auto *horizontal = new QHBoxLayout(root);
    horizontal->setContentsMargins(0, 0, 0, 0);
    horizontal->setSpacing(0);

    auto *sidebar = new QFrame(root);
    sidebar->setFrameShape(QFrame::NoFrame);
    sidebar->setFixedWidth(218);
    auto sidePalette = sidebar->palette();
    sidePalette.setColor(QPalette::Window, sidePalette.color(QPalette::AlternateBase));
    sidebar->setPalette(sidePalette);
    sidebar->setAutoFillBackground(true);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(14, 20, 14, 16);
    auto *brand = new QLabel(tr("CatOS Boot Manager"), sidebar);
    brand->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    auto font = brand->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 5);
    brand->setFont(font);
    brand->setWordWrap(true);
    sideLayout->addWidget(brand);
    navigation_ = new QListWidget(sidebar);
    navigation_->setObjectName(QStringLiteral("navigation"));
    navigation_->addItems({tr("Overview"), tr("Boot methods"), tr("Boot menu"),
                           tr("UEFI entries"), tr("Secure Boot"), tr("Repair and recovery"), tr("Pending changes")});
    navigation_->setFrameShape(QFrame::NoFrame);
    navigation_->setSpacing(4);
    navigation_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigation_->setTextElideMode(Qt::ElideRight);
    navigation_->setWordWrap(true);
    navigation_->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { padding: 10px 12px; border-radius: 8px; }"
        "QListWidget::item:selected { background: palette(highlight); color: palette(highlighted-text); }"));
    sideLayout->addWidget(navigation_);
    refreshButton_ = new QPushButton(tr("Refresh detection"), sidebar);
    refreshButton_->setObjectName("refreshDetection");
    refreshButton_->setFlat(true);
    sideLayout->addWidget(refreshButton_);
    horizontal->addWidget(sidebar);

    auto *right = new QWidget(root);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    readOnlyBanner_ = new QFrame(right);
    readOnlyBanner_->setObjectName(QStringLiteral("readOnlyBanner"));
    readOnlyBanner_->setFrameShape(QFrame::StyledPanel);
    auto *bannerLayout = new QHBoxLayout(readOnlyBanner_);
    bannerLayout->setContentsMargins(28, 8, 34, 8);
    auto *bannerText = new QLabel(tr("Read-only mode: administrator access was not granted. Information may be incomplete, "
        "and changes can be prepared but not applied."), readOnlyBanner_);
    bannerText->setWordWrap(true);
    bannerLayout->addWidget(bannerText, 1);
    auto *authorize = new QPushButton(tr("Authorize"), readOnlyBanner_);
    authorize->setObjectName(QStringLiteral("authorizeSession"));
    bannerLayout->addWidget(authorize);
    readOnlyBanner_->hide();
    rightLayout->addWidget(readOnlyBanner_);
    connect(authorize, &QPushButton::clicked, session_, &BootSession::start);
    pages_ = new QStackedWidget(right);
    pages_->setObjectName(QStringLiteral("pages"));
    overview_ = new OverviewPage(pages_);
    methods_ = new MethodsPage(pages_);
    security_ = new SecurityPage(pages_);
    // Menu entries and loader configuration describe the same boot menu, so
    // one page shows the menu with its configuration below it.
    auto *menuPage = new QWidget(pages_);
    menuPage->setObjectName(QStringLiteral("bootMenuPage"));
    auto *menuLayout = BootUi::page(menuPage);
    auto *menuContent = menuLayout->parentWidget();
    BootUi::title(menuLayout, menuContent, tr("Boot menu"),
                  tr("Edit boot entries and boot loader settings. Changes are staged until you apply them."));
    menu_ = new MenuPage(menuContent, true);
    settings_ = new SettingsPage(menuContent, true);
    menuLayout->addWidget(menu_);
    menuLayout->addWidget(settings_);
    menuLayout->addStretch();
    firmware_ = new FirmwarePage(pages_);
    recovery_ = new RecoveryPage(pages_);
    pendingPage_ = new PendingPage(pages_);
    for (QWidget *page : std::initializer_list<QWidget *>{overview_, methods_, menuPage, firmware_, security_, recovery_, pendingPage_})
        pages_->addWidget(page);
    rightLayout->addWidget(pages_, 1);
    footer_ = new QFrame(right);
    footer_->setFrameShape(QFrame::StyledPanel);
    auto *footerLayout = new QHBoxLayout(footer_);
    footerLayout->setContentsMargins(28, 10, 34, 10);
    pending_ = new QLabel(footer_);
    pending_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    footerLayout->addWidget(pending_, 1);
    discard_ = new QPushButton(tr("Discard all changes"), footer_);
    apply_ = new QPushButton(tr("Review and apply"), footer_);
    discard_->setObjectName("discardAllChanges");
    footerLayout->addWidget(discard_);
    apply_->setObjectName("applyTransaction");
    footerLayout->addWidget(apply_);
    rightLayout->addWidget(footer_);
    horizontal->addWidget(right, 1);
    setCentralWidget(root);

    connect(navigation_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    connect(refreshButton_, &QPushButton::clicked, this, &BootManagerWindow::refresh);
    connect(discard_, &QPushButton::clicked, this, [this] {
        if (QMessageBox::warning(this, tr("Discard all changes"),
            tr("Discard all %1 pending change(s) and any invalid edits?").arg(transaction_.count()),
            QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) return;
        transaction_.clear();
        menu_->updateSnapshot(snapshot_);
        settings_->updateSnapshot(snapshot_);
        firmware_->updateSnapshot(snapshot_);
        settings_->selector()->selectInstallation(menu_->selector()->method(), menu_->selector()->currentId());
        updateFooter();
    });
    connect(settings_, &SettingsPage::draftStateChanged, this, &BootManagerWindow::updateFooter);
    connect(apply_, &QPushButton::clicked, this, &BootManagerWindow::applyTransaction);
    connect(menu_, &MenuPage::operationRequested, this, &BootManagerWindow::queue);
    // Default entries chosen in the menu are settings too; keep the draft in step.
    connect(menu_, &MenuPage::operationRequested, settings_, &SettingsPage::reflectOperation);
    connect(settings_, &SettingsPage::operationRequested, this, &BootManagerWindow::queue);
    const QList<LoaderSelector *> selectors{methods_->selector(), menu_->selector(), settings_->selector(),
                                            security_->selector(), recovery_->selector()};
    for (auto *source : selectors)
        for (auto *target : selectors)
            if (source != target)
                connect(source, &LoaderSelector::installationChanged, this, [this, target](LoaderKind kind, const QString &id) {
                    if (!updatingSnapshot_) target->selectInstallation(kind, id);
                });
    connect(methods_, &MethodsPage::operationRequested, this, &BootManagerWindow::queue);
    const auto executeNow = [this](const BootOperation &operation) {
        if (isBusy()) return;
        if (readOnly_) {
            QMessageBox::information(this, tr("Administrator access required"),
                tr("This action needs administrator access. Use Authorize at the top of the window, then try again."));
            return;
        }
        BootTransaction installation;
        installation.stage(operation);
        const auto result = ApplyDialog::execute(this, *session_, installation, true);
        if (result != ApplyResult::Cancelled && session_->isAuthorized()) rescan(true);
    };
    connect(methods_, &MethodsPage::installationRequested, this, executeNow);
    connect(recovery_, &RecoveryPage::installationRequested, this, executeNow);
    connect(recovery_, &RecoveryPage::operationRequested, this, &BootManagerWindow::queue);
    connect(firmware_, &FirmwarePage::operationRequested, this, &BootManagerWindow::queue);
    connect(security_, &SecurityPage::mokRequested, this, &BootManagerWindow::runMokRequest);
    connect(pendingPage_, &PendingPage::removalRequested, this, [this](const QString &key) {
        for (const auto &operation : transaction_.operations())
            if (operation.key() == key) settings_->discardOperation(operation);
        transaction_.remove(key);
        updateFooter();
    });
    auto *refreshShortcut = new QShortcut(QKeySequence::Refresh, this);
    connect(refreshShortcut, &QShortcut::activated, this, &BootManagerWindow::refresh);
    connect(session_, &BootSession::snapshotReady, this, &BootManagerWindow::showSnapshot);
    connect(localScan_, &BootScan::completed, this, &BootManagerWindow::showSnapshot);
    connect(session_, &BootSession::scanProgress, this, [this](const QString &stage) {
        scanStage_ = stage;
        updateFooter();
    });
    connect(session_, &BootSession::authorized, this, [this] {
        if (!readOnly_) return;
        setReadOnly(false);
        if (!localScan_->isRunning()) rescan(true);
    });
    navigation_->setCurrentRow(0);
    updateFooter();
    QTimer::singleShot(0, this, &BootManagerWindow::refresh);
}

void BootManagerWindow::setReadOnly(bool readOnly)
{
    readOnly_ = readOnly;
    readOnlyBanner_->setVisible(readOnly);
    updateFooter();
}

void BootManagerWindow::runMokRequest(const QJsonObject &request, const QString &description)
{
    if (readOnly_) {
        QMessageBox::information(this, tr("Administrator access required"),
            tr("This action needs administrator access. Use Authorize at the top of the window, then try again."));
        return;
    }
    if (isBusy()) return;
    QString log;
    bool success = false;
    QEventLoop loop;
    const auto output = connect(session_, &BootSession::output, this, [&log](const QString &text) { log += text; });
    const auto finished = connect(session_, &BootSession::operationFinished, &loop, [&](bool ok) { success = ok; loop.quit(); });
    const auto lost = connect(session_, &BootSession::failed, &loop, &QEventLoop::quit);
    const bool started = session_->secureBoot(request);
    if (started) {
        pages_->setEnabled(false);
        statusBar()->showMessage(description + QStringLiteral("…"));
        loop.exec();
        statusBar()->clearMessage();
        pages_->setEnabled(true);
    }
    disconnect(output); disconnect(finished); disconnect(lost);
    if (!started && !session_->isAuthorized()) return;
    QMessageBox result(success ? QMessageBox::Information : QMessageBox::Warning, tr("Secure Boot"),
        success ? (request.value(QStringLiteral("operation")).toString() == QLatin1String("import")
                      ? tr("The enrollment request was saved. Restart and confirm it in MokManager with the password you set.")
                      : tr("The pending enrollment was cancelled."))
                : tr("%1 did not complete.").arg(description), QMessageBox::Close, this);
    if (!log.isEmpty()) result.setDetailedText(log);
    result.exec();
    if (session_->isAuthorized()) rescan(true);
}

bool BootManagerWindow::isBusy() const
{
    return session_->isBusy() || localScan_->isRunning();
}

void BootManagerWindow::showSnapshot(const BootSnapshot &snapshot)
{
    snapshot_ = snapshot;
    scanStage_.clear();
    overview_->updateSnapshot(snapshot_);
    updatingSnapshot_ = true;
    methods_->updateSnapshot(snapshot_);
    security_->updateSnapshot(snapshot_);
    menu_->updateSnapshot(snapshot_, preserveDrafts_);
    settings_->updateSnapshot(snapshot_, preserveDrafts_);
    recovery_->updateSnapshot(snapshot_);
    menu_->selector()->selectInstallation(methods_->selector()->method(), methods_->selector()->currentId());
    security_->selector()->selectInstallation(methods_->selector()->method(), methods_->selector()->currentId());
    recovery_->selector()->selectInstallation(methods_->selector()->method(), methods_->selector()->currentId());
    settings_->selector()->selectInstallation(menu_->selector()->method(), menu_->selector()->currentId());
    for (const auto &operation : transaction_.operations()) settings_->reflectOperation(operation);
    updatingSnapshot_ = false;
    firmware_->updateSnapshot(snapshot_);
    pages_->setEnabled(true);
    refreshButton_->setEnabled(true);
    updateFooter();
}

void BootManagerWindow::refresh()
{
    if (isBusy()) return;
    if (hasChanges() && QMessageBox::question(this, tr("Refresh detection"),
        tr("Refreshing will discard your pending changes and drafts. Continue?")) != QMessageBox::Yes) return;
    transaction_.clear();
    rescan();
}

void BootManagerWindow::rescan(bool preserveDrafts)
{
    preserveDrafts_ = preserveDrafts;
    pages_->setEnabled(false);
    refreshButton_->setEnabled(false);
    scanStage_.clear();
    if (readOnly_) localScan_->start();
    else session_->scan();
    updateFooter();
}

void BootManagerWindow::queue(const BootOperation &operation)
{
    transaction_.stage(operation);
    updateFooter();
}

void BootManagerWindow::updateFooter()
{
    const int count = transaction_.count();
    const QString conflict = BootPlan::firmwareConflict(transaction_.operations(), snapshot_);
    pendingPage_->updateTransaction(transaction_, snapshot_);
    firmware_->updateTransaction(transaction_);
    methods_->updateTransaction(transaction_);
    menu_->updateTransaction(transaction_);
    const bool scanning = localScan_->isRunning() || session_->isBusy();
    pending_->setText(settings_->hasInvalidDrafts() ? tr("Correct invalid settings before applying.")
        : scanning ? (scanStage_.isEmpty() ? tr("Detecting boot state…") : tr("Detecting boot state: %1…").arg(scanStage_))
        : count ? tr("%1 pending change(s)").arg(count) : tr("No pending changes"));
    const QString reason = readOnly_ ? tr("Authorize to apply changes.")
        : scanning ? tr("Wait for detection to finish.")
        : !count ? tr("There are no pending changes.")
        : !conflict.isEmpty() ? conflict
        : tr("Correct invalid settings before applying.");
    BootUi::setAvailable(apply_, !readOnly_ && !scanning && count > 0 && conflict.isEmpty() && !settings_->hasInvalidDrafts(), reason);
    BootUi::setAvailable(discard_, !scanning && hasChanges(), scanning ? tr("Wait for detection to finish.") : tr("There are no pending changes."));
}

void BootManagerWindow::applyTransaction()
{
    if (readOnly_ || isBusy() || settings_->hasInvalidDrafts() || !BootPlan::firmwareConflict(transaction_.operations(), snapshot_).isEmpty()) return;
    const auto result = ApplyDialog::execute(this, *session_, transaction_);
    if (result == ApplyResult::Cancelled) return;
    if (result == ApplyResult::Failed) {
        QMessageBox failure(QMessageBox::Warning, tr("Boot changes incomplete"), session_->lastApplyCode() == 2 || session_->lastApplyCode() == 3
            ? tr("No changes were applied. Pending changes and drafts have been retained.")
            : tr("Some changes may have been applied. Pending changes and drafts have been retained. Check the execution report before retrying."),
            QMessageBox::Close, this);
        auto *detect = failure.addButton(tr("Detect current state"), QMessageBox::ActionRole);
        detect->setEnabled(session_->isAuthorized());
        failure.exec();
        if (failure.clickedButton() == detect) rescan(true);
        return;
    }
    settings_->acknowledgeApplied(transaction_);
    transaction_.clear();
    rescan(true);
}

bool BootManagerWindow::hasChanges() const
{
    return transaction_.count() || settings_->hasDraftChanges();
}

void BootManagerWindow::closeEvent(QCloseEvent *event)
{
    if (isBusy()) {
        statusBar()->showMessage(tr("Boot detection or execution is still running. Please wait before closing."), 8000);
        event->ignore();
        return;
    }
    if (!hasChanges()) { event->accept(); return; }
    QMessageBox confirm(QMessageBox::Warning, tr("Unsaved changes"),
        tr("There are %1 pending change(s). Apply them before closing, or discard all changes and invalid edits.").arg(transaction_.count()),
        QMessageBox::Discard | QMessageBox::Cancel, this);
    QPushButton *review = nullptr;
    if (apply_->isEnabled()) review = confirm.addButton(tr("Review and apply"), QMessageBox::ActionRole);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.exec();
    if (confirm.standardButton(confirm.clickedButton()) == QMessageBox::Discard) { event->accept(); return; }
    if (review && confirm.clickedButton() == review) {
        applyTransaction();
        if (!hasChanges()) { event->accept(); return; }
    }
    event->ignore();
}
