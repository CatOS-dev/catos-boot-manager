#include "SettingsPage.h"
#include "services/BootProbe.h"
#include "services/BootSettings.h"
#include "widgets/BootUi.h"
#include "widgets/LoaderSelector.h"

#include <QFormLayout>
#include <QJsonDocument>
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
    selector_ = new LoaderSelector(content);
    selector_->setObjectName(QStringLiteral("settingsInstallation"));
    layout_->addWidget(selector_);
    location_ = BootUi::muted({}, content);
    layout_->addWidget(location_);
    BootUi::section(layout_, content, tr("Configuration"));
    fields_ = new QFormLayout;
    fields_->setSpacing(11);
    layout_->addLayout(fields_);
    queue_ = new QPushButton(tr("Queue setting changes"), content);
    queue_->setObjectName(QStringLiteral("queueSettings"));
    queue_->setMinimumHeight(36);
    layout_->addWidget(queue_, 0, Qt::AlignRight);
    layout_->addStretch();
    connect(selector_, &LoaderSelector::selectionChanged, this, [this] {
        manageable_ = selector_->installations();
        showSettings();
    });
    connect(selector_, &LoaderSelector::methodChanged, this, &SettingsPage::methodChanged);
    connect(queue_, &QPushButton::clicked, this, &SettingsPage::stageChanges);
}

void SettingsPage::updateSnapshot(const BootSnapshot &snapshot)
{
    drafts_.clear();
    snapshot_ = snapshot;
    selector_->updateSnapshot(snapshot);
}

void SettingsPage::selectMethod(LoaderKind kind) { selector_->selectMethod(kind); }

void SettingsPage::showSettings()
{
    while (auto *item = fields_->takeAt(0)) {
        if (auto *widget = item->widget()) widget->deleteLater();
        delete item;
    }
    edits_.clear();
    baseline_.clear();
    queue_->setEnabled(false);
    const int index = selector_->currentIndex();
    if (index < 0 || index >= manageable_.size()) {
        location_->setText(tr("No boot loader configuration is available."));
        return;
    }
    const auto &loader = manageable_.at(index);
    const QString config = BootSettings::configPath(loader.kind, loader.esp);
    const bool supported = !BootSettings::settings(loader.kind).isEmpty();
    const bool canEdit = supported && (loader.kind != LoaderKind::Limine ||
        QJsonDocument::fromJson(BootProbe::runReadOnly(QStringLiteral("limine-entry-tool"),
            {QStringLiteral("--config-json")}).toUtf8()).isObject());
    location_->setText(!supported ? tr("Configuration editing is not available for this method.")
        : canEdit ? tr("Configuration: %1").arg(config)
        : tr("Read-only: installed limine-tool lacks the structured settings API. Update limine-tool to edit settings."));
    queue_->setEnabled(canEdit);
    const auto specs = BootSettings::settings(loader.kind);
    if (canEdit && !drafts_.contains(loader.id)) {
        Draft draft;
        for (const auto &setting : specs)
            draft.baseline.append(BootSettings::read(loader.kind, loader.esp, setting.key));
        draft.values = draft.baseline;
        drafts_.insert(loader.id, draft);
    }
    const Draft draft = drafts_.value(loader.id);
    for (int i = 0; i < specs.size(); ++i) {
        const auto &setting = specs[i];
        auto *edit = new QLineEdit(fields_->parentWidget());
        edit->setObjectName(setting.key);
        edit->setText(draft.values.value(i));
        edit->setEnabled(canEdit);
        baseline_.append(draft.baseline.value(i));
        edit->setToolTip(setting.description);
        edit->setPlaceholderText(setting.description);
        fields_->addRow(setting.title, edit);
        edits_.append(edit);
        connect(edit, &QLineEdit::textChanged, this, [this, id = loader.id, i](const QString &value) {
            auto found = drafts_.find(id);
            if (found != drafts_.end()) found->values[i] = value;
        });
    }
}

void SettingsPage::discardOperation(const BootOperation &operation)
{
    for (const auto &loader : snapshot_.manageableInstallations()) {
        const QString kind = loader.kind == LoaderKind::Grub ? QStringLiteral("grub-setting")
            : loader.kind == LoaderKind::Limine ? QStringLiteral("limine-setting")
            : QStringLiteral("systemd-setting");
        if (operation.kind != kind) continue;
        const auto specs = BootSettings::settings(loader.kind);
        for (int i = 0; i < specs.size(); ++i) {
            const QString target = loader.kind == LoaderKind::SystemdBoot
                ? loader.esp + QLatin1Char('|') + specs[i].key : specs[i].key;
            if (operation.target != target) continue;
            auto found = drafts_.find(loader.id);
            if (found == drafts_.end()) continue;
            found->values[i] = found->baseline[i];
            if (selector_->currentId() == loader.id) edits_[i]->setText(found->values[i]);
        }
    }
}

void SettingsPage::stageChanges()
{
    if (!queue_->isEnabled()) return;
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
        if (before != after && !BootSettings::allowed(loader.kind, spec.key, after)) {
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
