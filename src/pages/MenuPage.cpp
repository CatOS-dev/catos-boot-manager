#include "MenuPage.h"
#include "services/BootProbe.h"
#include "widgets/BootUi.h"

#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
void addTreeNodes(const QJsonArray &nodes, QTreeWidget *tree, QTreeWidgetItem *parent = nullptr)
{
    for (const auto &value : nodes) {
        const auto node = value.toObject();
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
        item->setText(0, node.value(QStringLiteral("name")).toString());
        item->setText(1, node.value(QStringLiteral("children")).toArray().isEmpty()
                           ? QObject::tr("Entry") : QObject::tr("Group"));
        item->setData(0, Qt::UserRole, node.value(QStringLiteral("path")).toString());
        item->setData(0, Qt::UserRole + 1, node.value(QStringLiteral("position")).toInt(1));
        item->setData(0, Qt::UserRole + 2, node.value(QStringLiteral("selection")).toString());
        QStringList details;
        for (const auto &line : node.value(QStringLiteral("lines")).toArray())
            details.append(line.toString());
        item->setData(0, Qt::UserRole + 3, details.join(QLatin1Char('\n')));
        addTreeNodes(node.value(QStringLiteral("children")).toArray(), tree, item);
    }
}

QLineEdit *fileField(QFormLayout *form, QWidget *parent, const QString &name,
                     const QString &title, const QString &initial)
{
    auto *container = new QWidget(parent);
    auto *row = new QHBoxLayout(container);
    row->setContentsMargins(0, 0, 0, 0);
    auto *edit = new QLineEdit(initial, container);
    auto *browse = new QPushButton(QObject::tr("Browse"), container);
    row->addWidget(edit, 1);
    row->addWidget(browse);
    form->addRow(title, container);
    QObject::connect(browse, &QPushButton::clicked, parent, [edit, parent, name] {
        const QString selected = QFileDialog::getOpenFileName(parent, name,
                                                               QFileInfo(edit->text()).absolutePath());
        if (!selected.isEmpty()) edit->setText(selected);
    });
    return edit;
}
}

MenuPage::MenuPage(QWidget *parent) : QWidget(parent)
{
    auto *layout = BootUi::page(this);
    auto *content = layout->parentWidget();
    BootUi::title(layout, content, tr("Boot menu"),
                  tr("Inspect the entries provided by an installed boot loader."));
    selector_ = new QComboBox(content);
    layout->addWidget(selector_);
    menu_ = new QTreeWidget(content);
    menu_->setHeaderLabels({tr("Boot entry"), tr("Type")});
    menu_->setAlternatingRowColors(true);
    menu_->setRootIsDecorated(true);
    menu_->setMinimumHeight(225);
    layout->addWidget(menu_);
    details_ = new QPlainTextEdit(content);
    details_->setReadOnly(true);
    details_->setMaximumHeight(128);
    details_->setPlaceholderText(tr("Select an entry to view its boot configuration."));
    layout->addWidget(details_);
    BootUi::section(layout, content, tr("Menu entries"));
    add_ = BootUi::action(layout, content, tr("Add an EFI program"),
                           tr("Add an installed EFI loader to the Limine menu."),
                           QStringLiteral("list-add"));
    kernel_ = BootUi::action(layout, content, tr("Add a kernel entry"),
                            tr("Choose a kernel and initramfs image for Limine."),
                            QStringLiteral("list-add"));
    uki_ = BootUi::action(layout, content, tr("Add a unified kernel image"),
                         tr("Add an EFI-format Linux unified kernel image."),
                         QStringLiteral("list-add"));
    remove_ = BootUi::action(layout, content, tr("Remove a Limine menu entry"),
                              tr("Remove the selected entry without touching firmware boot entries."),
                              QStringLiteral("list-remove"));
    BootUi::section(layout, content, tr("Menu defaults"));
    default_ = BootUi::action(layout, content, tr("Use selected entry by default"),
                             tr("Set Limine default_entry to the selected menu entry."),
                             QStringLiteral("emblem-default"));
    timeout_ = BootUi::action(layout, content, tr("Menu timeout"),
                             tr("Configure Limine menu delay, in seconds or 'no'."),
                             QStringLiteral("preferences-system-time"));
    layout->addStretch();
    connect(selector_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MenuPage::loadMenu);
    connect(add_, &QPushButton::clicked, this, &MenuPage::addEfi);
    connect(kernel_, &QPushButton::clicked, this, &MenuPage::addKernel);
    connect(uki_, &QPushButton::clicked, this, &MenuPage::addUki);
    connect(remove_, &QPushButton::clicked, this, &MenuPage::removeEntry);
    connect(default_, &QPushButton::clicked, this, &MenuPage::setDefault);
    connect(timeout_, &QPushButton::clicked, this, &MenuPage::setTimeout);
    connect(menu_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) { showSelected(item); });
}

