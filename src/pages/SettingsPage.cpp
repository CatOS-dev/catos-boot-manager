#include "SettingsPage.h"
#include "services/BootSettings.h"
#include "widgets/BootUi.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

SettingsPage::SettingsPage(QWidget *parent) : QWidget(parent)
{
    layout_ = BootUi::page(this);
    auto *content = layout_->parentWidget();
    BootUi::title(layout_, content, tr("Boot loader settings"),
                  tr("Changes are queued. No system files are modified until you apply the transaction."));
    BootUi::section(layout_, content, tr("Choose installation"));
    selector_ = new QComboBox(content);
    layout_->addWidget(selector_);
    location_ = BootUi::muted({}, content);
    layout_->addWidget(location_);
    BootUi::section(layout_, content, tr("Configuration"));
    fields_ = new QFormLayout;
    fields_->setSpacing(11);
    layout_->addLayout(fields_);
    auto *queue = new QPushButton(tr("Queue setting changes"), content);
    queue->setMinimumHeight(36);
    layout_->addWidget(queue, 0, Qt::AlignRight);
    layout_->addStretch();
    connect(selector_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsPage::showSettings);
    connect(queue, &QPushButton::clicked, this, &SettingsPage::stageChanges);
}

void SettingsPage::updateSnapshot(const BootSnapshot &snapshot)
{
    snapshot_ = snapshot;
    manageable_ = snapshot.manageableInstallations();
    const QString previous = selector_->currentData().toString();
    selector_->blockSignals(true);
    selector_->clear();
    for (const auto &loader : manageable_)
        selector_->addItem(loaderName(loader.kind)
            + (loader.kind == LoaderKind::SystemdBoot ? QStringLiteral(" · ") + loader.esp : QString()),
            loader.id);
    int index = selector_->findData(previous);
    if (index < 0) {
        index = 0;
        for (int i = 0; i < manageable_.size(); ++i)
            if (manageable_.at(i).current) { index = i; break; }
    }
    selector_->setCurrentIndex(manageable_.isEmpty() ? -1 : index);
    selector_->blockSignals(false);
    showSettings();
}

void SettingsPage::showSettings()
{
    while (auto *item = fields_->takeAt(0)) {
        if (auto *widget = item->widget()) widget->deleteLater();
        delete item;
    }
    edits_.clear();
    baseline_.clear();
    const int index = selector_->currentIndex();
    if (index < 0 || index >= manageable_.size()) {
        location_->setText(tr("No boot loader configuration is available."));
        return;
    }
    const auto &loader = manageable_.at(index);
    const QString config = BootSettings::configPath(loader.kind, loader.esp);
    location_->setText(tr("Configuration: %1").arg(config));
    for (const auto &setting : BootSettings::settings(loader.kind)) {
        auto *edit = new QLineEdit(fields_->parentWidget());
        edit->setObjectName(setting.key);
        const QString current = BootSettings::read(loader.kind, loader.esp, setting.key);
        edit->setText(current);
        baseline_.append(current);
        edit->setToolTip(setting.description);
        edit->setPlaceholderText(setting.description);
        fields_->addRow(setting.title, edit);
        edits_.append(edit);
    }
}

void SettingsPage::stageChanges()
{
    const int index = selector_->currentIndex();
    if (index < 0 || index >= manageable_.size()) return;
    const auto &loader = manageable_.at(index);
    const auto specs = BootSettings::settings(loader.kind);
    QList<BootOperation> pending;
    for (int i = 0; i < edits_.size() && i < specs.size(); ++i) {
        const auto &spec = specs[i];
        const QString before = baseline_.at(i);
        const QString after = edits_[i]->text();
        // Also stage unchanged values: the transaction removes a previously
        // queued setting when the user returns it to its original value.
        if (!BootSettings::allowed(loader.kind, spec.key, after)) {
            QMessageBox::warning(this, tr("Invalid setting"),
                tr("Invalid value for %1. Check the field description.").arg(spec.title));
            return;
        }
        const QString kind = loader.kind == LoaderKind::Grub ? QStringLiteral("grub-setting")
            : loader.kind == LoaderKind::Limine ? QStringLiteral("limine-setting")
            : QStringLiteral("systemd-setting");
        const QString target = loader.kind == LoaderKind::SystemdBoot
            ? loader.esp + QLatin1Char('|') + spec.key : spec.key;
        pending.append({kind, target, after, before,
                        loaderName(loader.kind) + QStringLiteral(" · ") + spec.title
                        + QStringLiteral(": ") + before + QStringLiteral(" → ") + after});
    }
    for (const auto &item : pending) emit operationRequested(item);
}
