#include "OverviewPage.h"
#include "widgets/BootUi.h"

#include <QLayoutItem>
#include <QLabel>
#include <QVBoxLayout>

OverviewPage::OverviewPage(QWidget *parent) : QWidget(parent)
{
    layout_ = BootUi::page(this);
}

void OverviewPage::updateSnapshot(const BootSnapshot &snapshot)
{
    while (auto *item = layout_->takeAt(0)) {
        if (auto *widget = item->widget()) widget->deleteLater();
        delete item;
    }
    auto *content = layout_->parentWidget();
    BootUi::title(layout_, content, tr("Boot overview"),
                  tr("Detected firmware, boot loaders and configuration locations."));
    BootUi::section(layout_, content, tr("Current boot"));
    BootUi::line(layout_, content, tr("Firmware mode"),
                 snapshot.uefi ? QStringLiteral("UEFI") : QStringLiteral("Legacy BIOS"),
                 QStringLiteral("computer"));
    BootUi::line(layout_, content, tr("Loader used this boot"),
                 snapshot.currentLoader + QStringLiteral(" · ") + snapshot.currentEvidence,
                 QStringLiteral("system-run"));
    if (snapshot.uefi) {
        BootUi::line(layout_, content, tr("UEFI boot selection"),
                     tr("Current: %1 · Next: %2").arg(snapshot.bootCurrent.isEmpty() ? tr("unknown") : snapshot.bootCurrent,
                         snapshot.bootNext.isEmpty() ? tr("not set") : snapshot.bootNext),
                     QStringLiteral("drive-harddisk"));
        QString defaultEntry = tr("not set");
        for (const auto &number : snapshot.bootOrder) {
            const auto *entry = snapshot.entry(number);
            if (entry && entry->enabled) {
                defaultEntry = QStringLiteral("Boot") + number + QStringLiteral(" · ") + entry->label;
                break;
            }
        }
        BootUi::line(layout_, content, tr("First enabled entry in BootOrder"), defaultEntry,
                     QStringLiteral("emblem-default"));
    }
    BootUi::section(layout_, content, tr("Detected installations"));
    for (const auto &loader : snapshot.installations) {
        QStringList details;
        if (!loader.esp.isEmpty()) details << tr("ESP: %1").arg(loader.esp);
        if (!loader.espPartuuid.isEmpty()) details << tr("Partition UUID: %1").arg(loader.espPartuuid);
        if (!loader.binary.isEmpty()) details << tr("Program: %1").arg(loader.binary);
        if (!loader.config.isEmpty()) details << tr("Configuration: %1").arg(loader.config);
        if (loader.binaryPresent) details << tr("EFI binary found");
        if (!loader.binary.isEmpty() && !loader.binaryPresent) details << tr("EFI program is missing");
        if (loader.configPresent) details << tr("Configuration found");
        if (loader.firmwareRegistered) details << tr("Firmware entry present");
        if (loader.current) details << tr("Selected for this boot");
        if (loader.defaultEntry) details << tr("First enabled entry in BootOrder");
        if (loader.nextEntry) details << tr("Selected for next boot only");
        if (!loader.firmwareEntries.isEmpty()) details << tr("Firmware entries: %1").arg(loader.firmwareEntries.join(QStringLiteral(", ")));
        if (!loader.evidence.isEmpty()) details << loader.evidence;
        BootUi::line(layout_, content, loaderName(loader.kind), details.join(QStringLiteral(" · ")),
                     QStringLiteral("drive-harddisk"));
    }
    if (snapshot.installations.isEmpty())
        layout_->addWidget(BootUi::muted(tr("No manageable loader installation was detected."), content));
    if (!snapshot.messages.isEmpty()) {
        BootUi::section(layout_, content, tr("Detection notes"));
        for (const auto &message : snapshot.messages)
            layout_->addWidget(BootUi::muted(message, content));
    }
    layout_->addStretch();
}