void MenuPage::updateSnapshot(const BootSnapshot &snapshot)
{
    snapshot_ = snapshot;
    manageable_ = snapshot.manageableInstallations();
    const QString previous = selector_->currentData().toString();
    selector_->blockSignals(true);
    selector_->clear();
    for (const auto &loader : manageable_)
        selector_->addItem(loaderName(loader.kind)
            + (loader.kind == LoaderKind::SystemdBoot ? QStringLiteral(" · ") + loader.esp : QString()), loader.id);
    int index = selector_->findData(previous);
    if (index < 0) {
        index = 0;
        for (int i = 0; i < manageable_.size(); ++i)
            if (manageable_.at(i).current) { index = i; break; }
    }
    selector_->setCurrentIndex(manageable_.isEmpty() ? -1 : index);
    selector_->blockSignals(false);
    loadMenu();
}

void MenuPage::loadMenu()
{
    menu_->clear();
    details_->clear();
    menuHash_.clear();
    menuTimeout_.clear();
    menuDefault_.clear();
    const int index = selector_->currentIndex();
    if (index < 0 || index >= manageable_.size()) {
        details_->setPlainText(tr("No installed boot menu was detected."));
        add_->setEnabled(false);
        kernel_->setEnabled(false);
        uki_->setEnabled(false);
        remove_->setEnabled(false);
        default_->setEnabled(false);
        timeout_->setEnabled(false);
        return;
    }
    const auto &loader = manageable_.at(index);
    const bool limine = loader.kind == LoaderKind::Limine;
    for (auto *action : {add_, kernel_, uki_, remove_, default_, timeout_})
        action->setEnabled(false);
    QString output;
    switch (loader.kind) {
    case LoaderKind::Limine: {
        output = BootProbe::runReadOnly(QStringLiteral("limine-entry-tool"),
                                        {QStringLiteral("--tree-json")});
        const QJsonDocument json = QJsonDocument::fromJson(output.toUtf8());
        if (json.isObject()) {
            const QJsonObject root = json.object();
            addTreeNodes(root.value(QStringLiteral("entries")).toArray(), menu_);
            const QJsonObject options = root.value(QStringLiteral("globals")).toObject();
            menuTimeout_ = options.value(QStringLiteral("timeout")).toString();
            menuDefault_ = options.value(QStringLiteral("default_entry")).toString();
            menuHash_ = QString::fromLatin1(QCryptographicHash::hash(output.trimmed().toUtf8(),
                                         QCryptographicHash::Sha256).toHex());
            details_->setPlainText(tr("Default: %1 · Timeout: %2")
                .arg(menuDefault_.isEmpty() ? QStringLiteral("1") : menuDefault_,
                     menuTimeout_.isEmpty() ? QStringLiteral("no") : menuTimeout_));
            for (auto *action : {add_, kernel_, uki_, remove_, default_, timeout_})
                action->setEnabled(true);
        } else {
            const QString textTree = BootProbe::runReadOnly(QStringLiteral("limine-list"));
            if (!textTree.trimmed().isEmpty()) {
                auto *item = new QTreeWidgetItem(menu_);
                item->setText(0, tr("Limine menu (read-only)"));
                item->setText(1, tr("Older limine-tool"));
                item->setData(0, Qt::UserRole + 3, textTree);
            }
            details_->setPlainText(tr("This version of limine-tool does not provide a structured boot menu. Update limine-tool to manage entries."));
        }
        break;
    }
    case LoaderKind::SystemdBoot:
        output = BootProbe::runReadOnly(QStringLiteral("bootctl"),
            {QStringLiteral("--esp-path=") + loader.esp, QStringLiteral("--no-pager"), QStringLiteral("list")});
        if (!output.trimmed().isEmpty()) {
            auto *item = new QTreeWidgetItem(menu_);
            item->setText(0, tr("systemd-boot entries"));
            item->setText(1, tr("Read-only listing"));
            item->setData(0, Qt::UserRole + 3, output.trimmed());
        }
        break;
    case LoaderKind::Grub: {
        QFile file(QStringLiteral("/boot/grub/grub.cfg"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QRegularExpression entry(QStringLiteral("^\\s*(menuentry|submenu)\\s+(['\"])(.*?)\\2"));
            while (!file.atEnd()) {
                const auto match = entry.match(QString::fromUtf8(file.readLine()));
                if (match.hasMatch()) {
                    auto *item = new QTreeWidgetItem(menu_);
                    item->setText(0, match.captured(3));
                    item->setText(1, match.captured(1));
                    item->setData(0, Qt::UserRole + 3, tr("Generated GRUB menu item. Edit its default selection on the Settings page."));
                    output += match.captured(3) + QLatin1Char('\n');
                }
            }
        }
        break;
    }
    }
    if (menu_->topLevelItemCount() == 0 && details_->toPlainText().isEmpty())
        details_->setPlainText(tr("The installed tool could not read a boot menu."));
    if (limine) menu_->expandToDepth(1);
}

void MenuPage::addEfi()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Select an EFI binary"),
                          QStringLiteral("/boot"), tr("EFI executable (*.efi *.EFI)"));
    if (file.isEmpty()) return;
    if (!BootProbe::isOnEsp(file, BootProbe::espMounts())) {
        QMessageBox::warning(this, tr("Invalid EFI file"),
            tr("Choose an EFI program inside a mounted EFI system partition."));
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Add to Limine"), tr("Menu name:"),
                                QLineEdit::Normal, QFileInfo(file).completeBaseName(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    emit operationRequested({QStringLiteral("limine-add-efi"), file, name.trimmed(), menuHash_,
                             tr("Add %1 to Limine menu").arg(name.trimmed())});
}

void MenuPage::removeEntry()
{
    const auto *selected = menu_->currentItem();
    if (!selected || menuHash_.isEmpty()) return;
    const QString path = selected->data(0, Qt::UserRole).toString();
    const int occurrence = selected->data(0, Qt::UserRole + 1).toInt();
    if (path.isEmpty() || occurrence <= 0) return;
    if (QMessageBox::question(this, tr("Remove Limine entry"),
        tr("Remove the selected menu item '%1'? This does not remove EFI firmware entries.")
            .arg(path)) != QMessageBox::Yes) return;
    emit operationRequested({QStringLiteral("limine-remove-entry"), path,
                             QString::number(occurrence), menuHash_,
                             tr("Remove Limine menu item: %1").arg(path)});
}

void MenuPage::showSelected(QTreeWidgetItem *item)
{
    if (!item) return;
    details_->setPlainText(item->data(0, Qt::UserRole + 3).toString());
}

void MenuPage::setDefault()
{
    const auto *selected = menu_->currentItem();
    if (!selected || menuHash_.isEmpty() || selected->childCount() > 0) return;
    const QString value = selected->data(0, Qt::UserRole + 2).toString();
    if (value.isEmpty()) return;
    emit operationRequested({QStringLiteral("limine-global-setting"),
                             QStringLiteral("default_entry"), value, menuDefault_,
                             tr("Limine default startup entry: %1").arg(value)});
}

void MenuPage::setTimeout()
{
    if (menuHash_.isEmpty()) return;
    bool ok = false;
    const QString value = QInputDialog::getText(this, tr("Limine menu timeout"),
        tr("Seconds (0–9999) or 'no' to disable automatic boot:"), QLineEdit::Normal,
        menuTimeout_, &ok).trimmed();
    if (!ok) return;
    bool seconds = false;
    const double count = value.toDouble(&seconds);
    if (value != QLatin1String("no")
        && (!seconds || count < 0.0 || count > 9999.0)) {
        QMessageBox::warning(this, tr("Invalid timeout"),
                             tr("Enter a number between 0 and 9999, or 'no'."));
        return;
    }
    emit operationRequested({QStringLiteral("limine-global-setting"),
                             QStringLiteral("timeout"), value, menuTimeout_,
                             tr("Limine menu timeout: %1").arg(value)});
}

void MenuPage::addKernel()
{
    if (menuHash_.isEmpty()) return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add Limine kernel entry"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *name = new QLineEdit(&dialog);
    name->setPlaceholderText(QStringLiteral("linux-lts"));
    form->addRow(tr("Kernel name:"), name);
    auto *kernel = fileField(form, &dialog, tr("Select vmlinuz"), tr("Kernel image:"),
                             QStringLiteral("/usr/lib/modules"));
    auto *initramfs = fileField(form, &dialog, tr("Select initramfs"), tr("Initramfs image:"),
                                QStringLiteral("/boot"));
    auto *suffix = new QLineEdit(&dialog);
    suffix->setPlaceholderText(QStringLiteral("-fallback"));
    form->addRow(tr("Optional entry suffix:"), suffix);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    const QString kernelName = name->text().trimmed();
    const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,90}$"));
    if (!valid.match(kernelName).hasMatch() || !QFileInfo(kernel->text()).isFile()
        || !QFileInfo(initramfs->text()).isFile()
        || (!suffix->text().isEmpty() && !valid.match(QStringLiteral("a") + suffix->text()).hasMatch())) {
        QMessageBox::warning(this, tr("Invalid kernel entry"),
            tr("Provide a valid kernel name and existing kernel/initramfs files."));
        return;
    }
    const auto json = QJsonDocument(QJsonObject{{QStringLiteral("kernel"), kernel->text()},
        {QStringLiteral("initramfs"), initramfs->text()},
        {QStringLiteral("suffix"), suffix->text()}}).toJson(QJsonDocument::Compact);
    emit operationRequested({QStringLiteral("limine-add-kernel"), kernelName,
        QString::fromUtf8(json), menuHash_, tr("Add Limine kernel: %1%2").arg(kernelName, suffix->text())});
}

void MenuPage::addUki()
{
    if (menuHash_.isEmpty()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Select unified kernel image"),
                        QStringLiteral("/boot"), tr("EFI executable (*.efi *.EFI)"));
    if (path.isEmpty()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New UKI entry"),
                         tr("Kernel name:"), QLineEdit::Normal,
                         QFileInfo(path).completeBaseName(), &ok).trimmed();
    if (!ok) return;
    const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,90}$"));
    if (!valid.match(name).hasMatch() || !QFileInfo(path).isFile()) {
        QMessageBox::warning(this, tr("Invalid UKI"), tr("Select a valid UKI file and entry name."));
        return;
    }
    emit operationRequested({QStringLiteral("limine-add-uki"), name, path,
                             menuHash_, tr("Add Limine UKI: %1").arg(name)});
}
