#include "SettingsPage.h"
#include "services/BootSettings.h"
#include "widgets/BootUi.h"
#include "widgets/LoaderSelector.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QString editorValue(QWidget *widget)
{
    if (auto *combo = qobject_cast<QComboBox *>(widget)) return combo->currentData().toString();
    return qobject_cast<QLineEdit *>(widget)->text();
}

void setEditorValue(QWidget *widget, const QString &value)
{
    if (!widget) return;
    if (auto *combo = qobject_cast<QComboBox *>(widget)) {
        int index = combo->findData(value);
        if (index < 0) { combo->addItem(value, value); index = combo->count() - 1; }
        combo->setCurrentIndex(index);
    } else qobject_cast<QLineEdit *>(widget)->setText(value);
}
}

SettingsPage::SettingsPage(QWidget *parent, bool embedded) : QWidget(parent)
{
    layout_ = embedded ? new QVBoxLayout(this) : BootUi::page(this);
    if (embedded) layout_->setContentsMargins(0, 0, 0, 0);
    auto *content = layout_->parentWidget();
    if (!embedded) BootUi::title(layout_, content, tr("Boot loader settings"),
                  tr("Edit boot menu settings."));
    if (!embedded) BootUi::section(layout_, content, tr("Choose installation"));
    selector_ = new LoaderSelector(content, true);
    selector_->setObjectName(QStringLiteral("settingsInstallation"));
    layout_->addWidget(selector_);
    selector_->setVisible(!embedded);
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

void SettingsPage::updateSnapshot(const BootSnapshot &snapshot, bool preserveDrafts)
{
    if (!preserveDrafts) drafts_.clear();
    snapshot_ = snapshot;
    selector_->updateSnapshot(snapshot);
}

void SettingsPage::selectMethod(LoaderKind kind) { selector_->selectMethod(kind); }

void SettingsPage::showSettings()
{
    while (fields_->count()) {
        auto *item = fields_->takeAt(0);
        if (auto *widget = item->widget()) widget->deleteLater();
        delete item;
    }
    edits_.clear();
    baseline_.clear();
    queue_->setEnabled(false);
    queue_->hide();
    const int index = selector_->currentIndex();
    if (index < 0 || index >= manageable_.size()) {
        location_->setText(tr("No boot loader configuration is available."));
        return;
    }
    const auto &loader = manageable_.at(index);
    const bool canEdit = loader.settingsEditable;
    location_->setText(canEdit ? tr("Boot configuration") : loader.settingsMessage);
    queue_->setEnabled(canEdit);
    queue_->setVisible(canEdit);
    if (!canEdit) return;
    const auto specs = BootSettings::settings(loader.kind);
    if (canEdit && !drafts_.contains(loader.id)) {
        Draft draft;
        for (const auto &setting : specs)
            draft.baseline.append(loader.settingValues.value(setting.key));
        draft.values = draft.baseline;
        drafts_.insert(loader.id, draft);
    }
    const Draft draft = drafts_.value(loader.id);
    for (int i = 0; i < specs.size(); ++i) {
        const auto &setting = specs[i];
        baseline_.append(draft.baseline.value(i));
        if (QStringList{"GRUB_DEFAULT", "default_entry", "default_selection", "default"}.contains(setting.key)) {
            edits_.append(nullptr);
            continue;
        }
        const bool grubSwitch = loader.kind == LoaderKind::Grub
            && QStringList{"GRUB_DISABLE_OS_PROBER", "GRUB_DISABLE_LINUX_UUID"}.contains(setting.key);
        const bool switchValue = grubSwitch || (loader.kind == LoaderKind::Limine
            && QStringList{"quiet", "graphics", "remember_last_entry"}.contains(setting.key))
            || (loader.kind == LoaderKind::SystemdBoot
                && QStringList{"editor", "auto-entries", "auto-firmware", "beep"}.contains(setting.key));
        const bool wallpaperStyle = loader.kind == LoaderKind::Limine && setting.key == QLatin1String("wallpaper_style");
        QWidget *edit;
        const auto changed = [this, id = loader.id, i](const QString &value) {
            auto found = drafts_.find(id);
            if (found != drafts_.end()) found->values[i] = value;
        };
        if (switchValue || wallpaperStyle) {
            auto *combo = new QComboBox(fields_->parentWidget());
            BootUi::enableCopy(combo);
            combo->addItem(tr("Default"), QString());
            if (switchValue) {
                combo->addItem(tr("Enabled"), grubSwitch ? "true" : "yes");
                combo->addItem(tr("Disabled"), grubSwitch ? "false" : "no");
            } else {
                combo->addItem(tr("Centered"), "centered");
                combo->addItem(tr("Stretched"), "stretched");
                combo->addItem(tr("Tiled"), "tiled");
            }
            edit = combo;
            setEditorValue(edit, draft.values.value(i));
            connect(combo, &QComboBox::currentIndexChanged, this, [combo, changed] { changed(combo->currentData().toString()); });
        } else {
            auto *text = new QLineEdit(draft.values.value(i), fields_->parentWidget());
            text->setPlaceholderText(setting.description);
            text->setToolTip(setting.description);
            edit = text;
            connect(text, &QLineEdit::textChanged, this, changed);
        }
        edit->setObjectName(setting.key);
        fields_->addRow(setting.title, edit);
        auto *label = qobject_cast<QLabel *>(fields_->labelForField(edit));
        label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        edits_.append(edit);
    }
}

void SettingsPage::reflectOperation(const BootOperation &operation)
{
    for (const auto &loader : snapshot_.installations) {
        if (!loader.settingsEditable) continue;
        const QString kind = BootSettings::operationKind(loader.kind);
        if (operation.kind != kind) continue;
        const auto specs = BootSettings::settings(loader.kind);
        for (int i = 0; i < specs.size(); ++i) {
            const QString target = BootSettings::operationTarget(loader, specs[i].key);
            if (target != operation.target) continue;
            if (!drafts_.contains(loader.id)) {
                Draft draft;
                for (const auto &setting : specs) draft.baseline.append(loader.settingValues.value(setting.key));
                draft.values = draft.baseline;
                drafts_.insert(loader.id, draft);
            }
            drafts_[loader.id].values[i] = operation.value;
            if (selector_->currentId() == loader.id) setEditorValue(edits_[i], operation.value);
        }
    }
}

void SettingsPage::discardOperation(const BootOperation &operation)
{
    for (const auto &loader : snapshot_.manageableInstallations()) {
        const QString kind = BootSettings::operationKind(loader.kind);
        if (operation.kind != kind) continue;
        const auto specs = BootSettings::settings(loader.kind);
        for (int i = 0; i < specs.size(); ++i) {
            const QString target = BootSettings::operationTarget(loader, specs[i].key);
            if (operation.target != target) continue;
            auto found = drafts_.find(loader.id);
            if (found == drafts_.end()) continue;
            found->values[i] = found->baseline[i];
            if (selector_->currentId() == loader.id) setEditorValue(edits_[i], found->values[i]);
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
        if (!edits_[i]) continue;
        const auto &spec = specs[i];
        const QString before = baseline_.at(i);
        const QString after = editorValue(edits_[i]);
        // Also stage unchanged values: the transaction removes a previously
        // queued setting when the user returns it to its original value.
        if (before != after && !BootSettings::allowed(loader.kind, spec.key, after)) {
            QMessageBox::warning(this, tr("Invalid setting"),
                tr("Invalid value for %1. Check the field description.").arg(spec.title));
            return;
        }
        const QString kind = BootSettings::operationKind(loader.kind);
        const QString target = BootSettings::operationTarget(loader, spec.key);
        pending.append({kind, target, after, before,
                        loaderName(loader.kind) + QStringLiteral(" · ") + spec.title
                        + QStringLiteral(": ") + before + QStringLiteral(" → ") + after});
    }
    for (const auto &item : pending) emit operationRequested(item);
}
