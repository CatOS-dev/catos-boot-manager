#include "MenuPage.h"
#include "SettingsPage.h"
#include "services/BootBls.h"
#include "services/BootDeploy.h"
#include "services/BootMenus.h"
#include "services/BootSettings.h"
#include "widgets/BootUi.h"
#include "widgets/LoaderSelector.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

namespace {
constexpr int dataRole = Qt::UserRole + 4;
QJsonObject limineValues(const QJsonObject &entry);

void addNodes(const QJsonArray &nodes, QTreeWidget *tree, QTreeWidgetItem *parent = nullptr)
{
    for (const auto &value : nodes) {
        const auto node = value.toObject();
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
        item->setText(0, node.value("name").toString());
        item->setText(1, !node.value("children").toArray().isEmpty() || (node.contains("lines") && limineValues(node).value("protocol").toString().isEmpty()) ? QObject::tr("Group") : QObject::tr("Entry"));
        item->setData(0, dataRole, node);
        item->setData(0, Qt::UserRole, node.value("id").toString(node.value("selection").toString()));
        addNodes(node.value("children").toArray(), tree, item);
    }
}

QJsonArray nodes(QTreeWidget *tree, QTreeWidgetItem *parent = nullptr)
{
    QJsonArray result;
    const int count = parent ? parent->childCount() : tree->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        auto *item = parent ? parent->child(i) : tree->topLevelItem(i);
        auto object = item->data(0, dataRole).toJsonObject();
        object.insert("name", item->text(0));
        object.insert("children", nodes(tree, item));
        result.append(object);
    }
    return result;
}

QJsonObject limineValues(const QJsonObject &entry)
{
    QJsonObject result;
    QStringList initrds;
    for (const auto &line : entry.value("lines").toArray()) {
        const QString text = line.toString();
        const int colon = text.indexOf(':');
        if (colon < 0 || text.trimmed().startsWith('#')) continue;
        const QString key = text.left(colon).trimmed().toLower();
        const QString value = text.mid(colon + 1).trimmed();
        if (key == QLatin1String("module_path")) initrds.append(value);
        else result.insert(key, value);
    }
    result.insert("initramfs", initrds.join('\n'));
    return result;
}

QJsonArray replaceLimineLines(const QJsonArray &original, const QJsonObject &fields)
{
    QJsonArray result;
    const QSet<QString> keys{"protocol", "path", "module_path", "cmdline"};
    for (const auto &line : original) {
        const QString text = line.toString();
        if (!keys.contains(text.section(':', 0, 0).trimmed().toLower()) || text.trimmed().startsWith('#')) result.append(line);
    }
    for (auto it = fields.begin(); it != fields.end(); ++it)
        if (!it.value().toString().isEmpty()) result.append(it.key() + ": " + it.value().toString());
    return result;
}

int limineIndex(QTreeWidget *tree, QTreeWidgetItem *target, QTreeWidgetItem *parent = nullptr, int *counter = nullptr)
{
    int count = 0;
    if (!counter) counter = &count;
    const int size = parent ? parent->childCount() : tree->topLevelItemCount();
    for (int i = 0; i < size; ++i) {
        auto *item = parent ? parent->child(i) : tree->topLevelItem(i);
        const auto entry = item->data(0, dataRole).toJsonObject();
        const auto protocol = limineValues(entry).value("protocol").toString();
        if (!item->childCount() && (protocol == QLatin1String("bios") || protocol == QLatin1String("bios_chainload"))) continue;
        ++*counter;
        if (item == target) return *counter;
        if (entry.value("expanded").toBool()) {
            const auto result = limineIndex(tree, target, item, counter);
            if (result) return result;
        }
    }
    return 0;
}

QString defaultKey(LoaderKind kind)
{
    switch (kind) {
    case LoaderKind::Grub: return "GRUB_DEFAULT";
    case LoaderKind::Limine: return "default_entry";
    case LoaderKind::Refind: return "default_selection";
    case LoaderKind::SystemdBoot: return "default";
    default: return {};
    }
}

void addGrubEntries(const QString &content, QTreeWidget *tree)
{
    const QRegularExpression header(R"(^\s*(menuentry|submenu)\s+(['"])(.*?)\2)");
    const QRegularExpression identifier(R"grub((?:--id(?:=|\s+)|\$menuentry_id_option\s+)(?:'([^']*)'|"([^"]*)"|([^\s{]+)))grub");
    QList<QPair<QTreeWidgetItem *, int>> parents;
    int depth = 0;
    QChar quote;
    bool escape = false;
    for (const auto &line : content.split('\n')) {
        const auto match = header.match(line);
        if (quote.isNull() && match.hasMatch()) {
            auto *parent = parents.isEmpty() ? nullptr : parents.last().first;
            auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
            const int index = parent ? parent->indexOfChild(item) : tree->indexOfTopLevelItem(item);
            const auto id = identifier.match(line.mid(match.capturedEnd()));
            QString component = QString::number(index);
            for (int i = 1; i <= 3; ++i) if (!id.captured(i).isEmpty()) { component = id.captured(i); break; }
            const auto ancestor = parent ? parent->data(0, dataRole).toJsonObject() : QJsonObject();
            const auto path = [&](const QString &key, const QString &part) {
                const auto prefix = ancestor.value(key).toString();
                return prefix.isEmpty() ? part : prefix + '>' + part;
            };
            const bool group = match.captured(1) == QLatin1String("submenu");
            const QString value = path("id", component);
            item->setText(0, match.captured(3));
            item->setText(1, group ? QObject::tr("Group") : QObject::tr("Entry"));
            item->setData(0, dataRole, QJsonObject{{"id", value}, {"index", path("index", QString::number(index))},
                {"titlePath", path("titlePath", match.captured(3))}, {"group", group}});
            if (group) parents.append({item, depth + 1});
        }
        for (const auto ch : line) {
            if (escape) { escape = false; continue; }
            if (ch == QLatin1Char('\\') && quote != QLatin1Char('\'')) { escape = true; continue; }
            if (!quote.isNull()) { if (ch == quote) quote = {}; continue; }
            if (ch == QLatin1Char('#')) break;
            if (ch == QLatin1Char('\'') || ch == QLatin1Char('"')) quote = ch;
            else if (ch == QLatin1Char('{')) ++depth;
            else if (ch == QLatin1Char('}')) --depth;
        }
        escape = false;
        while (!parents.isEmpty() && depth < parents.last().second) parents.removeLast();
    }
    tree->expandToDepth(0);
}

}

