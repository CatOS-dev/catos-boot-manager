#include "RecoveryPage.h"
#include "RepairPage.h"
#include "services/BootPackages.h"
#include "widgets/BootUi.h"
#include "widgets/LoaderSelector.h"

#include <QDateTime>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QTreeWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

RecoveryPage::RecoveryPage(QWidget *parent) : QWidget(parent)
{
    layout_ = BootUi::page(this);
    auto *content = layout_->parentWidget();
    BootUi::title(layout_, content, tr("Repair and recovery"),
        tr("Check installed boot loaders and repair them, including systems that are not currently running."));
    selector_ = new LoaderSelector(content);
    selector_->setObjectName(QStringLiteral("recoveryInstallation"));
    layout_->addWidget(selector_);
    BootUi::section(layout_, content, tr("Diagnosis"));
    diagnostics_ = BootUi::muted({}, content);
    diagnostics_->setObjectName(QStringLiteral("recoveryDiagnostics"));
    diagnostics_->setTextFormat(Qt::PlainText);
    layout_->addWidget(diagnostics_);
    BootUi::section(layout_, content, tr("Repair"));
    layout_->addWidget(BootUi::muted(tr("To repair a system that is not running, choose “Mount for repair” as the target system. "
        "Its root partition and EFI system partition are mounted temporarily and unmounted afterwards."), content));
    forms_ = new QStackedWidget(content);
    forms_->setObjectName(QStringLiteral("recoveryForms"));
    for (const auto kind : loaderKinds) {
        auto *repair = new RepairPage(kind, forms_, RepairPage::Mode::Repair);
        repair->setObjectName(QStringLiteral("recoveryForm"));
        repair->bootEntryButton()->hide();
        forms_->addWidget(repair);
        repairs_.insert(static_cast<int>(kind), repair);
        connect(repair, &RepairPage::installationRequested, this, &RecoveryPage::installationRequested);
    }
    layout_->addWidget(forms_);

    BootUi::section(layout_, content, tr("Configuration backups"));
    layout_->addWidget(BootUi::muted(tr("Before applying changes, the original configuration files are saved. Restoring writes them back "
        "and removes files the change created. Firmware entries, generated menus and boot images are not changed; "
        "rebuild them afterwards if needed."), content));
    backups_ = new QTreeWidget(content);
    backups_->setObjectName(QStringLiteral("configurationBackups"));
    backups_->setHeaderLabels({tr("Time"), tr("Result"), tr("Changes")});
    backups_->setRootIsDecorated(false);
    backups_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    backups_->setMinimumHeight(140);
    BootUi::enableCopy(backups_);
    layout_->addWidget(backups_);
    backupDetails_ = BootUi::muted({}, content);
    backupDetails_->setObjectName(QStringLiteral("backupDetails"));
    backupDetails_->setTextFormat(Qt::PlainText);
    layout_->addWidget(backupDetails_);
    restore_ = new QPushButton(tr("Restore configuration files"), content);
    restore_->setObjectName(QStringLiteral("restoreBackup"));
    restore_->setEnabled(false);
    layout_->addWidget(restore_, 0, Qt::AlignLeft);
    layout_->addStretch();
    connect(backups_, &QTreeWidget::currentItemChanged, this, &RecoveryPage::showBackup);
    connect(restore_, &QPushButton::clicked, this, [this] {
        const auto *item = backups_->currentItem();
        if (!item) return;
        const auto backup = item->data(0, Qt::UserRole).toJsonObject();
        emit operationRequested({QStringLiteral("restore-backup"), backup.value("id").toString(), {},
            backup.value("digest").toString(), tr("Restore configuration files from %1").arg(item->text(0))});
    });
    connect(selector_, &LoaderSelector::selectionChanged, this, &RecoveryPage::showInstance);
    showInstance();
}

