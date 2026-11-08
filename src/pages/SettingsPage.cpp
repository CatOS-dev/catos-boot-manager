#include "SettingsPage.h"
#include "services/BootSettings.h"
#include "widgets/BootUi.h"
#include "widgets/LoaderSelector.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTimer>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
void setEditorValue(QWidget *widget, const QString &value)
{
    if (!widget) return;
    const QSignalBlocker blocker(widget);
    if (auto *combo = qobject_cast<QComboBox *>(widget)) {
        int index = combo->findData(value);
        if (index < 0) { combo->addItem(value, value); index = combo->count() - 1; }
        combo->setCurrentIndex(index);
    } else qobject_cast<QLineEdit *>(widget)->setText(value);
}
}

SettingsPage::SettingsPage(QWidget *parent, bool embedded) : QWidget(parent)
{
    layout_ = BootUi::page(this, embedded);
    auto *content = embedded ? this : layout_->parentWidget();
    if (!embedded) BootUi::section(layout_, content, tr("Choose installation"));
    selector_ = new LoaderSelector(content, true);
    selector_->setObjectName(QStringLiteral("settingsInstallation"));
    selector_->setVisible(!embedded);
    layout_->addWidget(selector_);
    // Embedded below the boot menu, the status line belongs to this section,
    // so it goes under the heading rather than above it.
    location_ = BootUi::muted({}, content);
    if (!embedded) layout_->addWidget(location_);
    BootUi::section(layout_, content, tr("Configuration"));
    if (embedded) layout_->addWidget(location_);
    embedded_ = embedded;
    fields_ = new QFormLayout;
    fields_->setSpacing(11);
    layout_->addLayout(fields_);
    layout_->addStretch();
    connect(selector_, &LoaderSelector::selectionChanged, this, [this] {
        manageable_ = selector_->installations();
        showSettings();
    });
    connect(selector_, &LoaderSelector::methodChanged, this, &SettingsPage::methodChanged);
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
    const int index = selector_->currentIndex();
    if (index < 0 || index >= manageable_.size()) {
        location_->setText(tr("No boot loader configuration is available."));
        location_->setVisible(true);
        return;
    }
    const auto &loader = manageable_.at(index);
    const bool canEdit = loader.settingsEditable;
    // The embedding page already explains that edits are staged.
    location_->setText(canEdit ? (embedded_ ? QString() : tr("Edits are added to pending changes automatically. Review and apply to save them."))
                               : loader.settingsMessage);
    location_->setVisible(!location_->text().isEmpty());
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
        const bool valid = draft.values.value(i) == draft.baseline.value(i)
            || BootSettings::allowed(loader.kind, setting.key, draft.values.value(i));
        // Fields carry no permanent description, so the error names the
        // expected format to say what would be accepted.
        const QString invalid = tr("Invalid value. %1").arg(setting.description);
        auto *help = BootUi::muted(valid ? QString() : invalid, fields_->parentWidget());
        help->setObjectName(setting.key + "Help");
        help->setVisible(!valid);
        // A correction clears the error at once, but a new error waits until
        // typing pauses or the field loses focus, so a half-typed value such
        // as "1920x" is not flagged. Values are still staged on every change.
        const auto changed = [this, loader, i, help, setting, invalid](const QString &value) {
            const bool valid = value == drafts_.value(loader.id).baseline.value(i)
                || BootSettings::allowed(loader.kind, setting.key, value);
            help->setText(valid ? QString() : invalid);
            if (valid) help->setVisible(false);
            stageValue(loader, i, value);
        };
        const auto reveal = [help] { help->setVisible(!help->text().isEmpty()); };
        if (setting.key == QLatin1String("default_kernel")) {
            auto *combo = new QComboBox(fields_->parentWidget());
            BootUi::enableCopy(combo);
            combo->addItem(tr("Choose a kernel"), QString());
            const auto root = loader.settingValues.value("root");
            QMap<QString, int> counts;
            for (const auto &value : snapshot_.kernels) {
                const auto kernel = value.toObject();
                if (kernel.value("root").toString() == root) counts[kernel.value("name").toString()]++;
            }
            for (const auto &value : snapshot_.kernels) {
                const auto kernel = value.toObject();
                if (kernel.value("root").toString() != root) continue;
                const auto name = kernel.value("name").toString();
                const auto version = kernel.value("version").toString();
                combo->addItem(name + " · " + version, counts.value(name) > 1 ? name + '-' + version : name);
            }
            edit = combo;
            setEditorValue(edit, draft.values.value(i));
            connect(combo, &QComboBox::currentIndexChanged, this, [combo, changed, reveal] { changed(combo->currentData().toString()); reveal(); });
        } else if (switchValue || wallpaperStyle) {
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
            connect(combo, &QComboBox::currentIndexChanged, this, [combo, changed, reveal] { changed(combo->currentData().toString()); reveal(); });
        } else {
            auto *text = new QLineEdit(draft.values.value(i), fields_->parentWidget());
            edit = text;
            auto *pause = new QTimer(text);
            pause->setSingleShot(true);
            pause->setInterval(800);
            connect(text, &QLineEdit::textChanged, this, [changed, pause](const QString &value) { changed(value); pause->start(); });
            connect(pause, &QTimer::timeout, help, reveal);
            connect(text, &QLineEdit::editingFinished, help, [pause, reveal] { pause->stop(); reveal(); });
        }
        edit->setObjectName(setting.key);
        auto *field = new QWidget(fields_->parentWidget());
        auto *fieldLayout = new QVBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(3);
        fieldLayout->addWidget(edit);
        fieldLayout->addWidget(help);
        fields_->addRow(setting.title, field);
        auto *label = qobject_cast<QLabel *>(fields_->labelForField(field));
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
            const auto current = drafts_.value(loader.id);
            if (current.values.value(i) != current.baseline.value(i)
                && !BootSettings::allowed(loader.kind, specs[i].key, current.values.value(i))) continue;
            drafts_[loader.id].values[i] = operation.value;
            if (selector_->currentId() == loader.id) {
                setEditorValue(edits_[i], operation.value);
                if (auto *help = findChild<QLabel *>(specs[i].key + "Help")) {
                    help->clear();
                    help->setVisible(false);
                }
            }
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
            if (selector_->currentId() == loader.id) {
                setEditorValue(edits_[i], found->values[i]);
                if (auto *help = findChild<QLabel *>(specs[i].key + "Help")) {
                    help->clear();
                    help->setVisible(false);
                }
            }
        }
    }
}

