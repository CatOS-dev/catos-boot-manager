#include "OverviewPage.h"
#include "widgets/BootUi.h"

#include <QLayoutItem>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QSet>
#include <QToolButton>
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
                 snapshot.currentLoader.isEmpty() ? tr("unknown") : snapshot.currentLoader,
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
    for (const auto kind : loaderKinds) {
        bool found = false;
        bool binary = false;
        bool config = false;
        bool current = false;
        bool defaultEntry = false;
        bool next = false;
        QSet<QString> programs;
        QSet<QString> issues;
        QStringList records;
        for (const auto &loader : snapshot.installations) {
            if (loader.kind != kind) continue;
            found = true;
            binary |= loader.binaryPresent;
            config |= loader.configPresent;
            current |= loader.current;
            defaultEntry |= loader.defaultEntry;
            next |= loader.nextEntry;
            if (loader.binaryPresent && !loader.binary.isEmpty()) programs.insert(loader.binary);
            QStringList details;
            if (!loader.esp.isEmpty()) details << tr("ESP: %1").arg(loader.esp);
            if (!loader.espPartuuid.isEmpty()) details << tr("Partition UUID: %1").arg(loader.espPartuuid);
            if (!loader.binary.isEmpty()) details << tr("Program: %1").arg(loader.binary);
            if (!loader.config.isEmpty()) details << tr("Configuration: %1").arg(loader.config);
            if (!loader.binary.isEmpty() && !loader.binaryPresent) {
                details << tr("EFI program is missing");
                issues.insert(tr("EFI program is missing"));
            }
            if (!loader.firmwareEntries.isEmpty())
                details << tr("Firmware entries: %1").arg(loader.firmwareEntries.join(QStringLiteral(", ")));
            if (loader.espBytesAvailable >= 0)
                details << tr("ESP available space: %1 MiB").arg(loader.espBytesAvailable / (1024 * 1024));
            for (const auto &issue : loader.issues) {
                issues.insert(issue);
                details << tr("Needs attention: %1").arg(issue);
            }
            records << details.join(QLatin1Char('\n'));
        }
        if (!found) continue;
        QStringList summary;
        summary << (binary && config ? tr("Program and configuration detected")
            : binary ? tr("Program detected") : config ? tr("Configuration only") : tr("Needs attention"));
        if (current) summary << tr("Current boot");
        if (defaultEntry) summary << tr("Default boot");
        if (next) summary << tr("Next boot only");
        if (programs.size() > 1) summary << tr("%1 installation locations").arg(programs.size());
        if (!issues.isEmpty()) summary << tr("Needs attention (%1)").arg(issues.size());

        auto *card = new QWidget(content);
        card->setObjectName(QStringLiteral("overviewLoader%1").arg(static_cast<int>(kind)));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(10, 8, 10, 8);
        auto *row = new QHBoxLayout;
        auto *name = new QLabel(loaderName(kind), card);
        name->setObjectName(QStringLiteral("overviewLoaderName"));
        name->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        auto font = name->font();
        font.setBold(true);
        name->setFont(font);
        row->addWidget(name);
        auto *status = BootUi::muted(summary.join(QStringLiteral(" · ")), card);
        status->setTextFormat(Qt::PlainText);
        status->setObjectName(QStringLiteral("overviewLoaderStatus"));
        row->addWidget(status, 1);
        auto *toggle = new QToolButton(card);
        toggle->setObjectName(QStringLiteral("overviewLoaderDetailsToggle"));
        toggle->setText(tr("Details"));
        toggle->setCheckable(true);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setArrowType(Qt::RightArrow);
        row->addWidget(toggle);
        cardLayout->addLayout(row);
        auto *details = new QPlainTextEdit(card);
        details->setObjectName(QStringLiteral("overviewLoaderDetails"));
        details->setReadOnly(true);
        details->setPlainText(records.join(QStringLiteral("\n\n")));
        details->setMaximumHeight(180);
        cardLayout->addWidget(details);
        details->hide();
        connect(toggle, &QToolButton::toggled, details, &QWidget::setVisible);
        connect(toggle, &QToolButton::toggled, toggle, [toggle](bool expanded) {
            toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        });
        layout_->addWidget(card);
    }
    if (snapshot.installations.isEmpty())
        layout_->addWidget(BootUi::muted(tr("No manageable loader installation was detected."), content));
    QStringList detection = snapshot.messages;
    if (!detection.isEmpty()) {
        auto *toggle = new QToolButton(content);
        toggle->setText(tr("Detection details"));
        toggle->setCheckable(true);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setArrowType(Qt::RightArrow);
        layout_->addWidget(toggle, 0, Qt::AlignLeft);
        auto *details = new QPlainTextEdit(content);
        details->setReadOnly(true);
        details->setPlainText(detection.join(QLatin1Char('\n')));
        details->setMaximumHeight(160);
        layout_->addWidget(details);
        details->hide();
        connect(toggle, &QToolButton::toggled, details, &QWidget::setVisible);
        connect(toggle, &QToolButton::toggled, toggle, [toggle](bool expanded) {
            toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        });
    }
    layout_->addStretch();
}
