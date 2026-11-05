#include "PendingPage.h"
#include "model/BootPlan.h"
#include "widgets/BootUi.h"

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

PendingPage::PendingPage(QWidget *parent) : QWidget(parent)
{
    auto *layout = BootUi::page(this);
    auto *content = layout->parentWidget();
    BootUi::title(layout, content, tr("Pending changes"),
        tr("Review queued operations or cancel an individual change before applying."));
    conflict_ = new QLabel(content);
    conflict_->setObjectName(QStringLiteral("pendingConflict"));
    conflict_->setTextFormat(Qt::PlainText);
    conflict_->setWordWrap(true);
    layout->addWidget(conflict_);
    items_ = new QListWidget(content);
    items_->setObjectName(QStringLiteral("pendingOperations"));
    items_->setMinimumHeight(240);
    layout->addWidget(items_);
    details_ = new QLabel(content);
    details_->setTextFormat(Qt::PlainText);
    details_->setWordWrap(true);
    details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(details_);
    remove_ = new QPushButton(tr("Cancel selected change"), content);
    remove_->setObjectName(QStringLiteral("cancelPendingOperation"));
    remove_->setEnabled(false);
    layout->addWidget(remove_, 0, Qt::AlignRight);
    layout->addStretch();
    connect(items_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        remove_->setEnabled(item != nullptr);
        details_->setText(item ? item->data(Qt::UserRole + 1).toString() : QString());
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        const auto *item = items_->currentItem();
        if (item) emit removalRequested(item->data(Qt::UserRole).toString());
    });
}

void PendingPage::updateTransaction(const BootTransaction &transaction, const BootSnapshot &snapshot)
{
    const QString conflict = BootPlan::firmwareConflict(transaction.operations(), snapshot);
    conflict_->setText(conflict);
    conflict_->setVisible(!conflict.isEmpty());
    const QString selected = items_->currentItem()
        ? items_->currentItem()->data(Qt::UserRole).toString() : QString();
    items_->clear();
    for (const auto &op : transaction.operations()) {
        auto *item = new QListWidgetItem(op.description, items_);
        item->setData(Qt::UserRole, op.key());
        QString details = tr("Target: %1").arg(op.target);
        if (op.kind.endsWith(QLatin1String("-setting"))
            || op.kind == QLatin1String("firmware-next")
            || op.kind == QLatin1String("firmware-order"))
            details += tr("\nOriginal value: %1\nPending value: %2").arg(op.expected, op.value);
        item->setData(Qt::UserRole + 1, details);
        if (op.key() == selected) items_->setCurrentItem(item);
    }
    if (!items_->currentItem() && items_->count()) items_->setCurrentRow(0);
    if (!items_->count()) details_->setText(tr("No pending changes"));
}
