#include "MethodsPage.h"
#include "RepairPage.h"
#include "model/BootPlan.h"
#include "services/BootPackages.h"
#include "services/BootDeploy.h"
#include "widgets/BootUi.h"
#include "widgets/LoaderSelector.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

MethodsPage::MethodsPage(QWidget *parent) : QWidget(parent)
{
    auto *layout = BootUi::page(this);
    auto *content = layout->parentWidget();
    selector_ = new LoaderSelector(content);
    layout->addWidget(selector_);
    pages_ = new QStackedWidget(content);
    pages_->setObjectName(QStringLiteral("methodPages"));
    layout->addWidget(pages_);
    for (const auto kind : loaderKinds) {
        Panel panel;
        panel.page = new QWidget(pages_);
        panel.page->setObjectName(QStringLiteral("methodPanel%1").arg(static_cast<int>(kind)));
        auto *body = new QVBoxLayout(panel.page);
        body->setContentsMargins(0, 0, 0, 0);
        panel.details = BootUi::muted({}, panel.page);
        panel.details->setTextFormat(Qt::PlainText);
        panel.details->setObjectName(QStringLiteral("methodDetails"));
        body->addWidget(panel.details);
        panel.firmware = new QWidget(panel.page);
        auto *firmware = new QVBoxLayout(panel.firmware);
        firmware->setContentsMargins(0, 0, 0, 0);
        BootUi::section(firmware, panel.firmware, tr("Firmware selection"));
        panel.entries = new QComboBox(panel.firmware);
        panel.entries->setObjectName(QStringLiteral("methodFirmwareEntry"));
        BootUi::enableCopy(panel.entries);
        firmware->addWidget(panel.entries);
        panel.status = BootUi::muted({}, panel.firmware);
        panel.status->setTextFormat(Qt::PlainText);
        firmware->addWidget(panel.status);
        auto *buttons = new QHBoxLayout;
        panel.setDefault = new QPushButton(tr("Set as firmware default"), panel.firmware);
        panel.setDefault->setObjectName(QStringLiteral("methodSetDefault"));
        panel.once = new QPushButton(tr("Boot once"), panel.firmware);
        panel.once->setObjectName(QStringLiteral("methodBootOnce"));
        buttons->addWidget(panel.setDefault);
        buttons->addWidget(panel.once);
        buttons->addStretch();
        firmware->addLayout(buttons);
        body->addWidget(panel.firmware);
        BootUi::section(body, panel.page, tr("Software packages (current system)"));
        panel.packages = BootUi::muted({}, panel.page);
        panel.packages->setObjectName(QStringLiteral("methodPackagePlan"));
        panel.packages->setTextFormat(Qt::PlainText);
        body->addWidget(panel.packages);
        panel.installPackages = new QPushButton(tr("Install software packages"), panel.page);
        panel.installPackages->setObjectName(QStringLiteral("installSoftwarePackages"));
        body->addWidget(panel.installPackages, 0, Qt::AlignLeft);
        connect(panel.installPackages, &QPushButton::clicked, this, [this] {
            const auto kind = selector_->method();
            const auto packages = BootPackages::plan(kind, false, false, snapshot_);
            if (!packages.error.isEmpty() || packages.missing.isEmpty()) return;
            emit installationRequested({QStringLiteral("install-packages"), loaderName(kind),
                packages.required.join(QLatin1Char(',')), {},
                tr("Install %1 software packages on the current system: %2").arg(loaderName(kind), packages.missing.join(QStringLiteral(", ")))});
        });
        {
            BootUi::section(body, panel.page, tr("Deploy %1").arg(loaderName(kind)));
            panel.repair = new RepairPage(kind, panel.page);
            panel.repair->setObjectName(QStringLiteral("installationForm"));
            body->addWidget(panel.repair);
            connect(panel.repair, &RepairPage::installationRequested, this, &MethodsPage::installationRequested);
        }
        panel.addEntry = panel.repair ? panel.repair->bootEntryButton() : new QPushButton(tr("Add boot entry"), panel.page);
        panel.addEntry->setObjectName(QStringLiteral("methodAddBootEntry"));
        if (!panel.repair) body->addWidget(panel.addEntry, 0, Qt::AlignLeft);
        connect(panel.addEntry, &QPushButton::clicked, this, [this] {
            const int index = selector_->currentIndex();
            if (index < 0 || !current_->addEntry->isEnabled()) return;
            const auto &loader = selector_->installations()[index];
            if (BootDeploy::directImage(loader.kind)) { current_->repair->addImageBootEntry(); return; }
            emit installationRequested({QStringLiteral("firmware-add"), loader.binary, loaderName(loader.kind), {},
                tr("Add %1 boot entry").arg(loaderName(loader.kind))});
        });
        body->addStretch();
        pages_->addWidget(panel.page);
        panels_.insert(static_cast<int>(kind), panel);
        connect(panel.entries, &QComboBox::currentIndexChanged, this, [this, kind] {
            if (selector_->method() == kind) updateActions();
        });
        connect(panel.setDefault, &QPushButton::clicked, this, [this] { queueSelection(false); });
        connect(panel.once, &QPushButton::clicked, this, [this] { queueSelection(true); });
    }
    layout->addStretch();
    connect(selector_, &LoaderSelector::selectionChanged, this, &MethodsPage::showInstance);
    connect(selector_, &LoaderSelector::methodChanged, this, &MethodsPage::methodChanged);
    showInstance();
}

