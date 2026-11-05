#include "MethodsPage.h"
#include "model/BootPlan.h"
#include "widgets/BootUi.h"
#include "widgets/LoaderSelector.h"

#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

MethodsPage::MethodsPage(QWidget *parent) : QWidget(parent)
{
    auto *layout = BootUi::page(this);
    auto *content = layout->parentWidget();
    BootUi::title(layout, content, tr("Boot methods"),
        tr("Select a deployed instance and review its firmware entry before changing the boot selection."));
    selector_ = new LoaderSelector(content);
    layout->addWidget(selector_);
    details_ = new QLabel(content);
    details_->setTextFormat(Qt::PlainText);
    details_->setWordWrap(true);
    details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(details_);
    BootUi::section(layout, content, tr("Firmware selection"));
    entries_ = new QComboBox(content);
    entries_->setObjectName(QStringLiteral("methodFirmwareEntry"));
    layout->addWidget(entries_);
    status_ = new QLabel(content);
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto *buttons = new QHBoxLayout;
    default_ = new QPushButton(tr("Set as default"), content);
    default_->setObjectName(QStringLiteral("methodSetDefault"));
    once_ = new QPushButton(tr("Boot once"), content);
    once_->setObjectName(QStringLiteral("methodBootOnce"));
    buttons->addWidget(default_);
    buttons->addWidget(once_);
    buttons->addStretch();
    layout->addLayout(buttons);
    layout->addWidget(BootUi::muted(tr("Selecting an instance changes firmware selection only. Other installations and files are retained."), content));
    layout->addStretch();
    connect(selector_, &LoaderSelector::selectionChanged, this, &MethodsPage::showInstance);
    connect(selector_, &LoaderSelector::methodChanged, this, &MethodsPage::methodChanged);
    connect(entries_, &QComboBox::currentIndexChanged, this, &MethodsPage::updateActions);
    connect(default_, &QPushButton::clicked, this, [this] { queueSelection(false); });
    connect(once_, &QPushButton::clicked, this, [this] { queueSelection(true); });
    showInstance();
}

void MethodsPage::updateSnapshot(const BootSnapshot &snapshot)
{
    snapshot_ = snapshot;
    selector_->updateSnapshot(snapshot);
}

void MethodsPage::selectMethod(LoaderKind kind) { selector_->selectMethod(kind); }

void MethodsPage::updateTransaction(const BootTransaction &transaction)
{
    transaction_ = transaction;
    updateActions();
}

const FirmwareEntry *MethodsPage::selectedEntry() const
{
    return snapshot_.entry(entries_->currentData().toString());
}

void MethodsPage::showInstance()
{
    entries_->blockSignals(true);
    entries_->clear();
    const auto &installations = selector_->installations();
    const int index = selector_->currentIndex();
    if (index < 0 || index >= installations.size()) {
        details_->setText(tr("No installation detected for this method."));
    } else {
        const auto &installation = installations[index];
        details_->setText(tr("ESP: %1\nPartition UUID: %2\nProgram: %3\nConfiguration: %4\nEvidence: %5")
            .arg(installation.esp, installation.espPartuuid, installation.binary,
                 installation.config, installation.evidence));
        for (const auto &number : installation.firmwareEntries) {
            const auto *entry = snapshot_.entry(number);
            if (entry) entries_->addItem(QStringLiteral("Boot") + number + QStringLiteral(" · ") + entry->label, number);
        }
    }
    entries_->setEnabled(entries_->count() > 0);
    entries_->blockSignals(false);
    updateActions();
}

void MethodsPage::updateActions()
{
    default_->setEnabled(false);
    once_->setEnabled(false);
    const auto *entry = selectedEntry();
    if (!entry) {
        status_->setText(tr("This instance has no associated firmware entry. File discovery alone does not enable direct boot."));
        return;
    }
    QStringList order = snapshot_.bootOrder;
    QString next = snapshot_.bootNext;
    for (const auto &op : transaction_.operations()) {
        if (op.kind == QLatin1String("firmware-order")) order = op.value.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (op.kind == QLatin1String("firmware-next")) next = op.value;
        if (op.target == entry->number && (op.kind == QLatin1String("firmware-remove")
            || op.kind == QLatin1String("firmware-rename") || op.kind == QLatin1String("firmware-toggle"))) {
            status_->setText(tr("Cancel pending changes to this firmware entry before selecting it."));
            return;
        }
    }
    BootOperation check{"firmware-next", "BootNext", entry->number, snapshot_.bootNext, {}, entry->identity()};
    const QString conflict = BootPlan::firmwareConflict({check}, snapshot_);
    if (!snapshot_.uefi || !entry->enabled || !conflict.isEmpty()) {
        status_->setText(!conflict.isEmpty() ? conflict : tr("An enabled UEFI entry is required."));
        return;
    }
    status_->setText(tr("Current: %1 · Default order: %2 · Next (including pending): %3\nThe program and partition were identified; a successful boot has not been demonstrated.")
        .arg(snapshot_.bootCurrent, order.join(QStringLiteral(", ")), next));
    default_->setEnabled(order.isEmpty() || order.first() != entry->number);
    once_->setEnabled(next != entry->number);
}

void MethodsPage::queueSelection(bool once)
{
    updateActions();
    if (!(once ? once_->isEnabled() : default_->isEnabled())) return;
    const auto *entry = selectedEntry();
    if (!entry) return;
    if (once) {
        emit operationRequested({QStringLiteral("firmware-next"), QStringLiteral("BootNext"), entry->number,
            snapshot_.bootNext, tr("Boot instance %1 (%2) once").arg(entry->label, entry->number), entry->identity()});
        return;
    }
    QStringList order = snapshot_.bootOrder;
    for (const auto &op : transaction_.operations())
        if (op.kind == QLatin1String("firmware-order")) order = op.value.split(QLatin1Char(','), Qt::SkipEmptyParts);
    order.removeAll(entry->number);
    order.prepend(entry->number);
    emit operationRequested({QStringLiteral("firmware-order"), QStringLiteral("BootOrder"), order.join(QLatin1Char(',')),
        snapshot_.bootOrder.join(QLatin1Char(',')), tr("Set instance %1 (%2) as default").arg(entry->label, entry->number), entry->identity()});
}
