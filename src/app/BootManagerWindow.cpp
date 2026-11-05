#include "BootManagerWindow.h"
#include "model/BootPlan.h"
#include "dialogs/ApplyDialog.h"
#include "pages/FirmwarePage.h"
#include "pages/MenuPage.h"
#include "pages/OverviewPage.h"
#include "pages/PendingPage.h"
#include "pages/RepairPage.h"
#include "pages/SettingsPage.h"
#include "services/BootProbe.h"
#include "services/BootScan.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

BootManagerWindow::BootManagerWindow(QWidget *parent) : QMainWindow(parent)
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
    auto font = brand->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 5);
    brand->setFont(font);
    brand->setWordWrap(true);
    sideLayout->addWidget(brand);
    navigation_ = new QListWidget(sidebar);
    navigation_->setObjectName(QStringLiteral("navigation"));
    navigation_->addItems({tr("Overview"), tr("Boot menu"), tr("Loader settings"),
                           tr("UEFI entries"), tr("Rebuild & update"), tr("Pending changes")});
    navigation_->setFrameShape(QFrame::NoFrame);
    navigation_->setSpacing(7);
    navigation_->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { padding: 10px 12px; border-radius: 8px; }"
        "QListWidget::item:selected { background: palette(highlight); color: palette(highlighted-text); }"));
    sideLayout->addWidget(navigation_);
    refreshButton_ = new QPushButton(tr("Refresh detection"), sidebar);
    refreshButton_->setFlat(true);
    sideLayout->addWidget(refreshButton_);
    horizontal->addWidget(sidebar);

    auto *right = new QWidget(root);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    pages_ = new QStackedWidget(right);
    overview_ = new OverviewPage(pages_);
    menu_ = new MenuPage(pages_);
    settings_ = new SettingsPage(pages_);
    firmware_ = new FirmwarePage(pages_);
    repair_ = new RepairPage(pages_);
    pendingPage_ = new PendingPage(pages_);
    for (QWidget *page : {static_cast<QWidget *>(overview_), static_cast<QWidget *>(menu_),
                          static_cast<QWidget *>(settings_), static_cast<QWidget *>(firmware_),
                          static_cast<QWidget *>(repair_), static_cast<QWidget *>(pendingPage_)})
        pages_->addWidget(page);
    rightLayout->addWidget(pages_, 1);
    footer_ = new QFrame(right);
    footer_->setFrameShape(QFrame::StyledPanel);
    auto *footerLayout = new QHBoxLayout(footer_);
    footerLayout->setContentsMargins(28, 10, 34, 10);
    pending_ = new QLabel(footer_);
    footerLayout->addWidget(pending_, 1);
    discard_ = new QPushButton(tr("Discard"), footer_);
    apply_ = new QPushButton(tr("Review and apply"), footer_);
    footerLayout->addWidget(discard_);
    footerLayout->addWidget(apply_);
    rightLayout->addWidget(footer_);
    horizontal->addWidget(right, 1);
    setCentralWidget(root);

    connect(navigation_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    connect(refreshButton_, &QPushButton::clicked, this, &BootManagerWindow::refresh);
    connect(discard_, &QPushButton::clicked, this, [this] {
        transaction_.clear();
        settings_->updateSnapshot(snapshot_);
        updateFooter();
    });
    connect(apply_, &QPushButton::clicked, this, &BootManagerWindow::applyTransaction);
    connect(menu_, &MenuPage::operationRequested, this, &BootManagerWindow::queue);
    connect(menu_, &MenuPage::methodChanged, settings_, &SettingsPage::selectMethod);
    connect(settings_, &SettingsPage::methodChanged, menu_, &MenuPage::selectMethod);
    connect(settings_, &SettingsPage::operationRequested, this, &BootManagerWindow::queue);
    connect(firmware_, &FirmwarePage::operationRequested, this, &BootManagerWindow::queue);
    connect(repair_, &RepairPage::operationRequested, this, &BootManagerWindow::queue);
    connect(pendingPage_, &PendingPage::removalRequested, this, [this](const QString &key) {
        for (const auto &operation : transaction_.operations())
            if (operation.key() == key) settings_->discardOperation(operation);
        transaction_.remove(key);
        updateFooter();
    });
    auto *refreshShortcut = new QShortcut(QKeySequence::Refresh, this);
    connect(refreshShortcut, &QShortcut::activated, this, &BootManagerWindow::refresh);
    scan_ = new BootScan(this);
    connect(scan_, &BootScan::completed, this, [this](const BootSnapshot &snapshot) {
        snapshot_ = snapshot;
        overview_->updateSnapshot(snapshot_);
        menu_->updateSnapshot(snapshot_);
        settings_->updateSnapshot(snapshot_);
        firmware_->updateSnapshot(snapshot_);
        repair_->updateSnapshot(snapshot_);
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
    if (scan_->isRunning()) return;
    if (transaction_.count() && QMessageBox::question(this, tr("Refresh detection"),
        tr("Refreshing will discard your pending changes. Continue?")) != QMessageBox::Yes) return;
    transaction_.clear();
    pages_->setEnabled(false);
    refreshButton_->setEnabled(false);
    scan_->start();
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
    pending_->setText(scan_->isRunning() ? tr("Detecting boot state…")
        : count ? tr("%1 pending change(s)").arg(count) : tr("No pending changes"));
    apply_->setEnabled(!scan_->isRunning() && count > 0 && conflict.isEmpty());
    apply_->setToolTip(conflict);
    discard_->setEnabled(!scan_->isRunning() && count > 0);
}

void BootManagerWindow::applyTransaction()
{
    if (scan_->isRunning() || !BootPlan::firmwareConflict(transaction_.operations(), snapshot_).isEmpty()) return;
    const auto result = ApplyDialog::execute(this, transaction_);
    if (result == ApplyResult::Cancelled) return;
    transaction_.clear();
    refresh();
    if (result == ApplyResult::Failed)
        QMessageBox::warning(this, tr("Boot changes incomplete"),
            tr("The operation did not finish. System state is being reloaded; review the log before making another change."));
}