void RecoveryPage::updateSnapshot(const BootSnapshot &snapshot)
{
    snapshot_ = snapshot;
    for (auto *repair : repairs_) repair->updateTargets(snapshot);
    selector_->updateSnapshot(snapshot);
    showBackups();
}

void RecoveryPage::showBackups()
{
    const QString previous = backups_->currentItem() ? backups_->currentItem()->data(0, Qt::UserRole).toJsonObject().value("id").toString() : QString();
    backups_->clear();
    for (const auto &value : snapshot_.backups) {
        const auto backup = value.toObject();
        const auto created = QDateTime::fromString(backup.value("created").toString(), Qt::ISODate).toLocalTime();
        const QString phase = backup.value("phase").toString();
        QStringList operations;
        for (const auto &operation : backup.value("operations").toArray()) operations << operation.toString();
        auto *item = new QTreeWidgetItem(backups_, {
            created.isValid() ? QLocale().toString(created, QLocale::ShortFormat) : backup.value("created").toString(),
            phase == QLatin1String("completed") ? tr("Completed") : phase == QLatin1String("incomplete") ? tr("Incomplete") : tr("Interrupted"),
            operations.join(QStringLiteral("; "))});
        item->setData(0, Qt::UserRole, backup);
        if (backup.value("id").toString() == previous) backups_->setCurrentItem(item);
    }
    backups_->resizeColumnToContents(0);
    backups_->resizeColumnToContents(1);
    if (!backups_->topLevelItemCount()) backupDetails_->setText(tr("No backups yet. One is created each time changes are applied."));
    showBackup();
}

void RecoveryPage::showBackup()
{
    const auto *item = backups_->currentItem();
    QStringList files;
    if (item) {
        const auto backup = item->data(0, Qt::UserRole).toJsonObject();
        for (const auto &value : backup.value("files").toArray()) {
            const auto file = value.toObject();
            files << (file.value("existed").toBool() ? tr("Restore %1") : tr("Remove %1 (created by the change)"))
                .arg(file.value("path").toString());
        }
        QStringList order;
        for (const auto &number : backup.value("bootOrder").toArray()) order << number.toString();
        if (!order.isEmpty()) files << tr("For reference, BootOrder was %1; it is not changed by restoring.").arg(order.join(QStringLiteral(", ")));
        backupDetails_->setText(files.isEmpty() ? tr("This backup contains no configuration files.") : files.join(QLatin1Char('\n')));
    } else if (backups_->topLevelItemCount()) backupDetails_->setText(tr("Select a backup to see which files it restores."));
    bool restorable = false;
    if (item) for (const auto &value : item->data(0, Qt::UserRole).toJsonObject().value("files").toArray()) restorable |= !value.toObject().isEmpty();
    restore_->setEnabled(restorable);
}

void RecoveryPage::showInstance()
{
    const auto kind = selector_->method();
    auto *repair = repairs_.value(static_cast<int>(kind));
    forms_->setCurrentWidget(repair);
    const auto &installations = selector_->installations();
    const int index = selector_->currentIndex();
    const BootInstallation *installation = index >= 0 && index < installations.size() ? &installations[index] : nullptr;
    QStringList lines;
    if (!installation) lines << tr("%1 was not detected. Repair installs it at the chosen destination.").arg(loaderName(kind));
    else if (installation->issues.isEmpty()) lines << tr("No problems were detected for this installation.");
    else for (const auto &issue : installation->issues) lines << QStringLiteral("• ") + issue;
    if (installation && !installation->binary.isEmpty() && !installation->firmwareRegistered && snapshot_.uefi)
        lines << QStringLiteral("• ") + tr("No UEFI firmware entry starts this program.");
    for (const auto &message : snapshot_.messages) lines << QStringLiteral("• ") + message;
    diagnostics_->setText(lines.join(QLatin1Char('\n')));
    const auto packages = BootPackages::plan(kind, false, false, snapshot_);
    repair->setPackagesReady(packages.error.isEmpty() && packages.missing.isEmpty());
    repair->selectInstallation(installation);
}