void MethodsPage::updateSnapshot(const BootSnapshot &snapshot)
{
    snapshot_ = snapshot;
    for (auto &panel : panels_)
        if (panel.repair) panel.repair->updateTargets(snapshot);
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
    return snapshot_.entry(current_->entries->currentData().toString());
}

void MethodsPage::showInstance()
{
    const auto kind = selector_->method();
    current_ = &panels_[static_cast<int>(kind)];
    pages_->setCurrentWidget(current_->page);
    current_->entries->blockSignals(true);
    current_->entries->clear();
    const auto &installations = selector_->installations();
    const int index = selector_->currentIndex();
    const bool detected = index >= 0 && index < installations.size();
    current_->firmware->setVisible(detected && snapshot_.uefi);
    if (!detected) {
        current_->details->setText(tr("%1 is not installed.").arg(loaderName(kind)));
    } else {
        const auto &installation = installations[index];
        current_->details->setText(installation.binaryPresent
            ? tr("Installed on %1").arg(installation.esp.isEmpty() ? tr("Current system") : installation.esp)
            : tr("Current system"));
        if (!installation.issues.isEmpty())
            current_->details->setText(current_->details->text() + QLatin1Char('\n') + installation.issues.join(QLatin1Char('\n')));
        for (const auto &number : installation.firmwareEntries) {
            const auto *entry = snapshot_.entry(number);
            if (entry) current_->entries->addItem(QStringLiteral("Boot") + number + QStringLiteral(" · ") + entry->label, number);
        }
    }
    const QString esp = detected ? installations[index].esp : QString();
    const auto packages = BootPackages::plan(kind, false, false, snapshot_);
    current_->packages->setText(!packages.error.isEmpty() ? packages.error
        : packages.missing.isEmpty() ? tr("Software packages installed")
        : tr("Missing: %1").arg(packages.missing.join(QStringLiteral(", "))));
    current_->installPackages->setEnabled(packages.error.isEmpty() && !packages.missing.isEmpty());
    if (current_->repair) {
        current_->repair->selectInstallation(detected ? &installations[index] : nullptr);
        current_->repair->setPackagesReady(packages.error.isEmpty() && packages.missing.isEmpty());
    }
    const bool registered = current_->entries->count() > 0;
    current_->firmware->setVisible(registered && snapshot_.uefi);
    current_->entries->setVisible(registered);
    current_->setDefault->setVisible(registered);
    current_->once->setVisible(registered);
    current_->entries->blockSignals(false);
    updateActions();
}

void MethodsPage::updateActions()
{
    current_->setDefault->setEnabled(false);
    current_->once->setEnabled(false);
    const int index = selector_->currentIndex();
    const bool selected = index >= 0 && index < selector_->installations().size();
    const bool registered = current_->entries->count() > 0;
    current_->addEntry->setVisible(selected && snapshot_.uefi && !registered);
    current_->addEntry->setEnabled(selected && snapshot_.uefi && !registered
        && selector_->installations()[index].binaryPresent);
    const auto *entry = selectedEntry();
    if (!entry) {
        current_->status->clear();
        return;
    }
    QStringList order = snapshot_.bootOrder;
    QString next = snapshot_.bootNext;
    for (const auto &op : transaction_.operations()) {
        if (op.kind == QLatin1String("firmware-order")) order = op.value.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (op.kind == QLatin1String("firmware-next")) next = op.value;
        if (op.target == entry->number && (op.kind == QLatin1String("firmware-remove")
            || op.kind == QLatin1String("firmware-rename") || op.kind == QLatin1String("firmware-toggle"))) {
            current_->status->setText(tr("Cancel pending changes to this firmware entry before selecting it."));
            return;
        }
    }
    BootOperation check{"firmware-next", "BootNext", entry->number, snapshot_.bootNext, {}, entry->identity()};
    const QString conflict = BootPlan::firmwareConflict({check}, snapshot_);
    if (!snapshot_.uefi || !entry->enabled || !conflict.isEmpty()) {
        current_->status->setText(!conflict.isEmpty() ? conflict : tr("An enabled UEFI entry is required."));
        return;
    }
    current_->status->setText(tr("Current: %1 · Default: %2 · Next: %3")
        .arg(snapshot_.bootCurrent, order.join(QStringLiteral(", ")), next));
    current_->setDefault->setEnabled(order.isEmpty() || order.first() != entry->number);
    current_->once->setEnabled(next != entry->number);
}

void MethodsPage::queueSelection(bool once)
{
    updateActions();
    if (!(once ? current_->once->isEnabled() : current_->setDefault->isEnabled())) return;
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
