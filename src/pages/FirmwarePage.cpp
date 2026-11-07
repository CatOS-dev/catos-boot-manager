#include "FirmwarePage.h"
#include "widgets/BootUi.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

FirmwarePage::FirmwarePage(QWidget *parent) : QWidget(parent)
{
    auto *layout = BootUi::page(this);
    auto *content = layout->parentWidget();
    BootUi::title(layout, content, tr("UEFI boot entries"),
                  tr("Manage the default and one-time boot entries."));
    BootUi::section(layout, content, tr("Firmware entries"));
    detail_ = BootUi::muted({}, content);
    layout->addWidget(detail_);
    table_ = new QTableWidget(content);
    BootUi::enableCopy(table_);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({tr("Boot ID"), tr("Name"), tr("EFI loader"), tr("State")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->setMinimumHeight(290);
    layout->addWidget(table_);
    auto *orderButtons = new QHBoxLayout;
    auto *up = new QPushButton(tr("Move up"), content);
    auto *down = new QPushButton(tr("Move down"), content);
    up->setObjectName(QStringLiteral("moveBootEntryUp"));
    down->setObjectName(QStringLiteral("moveBootEntryDown"));
    for (auto *button : {up, down}) { orderButtons->addWidget(button); actions_.append(button); }
    orderButtons->addStretch();
    layout->addLayout(orderButtons);
    BootUi::section(layout, content, tr("Entry operations"));
    auto *buttons = new QHBoxLayout;
    auto *next = new QPushButton(tr("Boot once"), content);
    clearNext_ = new QPushButton(tr("Clear next boot"), content);
    clearNext_->setObjectName(QStringLiteral("clearNextBoot"));
    auto *toggle = new QPushButton(tr("Enable"), content);
    toggle_ = toggle;
    toggle->setObjectName("toggleBootEntry");
    next->setObjectName("bootEntryOnce");
    auto *rename = new QPushButton(tr("Rename"), content);
    auto *remove = new QPushButton(tr("Remove entry"), content);
    remove_ = remove;
    auto *add = new QPushButton(tr("Add EFI entry"), content);
    for (auto *button : {next, clearNext_, toggle, rename, remove, add}) {
        buttons->addWidget(button);
        actions_.append(button);
    }
    buttons->addStretch();
    layout->addLayout(buttons);
    auto *danger = new QHBoxLayout;
    danger->addStretch();
    buttons->removeWidget(remove);
    danger->addWidget(remove);
    layout->addLayout(danger);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this, up, down, next, rename] {
        const auto *entry = snapshot_.entry(selectedId());
        const int position = displayOrder_.indexOf(selectedId());
        const QString select = tr("Select a firmware entry first.");
        const bool removing = entry && pendingRemovals_.contains(entry->number);
        const bool secureBootBlocked = snapshot_.secureBoot != SecureBootState::Disabled
            && snapshot_.secureBoot != SecureBootState::Unsupported;
        const QString removal = tr("This entry is queued for removal. Cancel the removal in Pending changes to edit it.");
        const bool ordered = position >= 0 && position < snapshot_.bootOrder.size();
        const QString orderReason = !entry ? select : !ordered ? tr("This entry is not part of BootOrder.")
            : tr("The entry is already at this end of the boot order.");
        BootUi::setAvailable(up, ordered && position > 0 && !secureBootBlocked,
            secureBootBlocked ? tr("Secure Boot is enabled or its state is unknown; changing the default is unavailable.") : orderReason);
        BootUi::setAvailable(down, ordered && position + 1 < snapshot_.bootOrder.size() && !secureBootBlocked,
            secureBootBlocked ? tr("Secure Boot is enabled or its state is unknown; changing the default is unavailable.") : orderReason);
        BootUi::setAvailable(next, entry && pendingEnabled_.value(entry->number, entry->enabled) && !removing && !secureBootBlocked,
            secureBootBlocked ? tr("Secure Boot is enabled or its state is unknown; one-time firmware selection is unavailable.")
                : !entry ? select : removing ? removal : tr("Enable this entry before selecting it for the next boot."));
        BootUi::setAvailable(rename, entry && !removing, !entry ? select : removal);
        BootUi::setAvailable(toggle_, entry && !removing, !entry ? select : removal);
        toggle_->setText(entry && pendingEnabled_.value(entry->number, entry->enabled) ? tr("Disable") : tr("Enable"));
        BootUi::setAvailable(remove_, entry && !entry->current && !removing,
            !entry ? select : removing ? tr("This entry is already queued for removal.")
                                       : tr("The entry used to start this system cannot be removed."));
    });
    layout->addStretch();
    connect(up, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(next, &QPushButton::clicked, this, &FirmwarePage::queueNext);
    connect(clearNext_, &QPushButton::clicked, this, &FirmwarePage::clearNext);
    connect(toggle, &QPushButton::clicked, this, &FirmwarePage::queueToggle);
    connect(rename, &QPushButton::clicked, this, &FirmwarePage::queueRename);
    connect(remove, &QPushButton::clicked, this, &FirmwarePage::queueRemoval);
    connect(add, &QPushButton::clicked, this, &FirmwarePage::queueCreation);
}

void FirmwarePage::updateSnapshot(const BootSnapshot &snapshot)
{
    snapshot_ = snapshot;
    pendingEnabled_.clear();
    pendingNames_.clear();
    pendingRemovals_.clear();
    pendingNext_ = snapshot.bootNext;
    displayOrder_ = snapshot.bootOrder;
    for (const auto &entry : snapshot.firmware)
        if (!displayOrder_.contains(entry.number)) displayOrder_.append(entry.number);
    const bool available = snapshot.uefi && !snapshot.firmware.isEmpty();
    for (auto *button : actions_) button->setEnabled(available);
    if (!actions_.isEmpty()) BootUi::setAvailable(actions_.last(), snapshot.uefi && !snapshot.espMounts.isEmpty(),
        !snapshot.uefi ? tr("This system was not started in UEFI mode.") : tr("No mounted EFI system partition was detected."));
    BootUi::setAvailable(clearNext_, snapshot.uefi && !snapshot.bootNext.isEmpty(), tr("No one-time boot entry is set."));
    if (snapshot.uefi && snapshot.firmware.isEmpty()) {
        detail_->setText(tr("Unable to read boot entries. Refresh and try again."));
    } else if (snapshot.secureBoot != SecureBootState::Disabled && snapshot.secureBoot != SecureBootState::Unsupported) {
        detail_->setText(tr("Secure Boot is enabled or its state is unknown; default and one-time firmware selection are unavailable."));
    } else {
        detail_->setText(tr("Current: %1 · Next: %2 · BootOrder: %3")
            .arg(snapshot.bootCurrent.isEmpty() ? tr("unknown") : snapshot.bootCurrent,
                 snapshot.bootNext.isEmpty() ? tr("not set") : snapshot.bootNext,
                 snapshot.bootOrder.join(QStringLiteral(", "))));
    }
    redraw();
    emit table_->itemSelectionChanged();
}

void FirmwarePage::updateTransaction(const BootTransaction &transaction)
{
    pendingNext_ = snapshot_.bootNext;
    QStringList order = snapshot_.bootOrder;
    pendingEnabled_.clear();
    pendingNames_.clear();
    pendingRemovals_.clear();
    for (const auto &op : transaction.operations()) {
        if (op.kind == QLatin1String("firmware-rename")) pendingNames_.insert(op.target, op.value);
        if (op.kind == QLatin1String("firmware-remove")) pendingRemovals_.insert(op.target);
        if (op.kind == QLatin1String("firmware-toggle")) pendingEnabled_.insert(op.target, op.value == QLatin1String("1"));
        if (op.kind == QLatin1String("firmware-next")) pendingNext_ = op.value;
        if (op.kind == QLatin1String("firmware-order"))
            order = op.value.split(QLatin1Char(','), Qt::SkipEmptyParts);
    }
    displayOrder_ = order;
    for (const auto &entry : snapshot_.firmware)
        if (!displayOrder_.contains(entry.number)) displayOrder_.append(entry.number);
    BootUi::setAvailable(clearNext_, snapshot_.uefi && !pendingNext_.isEmpty(), tr("No one-time boot entry is set."));
    detail_->setText(tr("Current: %1 · Next (including pending): %2 · BootOrder (including pending): %3")
        .arg(snapshot_.bootCurrent.isEmpty() ? tr("unknown") : snapshot_.bootCurrent,
             pendingNext_.isEmpty() ? tr("not set") : pendingNext_,
             displayOrder_.join(QStringLiteral(", "))));
    redraw();
    emit table_->itemSelectionChanged();
}

void FirmwarePage::redraw()
{
    const QString selected = selectedId();
    table_->setRowCount(0);
    for (const auto &id : displayOrder_) {
        const auto *entry = snapshot_.entry(id);
        if (!entry) continue;
        const int row = table_->rowCount();
        table_->insertRow(row);
        QString status = (pendingEnabled_.value(id, entry->enabled) ? tr("Enabled") : tr("Disabled"))
            + (entry->current ? QStringLiteral(" · ") + tr("Current") : QString());
        if (pendingEnabled_.contains(id)) status += " · " + tr("Pending change");
        if (pendingNames_.contains(id)) status += " · " + tr("Pending rename");
        if (pendingRemovals_.contains(id)) status += " · " + tr("Pending removal");
        const QStringList fields{QStringLiteral("Boot") + id, pendingNames_.value(id, entry->label), entry->path, status};
        for (int column = 0; column < fields.size(); ++column) {
            auto *item = new QTableWidgetItem(fields.at(column));
            if (column == 0) item->setData(Qt::UserRole, id);
            item->setToolTip(fields.at(column));
            table_->setItem(row, column, item);
        }
        if (id == selected) table_->selectRow(row);
    }
    table_->resizeColumnToContents(0);
    table_->resizeColumnToContents(3);
}

QString FirmwarePage::selectedId() const
{
    const int row = table_->currentRow();
    return row < 0 || !table_->item(row, 0) ? QString()
        : table_->item(row, 0)->data(Qt::UserRole).toString();
}

void FirmwarePage::moveSelected(int delta)
{
    const QString id = selectedId();
    const int position = displayOrder_.indexOf(id);
    const int destination = position + delta;
    if (position < 0 || destination < 0 || destination >= displayOrder_.size()) return;
    if (position >= snapshot_.bootOrder.size() && delta >= 0) return;
    if (position == snapshot_.bootOrder.size() && delta < 0) {
        displayOrder_.move(position, snapshot_.bootOrder.size() - 1);
        redraw();
        table_->selectRow(snapshot_.bootOrder.size() - 1);
        queueOrder();
        return;
    }
    if (position > snapshot_.bootOrder.size() || destination >= snapshot_.bootOrder.size()) return;
    displayOrder_.move(position, destination);
    redraw();
    table_->selectRow(destination);
    queueOrder();
}

void FirmwarePage::queueOrder()
{
    const QStringList ordered = displayOrder_;
    emit operationRequested({QStringLiteral("firmware-order"), QStringLiteral("BootOrder"),
        ordered.join(QLatin1Char(',')), snapshot_.bootOrder.join(QLatin1Char(',')),
        tr("Set UEFI boot order: %1").arg(ordered.join(QStringLiteral(", ")))});
}

void FirmwarePage::queueNext()
{
    const QString id = selectedId();
    if (id.isEmpty()) return;
    emit operationRequested({QStringLiteral("firmware-next"), QStringLiteral("BootNext"), id,
                             snapshot_.bootNext, tr("Boot%1 once on next restart").arg(id)});
}

void FirmwarePage::clearNext()
{
    if (pendingNext_.isEmpty()) return;
    emit operationRequested({QStringLiteral("firmware-next"), QStringLiteral("BootNext"), {},
                             snapshot_.bootNext, tr("Clear one-time UEFI boot selection")});
}

void FirmwarePage::queueRename()
{
    const auto *entry = snapshot_.entry(selectedId());
    if (!entry) return;
    bool ok = false;
    const QString label = QInputDialog::getText(this, tr("Rename UEFI boot entry"),
        tr("New firmware entry label:"), QLineEdit::Normal, pendingNames_.value(entry->number, entry->label), &ok).trimmed();
    if (!ok || label.isEmpty() || label == pendingNames_.value(entry->number, entry->label)) return;
    if (label.size() > 80 || label.contains(QLatin1Char('\n'))
        || label.contains(QLatin1Char('\r'))) {
        QMessageBox::warning(this, tr("Invalid EFI label"), tr("Enter a short single-line label."));
        return;
    }
    emit operationRequested({QStringLiteral("firmware-rename"), entry->number, label,
        entry->identity(), tr("Rename firmware entry Boot%1: %2 → %3")
            .arg(entry->number, entry->label, label)});
}

void FirmwarePage::queueToggle()
{
    const auto *entry = snapshot_.entry(selectedId());
    if (!entry) return;
    const bool enabled = pendingEnabled_.value(entry->number, entry->enabled);
    emit operationRequested({QStringLiteral("firmware-toggle"), entry->number,
        enabled ? QStringLiteral("0") : QStringLiteral("1"),
        entry->enabled ? QStringLiteral("1") : QStringLiteral("0"),
        tr("%1 firmware entry Boot%2").arg(enabled ? tr("Disable") : tr("Enable"), entry->number)});
}

void FirmwarePage::queueRemoval()
{
    const auto *entry = snapshot_.entry(selectedId());
    if (!entry) return;
    if (entry->current) {
        QMessageBox::warning(this, tr("Current entry"),
                             tr("The entry used for this boot cannot be removed here."));
        return;
    }
    if (QMessageBox::question(this, tr("Queue removal"),
          tr("Remove Boot%1 (%2)?").arg(entry->number, entry->label))
        != QMessageBox::Yes) return;
    emit operationRequested({QStringLiteral("firmware-remove"), entry->number, {}, entry->identity(),
                             tr("Remove firmware entry Boot%1 (%2)").arg(entry->number, entry->label)});
}

void FirmwarePage::queueCreation()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Select EFI executable"),
                          QStringLiteral("/boot"), tr("EFI executable (*.efi *.EFI)"));
    if (file.isEmpty()) return;
    bool ok = false;
    const QString label = QInputDialog::getText(this, tr("New UEFI entry"),
        tr("Display name:"), QLineEdit::Normal, QFileInfo(file).completeBaseName(), &ok);
    if (!ok || label.trimmed().isEmpty()) return;
    emit operationRequested({QStringLiteral("firmware-add"), file, label.trimmed(), {},
                             tr("Create firmware entry %1 → %2").arg(label.trimmed(), file)});
}