MenuPage::MenuPage(QWidget *parent) : QWidget(parent)
{
    auto *layout = BootUi::page(this);
    auto *content = layout->parentWidget();
    selector_ = new LoaderSelector(content, true);
    layout->addWidget(selector_);
    rebuild_ = new QPushButton(tr("Rebuild boot menu"), content);
    rebuild_->setObjectName("rebuildBootMenu");
    layout->addWidget(rebuild_, 0, Qt::AlignRight);
    entries_ = new QWidget(content);
    auto *entries = new QVBoxLayout(entries_);
    entries->setContentsMargins(0, 0, 0, 0);
    defaultField_ = new QWidget(entries_);
    auto *defaultForm = new QFormLayout(defaultField_);
    defaultForm->setContentsMargins(0, 0, 0, 0);
    defaultChoice_ = new QComboBox(defaultField_);
    defaultChoice_->setObjectName("defaultBootEntry");
    BootUi::enableCopy(defaultChoice_);
    defaultForm->addRow(tr("Default boot entry:"), defaultChoice_);
    qobject_cast<QLabel *>(defaultForm->labelForField(defaultChoice_))->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    entries->addWidget(defaultField_);
    menu_ = new QTreeWidget(entries_);
    menu_->setObjectName("bootMenuEntries");
    BootUi::enableCopy(menu_);
    menu_->setHeaderLabels({tr("Boot entry"), tr("Type")});
    menu_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    menu_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    menu_->setMinimumHeight(180);
    menu_->setMaximumHeight(260);
    entries->addWidget(menu_);
    details_ = new QPlainTextEdit(entries_);
    details_->setReadOnly(true);
    details_->setMaximumHeight(100);
    details_->setPlaceholderText(tr("Select a boot entry to view its settings."));
    entries->addWidget(details_);
    auto *row = new QHBoxLayout;
    add_ = new QPushButton(tr("Add entry"), entries_);
    edit_ = new QPushButton(tr("Edit entry"), entries_);
    group_ = new QPushButton(tr("Add group"), entries_);
    remove_ = new QPushButton(tr("Remove entry"), entries_);
    for (auto *button : {add_, edit_, group_, remove_}) { button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed); row->addWidget(button); }
    add_->setObjectName("addMenuEntry"); edit_->setObjectName("editMenuEntry"); remove_->setObjectName("removeMenuEntry"); group_->setObjectName("addMenuGroup");
    row->addStretch(); entries->addLayout(row);
    row = new QHBoxLayout;
    up_ = new QPushButton(tr("Move up"), entries_);
    down_ = new QPushButton(tr("Move down"), entries_);
    indent_ = new QPushButton(tr("Move into group"), entries_);
    outdent_ = new QPushButton(tr("Move out of group"), entries_);
    for (auto *button : {up_, down_, indent_, outdent_}) { button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed); row->addWidget(button); }
    up_->setObjectName("moveMenuEntryUp"); down_->setObjectName("moveMenuEntryDown");
    row->addStretch(); entries->addLayout(row);
    row = new QHBoxLayout;
    timeout_ = new QPushButton(tr("Menu timeout"), entries_);
    timeout_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed); row->addWidget(timeout_);
    row->addStretch(); entries->addLayout(row);
    layout->addWidget(entries_);
    settings_ = new SettingsPage(content, true);
    layout->addWidget(settings_);
    connect(settings_, &SettingsPage::operationRequested, this, &MenuPage::operationRequested);
    connect(selector_, &LoaderSelector::installationChanged, settings_->selector(), &LoaderSelector::selectInstallation);
    connect(this, &MenuPage::operationRequested, settings_, &SettingsPage::reflectOperation);
    connect(selector_, &LoaderSelector::selectionChanged, this, &MenuPage::loadMenu);
    connect(selector_, &LoaderSelector::methodChanged, this, &MenuPage::methodChanged);
    connect(add_, &QPushButton::clicked, this, [this] { editEntry(true); });
    connect(edit_, &QPushButton::clicked, this, [this] { editEntry(false); });
    connect(group_, &QPushButton::clicked, this, [this] { editEntry(true, true); });
    connect(remove_, &QPushButton::clicked, this, &MenuPage::removeEntry);
    connect(up_, &QPushButton::clicked, this, [this] { moveEntry(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { moveEntry(1); });
    connect(indent_, &QPushButton::clicked, this, [this] { changeGroup(true); });
    connect(outdent_, &QPushButton::clicked, this, [this] { changeGroup(false); });
    connect(defaultChoice_, &QComboBox::activated, this, [this](int index) {
        if (!defaultChoice_->isEnabled()) return;
        const auto *loader = installation();
        if (!loader) return;
        const auto selected = defaultChoice_->itemData(index, Qt::UserRole + 1).value<QPersistentModelIndex>();
        if (selected.isValid()) {
            menu_->setCurrentItem(menu_->itemFromIndex(selected));
            setDefault();
        } else {
            const auto key = defaultKey(loader->kind);
            const auto value = defaultChoice_->currentData().toString();
            const auto target = BootSettings::operationTarget(*loader, key);
            if (loader->kind == LoaderKind::Limine) limineDefaults_.remove(loader->config);
            pendingDefaults_.insert(target, value);
            emit operationRequested({BootSettings::operationKind(loader->kind), target, value,
                loader->settingValues.value(key), tr("Change default boot entry")});
            updateDefaultChoices();
        }
    });
    connect(timeout_, &QPushButton::clicked, this, &MenuPage::setTimeout);
    connect(menu_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) { showSelected(item); });
    connect(menu_, &QTreeWidget::itemDoubleClicked, this, [this] { editEntry(false); });
    connect(rebuild_, &QPushButton::clicked, this, [this] {
        const auto *loader = installation();
        if (!loader || !rebuild_->isEnabled()) return;
        const QString kind = loader->kind == LoaderKind::Grub ? QStringLiteral("rebuild-grub")
            : loader->kind == LoaderKind::Limine ? QStringLiteral("rebuild-limine") : QStringLiteral("rebuild-image");
        emit operationRequested({kind, (BootDeploy::directImage(loader->kind) || loader->kind == LoaderKind::Limine) ? loader->config : BootDeploy::loaderId(loader->kind), BootDeploy::directImage(loader->kind) ? BootDeploy::loaderId(loader->kind) : QString(),
            BootDeploy::directImage(loader->kind) ? QJsonDocument::fromJson(loader->menuContent.toUtf8()).object().value("fingerprint").toString() : QString(), tr("Rebuild boot files")});
    });
    layout->addStretch();
}