void SettingsPage::stageValue(const BootInstallation &loader, int index, const QString &value)
{
    auto draft = drafts_.find(loader.id);
    if (draft == drafts_.end()) return;
    draft->values[index] = value;
    const auto spec = BootSettings::settings(loader.kind).at(index);
    const QString before = draft->baseline.at(index);
    if (value == before || BootSettings::allowed(loader.kind, spec.key, value))
        emit operationRequested({BootSettings::operationKind(loader.kind),
            BootSettings::operationTarget(loader, spec.key), value, before,
            loaderName(loader.kind) + " · " + spec.title + ": " + before + " → " + value});
    emit draftStateChanged();
}

bool SettingsPage::hasInvalidDrafts() const
{
    for (const auto &loader : snapshot_.installations) {
        const auto draft = drafts_.constFind(loader.id);
        if (draft == drafts_.cend()) continue;
        const auto specs = BootSettings::settings(loader.kind);
        for (int i = 0; i < specs.size(); ++i)
            if (draft->values.value(i) != draft->baseline.value(i)
                && !BootSettings::allowed(loader.kind, specs[i].key, draft->values.value(i))) return true;
    }
    return false;
}

bool SettingsPage::hasDraftChanges() const
{
    for (const auto &draft : drafts_)
        if (draft.values != draft.baseline) return true;
    return false;
}

void SettingsPage::acknowledgeApplied(const BootTransaction &transaction)
{
    for (const auto &loader : snapshot_.installations) {
        auto draft = drafts_.find(loader.id);
        if (draft == drafts_.end()) continue;
        const auto specs = BootSettings::settings(loader.kind);
        for (int i = 0; i < specs.size(); ++i)
            for (const auto &operation : transaction.operations())
                if (operation.kind == BootSettings::operationKind(loader.kind)
                    && operation.target == BootSettings::operationTarget(loader, specs[i].key))
                    draft->baseline[i] = operation.value;
    }
}
