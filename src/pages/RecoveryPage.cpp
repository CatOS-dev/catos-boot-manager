#include "RecoveryPage.h"
#include "RepairPage.h"
#include "services/BootPackages.h"
#include "widgets/BootUi.h"
#include "widgets/LoaderSelector.h"

#include <QLabel>
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
    layout_->addStretch();
    connect(selector_, &LoaderSelector::selectionChanged, this, &RecoveryPage::showInstance);
    showInstance();
}

void RecoveryPage::updateSnapshot(const BootSnapshot &snapshot)
{
    snapshot_ = snapshot;
    for (auto *repair : repairs_) repair->updateTargets(snapshot);
    selector_->updateSnapshot(snapshot);
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