const BootInstallation *MenuPage::installation() const
{
    const auto &list = selector_->installations();
    const int index = selector_->currentIndex();
    return index >= 0 && index < list.size() ? &list[index] : nullptr;
}

void MenuPage::updateSnapshot(const BootSnapshot &snapshot, bool preserveDrafts)
{
    snapshot_ = snapshot;
    if (!preserveDrafts) { drafts_.clear(); limineDefaults_.clear(); pendingDefaults_.clear(); }
    settings_->updateSnapshot(snapshot, preserveDrafts);
    selector_->updateSnapshot(snapshot);
    settings_->selector()->selectInstallation(selector_->method(), selector_->currentId());
}

void MenuPage::updateTransaction(const BootTransaction &transaction)
{
    QMap<QString, QJsonObject> next;
    pendingDefaults_.clear();
    for (const auto &operation : transaction.operations())
        if (operation.target == QLatin1String("GRUB_DEFAULT") || operation.target.endsWith("|default_entry")
            || operation.target.endsWith("|default_selection") || operation.target.endsWith("|default"))
            pendingDefaults_.insert(operation.target, operation.value);
    for (const auto &operation : transaction.operations())
        if (operation.kind == QLatin1String("menu-write")) next.insert(operation.target, QJsonDocument::fromJson(operation.value.toUtf8()).object());
    if (!staging_) {
        for (auto it = limineDefaults_.begin(); it != limineDefaults_.end(); ) {
            if (!pendingDefaults_.contains(it.key() + "|default_entry")) it = limineDefaults_.erase(it);
            else ++it;
        }
    }
    if (next == drafts_) { updateDefaultChoices(); return; }
    drafts_ = next;
    if (!staging_) loadMenu();
}

void MenuPage::selectMethod(LoaderKind kind) { selector_->selectMethod(kind); }

