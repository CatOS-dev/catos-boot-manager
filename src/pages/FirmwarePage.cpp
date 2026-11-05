#include "FirmwarePage.h"
#include "services/BootProbe.h"
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
                  tr("Manage firmware BootOrder and BootNext. The boot loader's own menu is separate."));
    BootUi::section(layout, content, tr("Firmware entries"));
    detail_ = BootUi::muted({}, content);
    layout->addWidget(detail_);
    table_ = new QTableWidget(content);
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
    auto *saveOrder = new QPushButton(tr("Queue boot order"), content);
    up->setObjectName(QStringLiteral("moveBootEntryUp"));
    down->setObjectName(QStringLiteral("moveBootEntryDown"));
    saveOrder->setObjectName(QStringLiteral("queueBootOrder"));
    for (auto *button : {up, down, saveOrder}) { orderButtons->addWidget(button); actions_.append(button); }
    orderButtons->addStretch();
    layout->addLayout(orderButtons);
    BootUi::section(layout, content, tr("Entry operations"));
    auto *buttons = new QHBoxLayout;
    auto *next = new QPushButton(tr("Boot once"), content);
    clearNext_ = new QPushButton(tr("Clear next boot"), content);
    clearNext_->setObjectName(QStringLiteral("clearNextBoot"));
    auto *toggle = new QPushButton(tr("Enable / Disable"), content);
    auto *rename = new QPushButton(tr("Rename"), content);
    auto *remove = new QPushButton(tr("Remove entry"), content);
    auto *add = new QPushButton(tr("Add EFI entry"), content);
    for (auto *button : {next, clearNext_, toggle, rename, remove, add}) {
        buttons->addWidget(button);
        actions_.append(button);
    }
    layout->addLayout(buttons);
    layout->addWidget(BootUi::muted(tr("Removing a firmware entry does not delete EFI files from disk."), content));
    layout->addStretch();
    connect(up, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(saveOrder, &QPushButton::clicked, this, &FirmwarePage::queueOrder);
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
    pendingNext_ = snapshot.bootNext;
    displayOrder_ = snapshot.bootOrder;
    for (const auto &entry : snapshot.firmware)
        if (!displayOrder_.contains(entry.number)) displayOrder_.append(entry.number);
    const bool available = snapshot.uefi && !snapshot.firmware.isEmpty();
    for (auto *button : actions_) button->setEnabled(available);
    if (!actions_.isEmpty()) actions_.last()->setEnabled(snapshot.uefi && !BootProbe::espMounts().isEmpty());
    clearNext_->setEnabled(snapshot.uefi && !snapshot.bootNext.isEmpty());
    if (snapshot.uefi && snapshot.firmware.isEmpty()) {
        detail_->setText(tr("No UEFI entries could be read. EFI variables may be unavailable."));
    } else {
        detail_->setText(tr("Current: %1 · Next: %2 · BootOrder: %3")
            .arg(snapshot.bootCurrent.isEmpty() ? tr("unknown") : snapshot.bootCurrent,
                 snapshot.bootNext.isEmpty() ? tr("not set") : snapshot.bootNext,
                 snapshot.bootOrder.join(QStringLiteral(", "))));
    }
    redraw();
}

void FirmwarePage::updateTransaction(const BootTransaction &transaction)
{
    pendingNext_ = snapshot_.bootNext;
    displayOrder_ = snapshot_.bootOrder;
    for (const auto &op : transaction.operations()) {
        if (op.kind == QLatin1String("firmware-next")) pendingNext_ = op.value;
        if (op.kind == QLatin1String("firmware-order"))
            displayOrder_ = op.value.split(QLatin1Char(','), Qt::SkipEmptyParts);
    }
    for (const auto &entry : snapshot_.firmware)
        if (!displayOrder_.contains(entry.number)) displayOrder_.append(entry.number);
    clearNext_->setEnabled(snapshot_.uefi && !pendingNext_.isEmpty());
    detail_->setText(tr("Current: %1 · Next (including pending): %2 · BootOrder (including pending): %3")
        .arg(snapshot_.bootCurrent.isEmpty() ? tr("unknown") : snapshot_.bootCurrent,
             pendingNext_.isEmpty() ? tr("not set") : pendingNext_,
             displayOrder_.join(QStringLiteral(", "))));
    redraw();
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
        const QString status = (entry->enabled ? tr("Enabled") : tr("Disabled"))
            + (entry->current ? QStringLiteral(" · ") + tr("Current") : QString());
        const QStringList fields{QStringLiteral("Boot") + id, entry->label, entry->path, status};
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
    if (position < 0 || position >= snapshot_.bootOrder.size()
        || destination < 0 || destination >= snapshot_.bootOrder.size()) return;
    displayOrder_.move(position, destination);
    redraw();
    table_->selectRow(destination);
}

void FirmwarePage::queueOrder()
{
    const QStringList ordered = displayOrder_.mid(0, snapshot_.bootOrder.size());
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
        tr("New firmware entry label:"), QLineEdit::Normal, entry->label, &ok).trimmed();
    if (!ok || label.isEmpty() || label == entry->label) return;
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
    emit operationRequested({QStringLiteral("firmware-toggle"), entry->number,
        entry->enabled ? QStringLiteral("0") : QStringLiteral("1"),
        entry->enabled ? QStringLiteral("1") : QStringLiteral("0"),
        tr("%1 firmware entry Boot%2").arg(entry->enabled ? tr("Disable") : tr("Enable"), entry->number)});
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
          tr("Queue removal of Boot%1 (%2)? EFI files remain on disk.").arg(entry->number, entry->label))
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
    if (!BootProbe::isOnEsp(file, BootProbe::espMounts())) {
        QMessageBox::warning(this, tr("Invalid EFI file"),
            tr("Select an EFI binary from a mounted EFI system partition."));
        return;
    }
    emit operationRequested({QStringLiteral("firmware-add"), file, label.trimmed(), {},
                             tr("Create firmware entry %1 → %2").arg(label.trimmed(), file)});
}
