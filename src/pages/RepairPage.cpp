#include "RepairPage.h"
#include "widgets/BootUi.h"

#include <QLabel>
#include <QInputDialog>
#include <QPushButton>
#include <QVBoxLayout>

RepairPage::RepairPage(QWidget *parent) : QWidget(parent)
{
    auto *layout = BootUi::page(this);
    auto *content = layout->parentWidget();
    BootUi::title(layout, content, tr("Rebuild and update"),
                  tr("Queue a loader-specific maintenance operation. Installation to a different disk is not performed here."));
    BootUi::section(layout, content, tr("Configuration generation"));
    grub_ = BootUi::action(layout, content, tr("Regenerate GRUB menu"),
            tr("Runs grub-mkconfig with the installed system configuration."),
            QStringLiteral("view-refresh"));
    limine_ = BootUi::action(layout, content, tr("Regenerate Limine kernel entries"),
            tr("Runs limine-mkinitcpio. Maintains UKI / initramfs and Limine entries using the existing hooks."),
            QStringLiteral("view-refresh"));
    BootUi::section(layout, content, tr("Loader program update"));
    systemd_ = BootUi::action(layout, content, tr("Update systemd-boot program"),
            tr("Runs bootctl update. This is separate from editing loader.conf."),
            QStringLiteral("system-software-update"));
    layout->addWidget(BootUi::muted(tr("Changes are reviewed and applied with the other queued operations."
                                         " Never use this page to guess or replace another operating system's loader."), content));
    layout->addStretch();
    connect(grub_, &QPushButton::clicked, this, [this] {
        emit operationRequested({QStringLiteral("rebuild-grub"), QStringLiteral("grub"), {}, {},
                                 tr("Regenerate GRUB configuration")});
    });
    connect(limine_, &QPushButton::clicked, this, [this] {
        emit operationRequested({QStringLiteral("rebuild-limine"), QStringLiteral("limine"), {}, {},
                                 tr("Regenerate Limine kernel entries")});
    });
    connect(systemd_, &QPushButton::clicked, this, [this] {
        if (systemdEsp_.isEmpty()) return;
        QString esp = systemdEsp_.first();
        if (systemdEsp_.size() > 1) {
            bool ok = false;
            esp = QInputDialog::getItem(this, tr("Select EFI partition"),
                                      tr("Update the systemd-boot binary on:"), systemdEsp_,
                                      0, false, &ok);
            if (!ok) return;
        }
        emit operationRequested({QStringLiteral("update-systemd"), esp, {}, {},
                                 tr("Update systemd-boot on %1").arg(esp)});
    });
}

void RepairPage::updateSnapshot(const BootSnapshot &snapshot)
{
    bool grub = false;
    bool limine = false;
    bool systemd = false;
    systemdEsp_.clear();
    for (const auto &loader : snapshot.installations) {
        grub |= loader.kind == LoaderKind::Grub;
        limine |= loader.kind == LoaderKind::Limine;
        systemd |= loader.kind == LoaderKind::SystemdBoot;
        if (loader.kind == LoaderKind::SystemdBoot && !loader.esp.isEmpty()
            && !systemdEsp_.contains(loader.esp)) systemdEsp_.append(loader.esp);
    }
    grub_->setEnabled(grub);
    limine_->setEnabled(limine);
    systemd_->setEnabled(systemd && snapshot.uefi && !systemdEsp_.isEmpty());
}