void MenuPage::loadMenu()
{
    const auto previous = menu_->currentItem() ? menu_->currentItem()->data(0, dataRole).toJsonObject() : QJsonObject();
    const auto identity = [](const QJsonObject &entry) { return entry.value("selection").toString(entry.value("id").toString(entry.value("path").toString())); };
    const auto selected = identity(previous);
    const auto restore = qScopeGuard([&] {
        updateDefaultChoices();
        if (selected.isEmpty()) return;
        for (QTreeWidgetItemIterator it(menu_); *it; ++it)
            if (identity((*it)->data(0, dataRole).toJsonObject()) == selected) { menu_->setCurrentItem(*it); break; }
    });
    menu_->clear(); details_->clear();
    const auto kind = selector_->method();
    const bool limine = kind == LoaderKind::Limine;
    const bool refind = kind == LoaderKind::Refind;
    const bool systemd = kind == LoaderKind::SystemdBoot;
    const bool image = BootDeploy::directImage(kind);
    const auto *loader = installation();
    menu_->setVisible(!image);
    for (auto *button : {add_, edit_, remove_, timeout_}) button->setVisible(limine || refind || systemd);
    group_->setVisible(limine);
    for (auto *button : {up_, down_}) button->setVisible(limine || refind);
    indent_->setVisible(limine); outdent_->setVisible(limine);
    rebuild_->setVisible(kind == LoaderKind::Grub || limine || image);
    rebuild_->setText(image ? tr("Rebuild image") : tr("Rebuild boot menu"));
    rebuild_->setEnabled(loader && loader->settingsEditable && (image || loader->esp.isEmpty()));
    if (limine) {
        rebuild_->setEnabled(loader && loader->settingsEditable && !drafts_.contains(loader->config));
        rebuild_->setToolTip(loader && drafts_.contains(loader->config) ? tr("Apply menu changes before rebuilding.") : QString());
    }
    for (auto *button : {add_, edit_, group_, remove_, up_, down_, indent_, outdent_, timeout_}) button->setEnabled(false);
    if (!loader) { details_->setPlainText(tr("No installed boot entry was detected.")); return; }
    add_->setEnabled(loader->settingsEditable && !image);
    group_->setEnabled(limine && loader->settingsEditable);
    timeout_->setEnabled(loader->settingsEditable);
    if (!loader->menuMessage.isEmpty()) { details_->setPlainText(loader->menuMessage); return; }
    if (limine || refind) {
        auto document = QJsonDocument::fromJson(loader->menuContent.toUtf8()).object();
        QJsonArray entries = document.value("entries").toArray();
        if (limine && drafts_.contains(loader->config)) entries = drafts_[loader->config].value("entries").toArray();
        if (refind) {
            QMap<QString, int> totals, positions;
            for (const auto &entry : entries) ++totals[entry.toObject().value("source").toString()];
            QJsonArray merged;
            for (const auto &entry : entries) {
                const auto source = entry.toObject().value("source").toString();
                if (!drafts_.contains(source)) { merged.append(entry); continue; }
                const auto local = drafts_[source].value("entries").toArray();
                const int position = positions[source]++;
                bool ordered = true;
                int lastStart = -1;
                for (const auto &value : local) {
                    const auto object = value.toObject();
                    if (!object.contains("start")) continue;
                    const int start = object.value("start").toInt(); ordered &= start > lastStart; lastStart = start;
                }
                if (ordered) {
                    for (const auto &value : local)
                        if (value.toObject().contains("start") && value.toObject().value("start") == entry.toObject().value("start")) merged.append(value);
                    if (positions[source] == totals[source])
                        for (const auto &value : local) if (!value.toObject().contains("start")) merged.append(value);
                } else {
                    if (position < local.size()) merged.append(local[position]);
                    if (positions[source] == totals[source])
                        for (int extra = positions[source]; extra < local.size(); ++extra) merged.append(local[extra]);
                }
            }
            for (const auto &source : document.value("sources").toArray()) {
                const auto path = source.toObject().value("path").toString();
                if (!totals.contains(path) && drafts_.contains(path))
                    for (const auto &entry : drafts_[path].value("entries").toArray()) merged.append(entry);
            }
            entries = merged;
        }
        addNodes(entries, menu_);
        menu_->expandToDepth(1);
    } else if (systemd) {
        QString error;
        auto entries = BootBls::entries(loader->menuContent, loader->esp, &error);
        if (!error.isEmpty()) { details_->setPlainText(error); return; }
        QSet<QString> seen;
        for (int entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
            auto value = entries[entryIndex];
            auto entry = value.toObject();
            const QString source = entry.value("path").toString();
            seen.insert(source);
            if (drafts_.contains(source)) {
                if (drafts_[source].value("remove").toBool()) { entries[entryIndex] = QJsonObject(); continue; }
                entry.insert("configuration", QJsonObject{{"source", source}, {"values", drafts_[source].value("values")},
                    {"fingerprint", entry.value("configuration").toObject().value("fingerprint")}});
            }
            entries[entryIndex] = entry;
        }
        for (auto it = drafts_.begin(); it != drafts_.end(); ++it)
            if (it.value().value("format").toString() == QLatin1String("bls") && it.key().startsWith(loader->esp + "/loader/entries/")
                && !seen.contains(it.key()) && !it.value().value("remove").toBool())
                entries.append(QJsonObject{{"id", QFileInfo(it.key()).fileName()}, {"path", it.key()}, {"type", "type1"},
                    {"root", loader->esp}, {"configuration", QJsonObject{{"source", it.key()}, {"values", it.value().value("values")}, {"fingerprint", BootMenus::fingerprint({})}}}});
        for (const auto &value : entries) {
            auto entry = value.toObject();
            if (entry.isEmpty()) continue;
            const auto fields = entry.value("configuration").toObject().value("values").toObject();
            entry.insert("name", fields.value("title").toString(entry.value("showTitle").toString(entry.value("id").toString())));
            auto *item = new QTreeWidgetItem(menu_);
            item->setText(0, entry.value("name").toString());
            item->setText(1, entry.value("type").toString() == QLatin1String("type2") ? tr("UKI") : tr("Entry"));
            item->setData(0, dataRole, entry);
            item->setData(0, Qt::UserRole, entry.value("id").toString());
        }
        details_->setPlainText(snapshot_.systemdDefaultOverride.isEmpty() ? tr("Select a boot entry to edit its settings.")
            : tr("Default boot entry: %1.").arg(snapshot_.systemdDefaultOverride));
    } else if (kind == LoaderKind::Grub) {
        addGrubEntries(loader->menuContent, menu_);
    } else details_->setPlainText(tr("Image: %1").arg(QFileInfo(loader->binary).fileName()));
}

