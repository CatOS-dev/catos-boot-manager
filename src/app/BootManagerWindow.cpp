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

#include <QApplication>
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
#include <QStackedWidget>
#include <QTimer>
#include <QStatusBar>
#include <QVBoxLayout>

BootManagerWindow::BootManagerWindow(BootSession *session, QWidget *parent) : QMainWindow(parent), session_(session)
{
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
    navigation_->addItems({tr("Overview"), tr("Boot methods"), tr("Boot menu"), tr("Configuration"),
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
    pages_ = new QStackedWidget(right);
    overview_ = new OverviewPage(pages_);
    methods_ = new MethodsPage(pages_);
    security_ = new SecurityPage(pages_);
    menu_ = new MenuPage(pages_);
    settings_ = new SettingsPage(pages_);
    firmware_ = new FirmwarePage(pages_);
    recovery_ = new RecoveryPage(pages_);
    pendingPage_ = new PendingPage(pages_);
    for (QWidget *page : std::initializer_list<QWidget *>{overview_, methods_, menu_, settings_, firmware_, security_, recovery_, pendingPage_})
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
        if (session_->isBusy()) return;
        BootTransaction installation;
        installation.stage(operation);
        const auto result = ApplyDialog::execute(this, *session_, installation, true);
        if (result != ApplyResult::Cancelled && session_->isAuthorized()) rescan(true);
    };
    connect(methods_, &MethodsPage::installationRequested, this, executeNow);
    connect(recovery_, &RecoveryPage::installationRequested, this, executeNow);
    connect(recovery_, &RecoveryPage::operationRequested, this, &BootManagerWindow::queue);
    connect(firmware_, &FirmwarePage::operationRequested, this, &BootManagerWindow::queue);
    connect(pendingPage_, &PendingPage::removalRequested, this, [this](const QString &key) {
        for (const auto &operation : transaction_.operations())
            if (operation.key() == key) settings_->discardOperation(operation);
        transaction_.remove(key);
        updateFooter();
    });
    auto *refreshShortcut = new QShortcut(QKeySequence::Refresh, this);
    connect(refreshShortcut, &QShortcut::activated, this, &BootManagerWindow::refresh);
    connect(session_, &BootSession::snapshotReady, this, [this](const BootSnapshot &snapshot) {
        snapshot_ = snapshot;
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
    });
    navigation_->setCurrentRow(0);
    updateFooter();
    QTimer::singleShot(0, this, &BootManagerWindow::refresh);
}

void BootManagerWindow::refresh()
{
    if (session_->isBusy()) return;
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
    session_->scan();
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
    pending_->setText(settings_->hasInvalidDrafts() ? tr("Correct invalid settings before applying.") : session_->isBusy() ? tr("Detecting boot state…")
        : count ? tr("%1 pending change(s)").arg(count) : tr("No pending changes"));
    apply_->setEnabled(!session_->isBusy() && count > 0 && conflict.isEmpty() && !settings_->hasInvalidDrafts());
    apply_->setToolTip(conflict);
    discard_->setEnabled(!session_->isBusy() && hasChanges());
}

void BootManagerWindow::applyTransaction()
{
    if (session_->isBusy() || settings_->hasInvalidDrafts() || !BootPlan::firmwareConflict(transaction_.operations(), snapshot_).isEmpty()) return;
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
    if (session_->isBusy()) {
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