void MenuPage::showSelected(QTreeWidgetItem *item)
{
    const auto *loader = installation();
    if (!loader) return;
    const bool selected = item;
    const auto entry = item ? item->data(0, dataRole).toJsonObject() : QJsonObject();
    const bool bls = loader->kind == LoaderKind::SystemdBoot;
    const bool editable = selected && loader->settingsEditable && (loader->kind == LoaderKind::Limine || loader->kind == LoaderKind::Refind
        || (bls && entry.value("type").toString() == QLatin1String("type1")));
    edit_->setEnabled(editable || (selected && bls && entry.value("type").toString() == QLatin1String("type2")));
    remove_->setEnabled(editable);
    for (auto *button : {up_, down_, indent_, outdent_}) button->setEnabled(editable);
    if (!item) return;
    const auto fields = loader->kind == LoaderKind::Limine ? limineValues(entry)
        : bls ? entry.value("configuration").toObject().value("values").toObject() : entry.value("values").toObject();
    QStringList details{item->text(0)};
    const QString kernel = fields.value(loader->kind == LoaderKind::Limine ? "path" : bls ? "linux" : "loader").toString();
    if (!kernel.isEmpty()) details.append(tr("Kernel / program: %1").arg(kernel));
    const QString options = fields.value(loader->kind == LoaderKind::Limine ? "cmdline" : "options").toString(entry.value("options").toString());
    if (!options.isEmpty()) details.append(tr("Parameters: %1").arg(options));
    details_->setPlainText(details.join('\n'));
}

void MenuPage::stageMenu(const QString &source, const QJsonObject &data, const QString &hash)
{
    bool unchanged = false;
    const auto *loader = installation();
    const auto document = QJsonDocument::fromJson(loader->menuContent.toUtf8()).object();
    const auto format = data.value("format").toString();
    if (format == QLatin1String("bls")) {
        QString error;
        for (const auto &entry : BootBls::entries(loader->menuContent, loader->esp, &error))
            if (entry.toObject().value("path").toString() == source)
                unchanged = !data.value("remove").toBool() && entry.toObject().value("configuration").toObject().value("values") == data.value("values");
        if (data.value("remove").toBool() && hash == BootMenus::fingerprint({})) unchanged = true;
    } else {
        auto original = document.value("entries").toArray();
        if (format == QLatin1String("refind")) {
            QJsonArray local;
            for (const auto &entry : original) if (entry.toObject().value("source").toString() == source) local.append(entry);
            original = local;
        }
        unchanged = original == data.value("entries").toArray();
    }
    if (unchanged) drafts_.remove(source); else drafts_.insert(source, data);
    staging_ = true;
    emit operationRequested({"menu-write", source, unchanged ? QString() : QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)), hash, tr("Update boot menu")});
    if (format == QLatin1String("limine") && limineDefaults_.contains(source)) {
        int index = 0;
        for (QTreeWidgetItemIterator it(menu_); *it; ++it)
            if ((*it)->data(0, dataRole).toJsonObject().value("selection").toString() == limineDefaults_.value(source)) {
                for (auto *parent = (*it)->parent(); parent; parent = parent->parent()) {
                    auto object = parent->data(0, dataRole).toJsonObject(); object.insert("expanded", true); parent->setData(0, dataRole, object);
                }
                index = limineIndex(menu_, *it); break;
            }
        if (index > 0) emit operationRequested({"limine-setting", source + "|default_entry", QString::number(index), loader->settingValues.value("default_entry"), tr("Set default boot entry: %1").arg(index)});
    }
    staging_ = false;
    loadMenu();
}

void MenuPage::editEntry(bool create, bool group)
{
    const auto *loader = installation();
    if (!loader || !loader->settingsEditable) return;
    auto *selected = menu_->currentItem();
    if (!create && !selected) return;
    const bool limine = loader->kind == LoaderKind::Limine;
    const bool refind = loader->kind == LoaderKind::Refind;
    const bool systemd = loader->kind == LoaderKind::SystemdBoot;
    QJsonObject entry = create ? QJsonObject() : selected->data(0, dataRole).toJsonObject();
    if (systemd && entry.value("type").toString() == QLatin1String("type2")) {
        for (const auto &image : snapshot_.installations) if (image.kind == LoaderKind::Uki && image.binary == entry.value("path").toString()) {
            selector_->selectInstallation(image.kind, image.id); return;
        }
        return;
    }
    const auto originalFields = limine ? limineValues(entry) : systemd ? entry.value("configuration").toObject().value("values").toObject() : entry.value("values").toObject();
    QDialog dialog(this);
    dialog.setWindowTitle(create ? group ? tr("Add group") : tr("Add boot entry") : tr("Edit boot entry"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    QLineEdit name(entry.value("name").toString(), &dialog); name.setObjectName("entryName"); form->addRow(tr("Name:"), &name);
    QLineEdit id(create ? QStringLiteral("catos-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) + QStringLiteral(".conf") : entry.value("id").toString(), &dialog);
    if (systemd) { id.setReadOnly(!create); id.setObjectName("entryId"); form->addRow(tr("Entry file:"), &id); }
    QComboBox type(&dialog);
    type.addItem(tr("Linux kernel"), "linux"); type.addItem(tr("EFI program"), "efi");
    if (refind) type.hide();
    if (limine) type.addItem(tr("Group"), "group");
    const auto protocol = (group || (limine && !create && !originalFields.contains("protocol"))) ? QStringLiteral("group") : originalFields.value("protocol").toString(originalFields.contains("efi") ? QStringLiteral("efi") : QStringLiteral("linux"));
    if (limine && !protocol.isEmpty() && type.findData(protocol) < 0) type.addItem(protocol, protocol);
    type.setCurrentIndex(qMax(0, type.findData(protocol)));
    if (!refind) form->addRow(tr("Boot type:"), &type);
    QLineEdit kernel(originalFields.value(limine ? "path" : refind ? "loader" : protocol == QLatin1String("efi") ? "efi" : "linux").toString(), &dialog);
    kernel.setObjectName("entryKernel"); form->addRow(tr("Kernel / program:"), &kernel);
    QPlainTextEdit initrd(&dialog); initrd.setMaximumHeight(70); initrd.setObjectName("entryInitramfs");
    if (systemd) { QStringList list; for (const auto &value : originalFields.value("initrd").toArray()) list.append(value.toString()); initrd.setPlainText(list.join('\n')); }
    else initrd.setPlainText(originalFields.value(limine ? "initramfs" : "initrd").toString());
    form->addRow(tr("Initramfs:"), &initrd);
    QLineEdit parameters(originalFields.value(limine ? "cmdline" : "options").toString(), &dialog);
    parameters.setObjectName("entryParameters"); form->addRow(tr("Kernel parameters:"), &parameters);
    QLineEdit icon(originalFields.value("icon").toString(), &dialog);
    QLineEdit volume(originalFields.value("volume").toString(), &dialog);
    if (refind) { form->addRow(tr("Icon:"), &icon); form->addRow(tr("Volume:"), &volume); }
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&type, &QComboBox::currentIndexChanged, &dialog, [&] {
        const bool isGroup = type.currentData().toString() == QLatin1String("group");
        kernel.setEnabled(!isGroup); initrd.setEnabled(!isGroup); parameters.setEnabled(!isGroup);
    });
    if (type.currentData().toString() == QLatin1String("group")) { kernel.setEnabled(false); initrd.setEnabled(false); parameters.setEnabled(false); }
    if (dialog.exec() != QDialog::Accepted) return;
    if (name.text().trimmed().isEmpty() || name.text().contains('\n') || name.text().contains('\r')
        || (type.currentData().toString() != QLatin1String("group") && kernel.text().trimmed().isEmpty())) {
        QMessageBox::warning(this, tr("Invalid boot entry"), tr("Enter a name and select the kernel or EFI program.")); return;
    }
    const auto document = QJsonDocument::fromJson(loader->menuContent.toUtf8()).object();
    QString source = loader->config;
    QString hash;
    if (systemd) {
        if (!BootBls::validId(id.text()) || !id.text().endsWith(QLatin1String(".conf"))) {
            QMessageBox::warning(this, tr("Invalid boot entry"), tr("Use a valid entry name ending in .conf.")); return;
        }
        source = loader->esp + "/loader/entries/" + id.text();
        auto fields = originalFields;
        fields.insert("title", name.text().trimmed());
        const bool efi = type.currentData().toString() == QLatin1String("efi");
        fields.remove(efi ? "linux" : "efi"); fields.insert(efi ? "efi" : "linux", kernel.text().trimmed());
        QStringList paths = initrd.toPlainText().split('\n', Qt::SkipEmptyParts);
        for (auto &path : paths) path = path.trimmed();
        fields.insert("initrd", QJsonArray::fromStringList(paths)); fields.insert("options", parameters.text());
        hash = create ? BootMenus::fingerprint({}) : entry.value("configuration").toObject().value("fingerprint").toString();
        stageMenu(source, {{"format", "bls"}, {"values", fields}}, hash);
        return;
    }
    entry.insert("name", name.text().trimmed());
    if (limine) {
        QJsonObject fields;
        if (type.currentData().toString() != QLatin1String("group")) {
            fields = {{"protocol", type.currentData().toString()}, {"path", kernel.text().trimmed()}, {"cmdline", parameters.text()}};
        }
        auto lines = replaceLimineLines(entry.value("lines").toArray(), fields);
        for (const auto &path : initrd.toPlainText().split('\n', Qt::SkipEmptyParts)) if (type.currentData().toString() != QLatin1String("group")) lines.append("module_path: " + path.trimmed());
        entry.insert("lines", lines);
        hash = document.value("fingerprint").toString();
    } else {
        auto fields = originalFields;
        fields.insert("loader", kernel.text().trimmed()); fields.insert("initrd", initrd.toPlainText().trimmed());
        fields.insert("options", parameters.text()); fields.insert("icon", icon.text()); fields.insert("volume", volume.text());
        entry.insert("values", fields);
        source = create ? loader->config : entry.value("source").toString(); entry.insert("source", source);
        for (const auto &value : document.value("sources").toArray()) if (value.toObject().value("path").toString() == source) hash = value.toObject().value("fingerprint").toString();
    }
    if (create) {
        if (limine) entry.insert("selection", QUuid::createUuid().toString(QUuid::WithoutBraces));
        entry.insert("children", QJsonArray());
        auto *parent = limine && selected && limineValues(selected->data(0, dataRole).toJsonObject()).value("protocol").toString().isEmpty() ? selected : nullptr;
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(menu_);
        item->setText(0, name.text().trimmed()); item->setData(0, dataRole, entry); menu_->setCurrentItem(item);
    } else { selected->setText(0, name.text().trimmed()); selected->setData(0, dataRole, entry); }
    QJsonArray entries = nodes(menu_);
    if (refind) { QJsonArray local; for (const auto &value : entries) if (value.toObject().value("source").toString() == source) local.append(value); entries = local; }
    stageMenu(source, {{"format", limine ? "limine" : "refind"}, {"entries", entries}}, hash);
}

void MenuPage::removeEntry()
{
    const auto *loader = installation(); auto *item = menu_->currentItem();
    if (!loader || !item || !remove_->isEnabled()) return;
    if (loader->kind == LoaderKind::Limine && limineDefaults_.contains(loader->config)) {
        const QString selection = limineDefaults_.value(loader->config);
        for (QTreeWidgetItemIterator it(menu_); *it; ++it) {
            if ((*it)->data(0, dataRole).toJsonObject().value("selection").toString() != selection) continue;
            for (auto *ancestor = *it; ancestor; ancestor = ancestor->parent()) {
                if (ancestor != item) continue;
                QMessageBox::warning(this, tr("Default boot entry"),
                    tr("Choose another default boot entry before removing this entry."));
                return;
            }
        }
    }
    if (QMessageBox::question(this, tr("Remove boot entry"), tr("Remove %1?").arg(item->text(0))) != QMessageBox::Yes) return;
    const auto entry = item->data(0, dataRole).toJsonObject();
    const auto document = QJsonDocument::fromJson(loader->menuContent.toUtf8()).object();
    QString source = loader->config;
    QString hash = document.value("fingerprint").toString();
    if (loader->kind == LoaderKind::SystemdBoot) {
        stageMenu(entry.value("path").toString(), {{"format", "bls"}, {"remove", true}}, entry.value("configuration").toObject().value("fingerprint").toString()); return;
    }
    if (loader->kind == LoaderKind::Refind) {
        source = entry.value("source").toString();
        for (const auto &value : document.value("sources").toArray()) if (value.toObject().value("path").toString() == source) hash = value.toObject().value("fingerprint").toString();
    }
    delete item;
    auto entries = nodes(menu_);
    if (loader->kind == LoaderKind::Refind) { QJsonArray local; for (const auto &value : entries) if (value.toObject().value("source").toString() == source) local.append(value); entries = local; }
    stageMenu(source, {{"format", loader->kind == LoaderKind::Limine ? "limine" : "refind"}, {"entries", entries}}, hash);
}

void MenuPage::moveEntry(int direction)
{
    const auto *loader = installation(); auto *item = menu_->currentItem();
    if (!loader || !item) return;
    auto *parent = item->parent();
    const int index = parent ? parent->indexOfChild(item) : menu_->indexOfTopLevelItem(item);
    const int count = parent ? parent->childCount() : menu_->topLevelItemCount();
    if (index + direction < 0 || index + direction >= count) return;
    auto *sibling = parent ? parent->child(index + direction) : menu_->topLevelItem(index + direction);
    const QString source = item->data(0, dataRole).toJsonObject().value("source").toString(loader->config);
    if (loader->kind == LoaderKind::Refind && sibling->data(0, dataRole).toJsonObject().value("source").toString() != source) return;
    if (parent) { parent->takeChild(index); parent->insertChild(index + direction, item); }
    else { menu_->takeTopLevelItem(index); menu_->insertTopLevelItem(index + direction, item); }
    const auto document = QJsonDocument::fromJson(loader->menuContent.toUtf8()).object();
    QString hash = document.value("fingerprint").toString();
    auto entries = nodes(menu_);
    if (loader->kind == LoaderKind::Refind) {
        QJsonArray local; for (const auto &value : entries) if (value.toObject().value("source").toString() == source) local.append(value); entries = local;
        for (const auto &value : document.value("sources").toArray()) if (value.toObject().value("path").toString() == source) hash = value.toObject().value("fingerprint").toString();
    }
    stageMenu(source, {{"format", loader->kind == LoaderKind::Limine ? "limine" : "refind"}, {"entries", entries}}, hash);
}

void MenuPage::changeGroup(bool indent)
{
    const auto *loader = installation(); auto *item = menu_->currentItem();
    if (!loader || loader->kind != LoaderKind::Limine || !item) return;
    auto *parent = item->parent();
    const int index = parent ? parent->indexOfChild(item) : menu_->indexOfTopLevelItem(item);
    if (indent) {
        if (!index) return;
        auto *previous = parent ? parent->child(index - 1) : menu_->topLevelItem(index - 1);
        if (!limineValues(previous->data(0, dataRole).toJsonObject()).value("protocol").toString().isEmpty()) return;
        if (parent) parent->takeChild(index); else menu_->takeTopLevelItem(index);
        previous->addChild(item);
    } else {
        if (!parent) return;
        auto *grandparent = parent->parent();
        parent->takeChild(index);
        if (grandparent) grandparent->insertChild(grandparent->indexOfChild(parent) + 1, item);
        else menu_->insertTopLevelItem(menu_->indexOfTopLevelItem(parent) + 1, item);
    }
    const auto document = QJsonDocument::fromJson(loader->menuContent.toUtf8()).object();
    stageMenu(loader->config, {{"format", "limine"}, {"entries", nodes(menu_)}}, document.value("fingerprint").toString());
}

void MenuPage::setDefault()
{
    const auto *loader = installation(); const auto *item = menu_->currentItem();
    if (!loader || !item || !loader->settingsEditable || item->childCount()
        || item->data(0, dataRole).toJsonObject().value("group").toBool()) return;
    const auto selectedName = item->text(0);
    QString key, value;
    QString menu;
    if (loader->kind == LoaderKind::SystemdBoot) {
        if (!snapshot_.systemdDefaultOverride.isEmpty()) return;
        key = "default"; value = item->data(0, Qt::UserRole).toString();
        if (!BootBls::validId(value)) return;
        menu = BootBls::fingerprint(loader->menuContent);
    } else if (loader->kind == LoaderKind::Limine) {
        auto *selected = const_cast<QTreeWidgetItem *>(item);
        bool changed = false;
        for (auto *parent = selected->parent(); parent; parent = parent->parent()) {
            auto data = parent->data(0, dataRole).toJsonObject();
            changed |= !data.value("expanded").toBool(); data.insert("expanded", true); parent->setData(0, dataRole, data);
        }
        key = "default_entry"; value = QString::number(limineIndex(menu_, selected));
        limineDefaults_.insert(loader->config, selected->data(0, dataRole).toJsonObject().value("selection").toString());
        if (changed) {
            const auto document = QJsonDocument::fromJson(loader->menuContent.toUtf8()).object();
            stageMenu(loader->config, {{"format", "limine"}, {"entries", nodes(menu_)}}, document.value("fingerprint").toString());
        }
    }
    else if (loader->kind == LoaderKind::Refind) { key = "default_selection"; value = item->text(0); }
    else if (loader->kind == LoaderKind::Grub) { key = "GRUB_DEFAULT"; value = item->data(0, dataRole).toJsonObject().value("id").toString(); }
    else return;
    pendingDefaults_.insert(BootSettings::operationTarget(*loader, key), value);
    emit operationRequested({BootSettings::operationKind(loader->kind), BootSettings::operationTarget(*loader, key), value,
        loader->settingValues.value(key), tr("Set default boot entry: %1").arg(selectedName), {}, menu});
    updateDefaultChoices();
}

void MenuPage::updateDefaultChoices()
{
    defaultChoice_->clear();
    const auto *loader = installation();
    const QString key = loader ? defaultKey(loader->kind) : QString();
    defaultField_->setVisible(!key.isEmpty());
    defaultChoice_->setEnabled(loader && loader->settingsEditable && menu_->topLevelItemCount()
        && (loader->kind != LoaderKind::SystemdBoot || snapshot_.systemdDefaultOverride.isEmpty()));
    if (key.isEmpty()) return;
    const auto target = BootSettings::operationTarget(*loader, key);
    QString current = pendingDefaults_.value(target, loader->settingValues.value(key));
    if (loader->kind == LoaderKind::SystemdBoot && !snapshot_.systemdDefaultOverride.isEmpty()) current = snapshot_.systemdDefaultOverride;
    defaultChoice_->addItem(tr("Loader default"), QString());
    if (loader->kind == LoaderKind::Grub) defaultChoice_->addItem(tr("Last selected entry"), "saved");
    int selected = defaultChoice_->findData(current);
    for (QTreeWidgetItemIterator it(menu_); *it; ++it) {
        auto *item = *it;
        const auto entry = item->data(0, dataRole).toJsonObject();
        const auto protocol = limineValues(entry).value("protocol").toString();
        if (item->childCount() || entry.value("group").toBool()
            || (loader->kind == LoaderKind::Limine && protocol.isEmpty()) || protocol == QLatin1String("bios") || protocol == QLatin1String("bios_chainload")) continue;
        QString value;
        if (loader->kind == LoaderKind::Limine) value = QString::number(limineIndex(menu_, item));
        else if (loader->kind == LoaderKind::Refind) value = item->text(0);
        else value = entry.value("id").toString();
        QStringList title{item->text(0)};
        for (auto *parent = item->parent(); parent; parent = parent->parent()) title.prepend(parent->text(0));
        const int index = defaultChoice_->count();
        defaultChoice_->addItem(title.join(" / "), value);
        defaultChoice_->setItemData(index, QVariant::fromValue(QPersistentModelIndex(menu_->indexFromItem(item))), Qt::UserRole + 1);
        bool matches = current == value && (loader->kind != LoaderKind::Limine || value != QLatin1String("0"));
        if (loader->kind == LoaderKind::Limine && limineDefaults_.contains(loader->config))
            matches = entry.value("selection").toString() == limineDefaults_.value(loader->config);
        if (loader->kind == LoaderKind::Grub) matches |= current == entry.value("index").toString() || current == entry.value("titlePath").toString();
        if (matches) selected = index;
    }
    if (selected < 0) { defaultChoice_->addItem(tr("Current default"), current); selected = defaultChoice_->count() - 1; }
    defaultChoice_->setCurrentIndex(selected);
}

void MenuPage::setTimeout()
{
    const auto *loader = installation();
    if (!loader) return;
    bool ok = false;
    const QString value = QInputDialog::getText(this, tr("Menu timeout"), tr("Waiting time:"), QLineEdit::Normal,
        loader->settingValues.value("timeout"), &ok).trimmed();
    if (!ok) return;
    if (!BootSettings::allowed(loader->kind, "timeout", value)) {
        QMessageBox::warning(this, tr("Invalid timeout"), tr("Enter a valid waiting time.")); return;
    }
    emit operationRequested({BootSettings::operationKind(loader->kind), BootSettings::operationTarget(*loader, "timeout"), value,
        loader->settingValues.value("timeout"), tr("Change menu timeout: %1").arg(value)});
}
