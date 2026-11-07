#include "BootUi.h"

#include <QFrame>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QKeySequence>
#include <algorithm>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace {
QWidget *rowWidget(QWidget *parent, const QString &name, const QString &description,
                   const QString &iconName)
{
    auto *container = new QWidget(parent);
    auto *line = new QHBoxLayout(container);
    line->setContentsMargins(10, 8, 10, 8);
    line->setSpacing(14);
    auto *icon = new QLabel(container);
    icon->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    icon->setFixedSize(30, 30);
    icon->setAlignment(Qt::AlignCenter);
    const QIcon themed = QIcon::fromTheme(iconName);
    if (!themed.isNull()) icon->setPixmap(themed.pixmap(24, 24));
    line->addWidget(icon);
    auto *labels = new QVBoxLayout;
    labels->setContentsMargins(0, 0, 0, 0);
    labels->setSpacing(3);
    auto *header = new QLabel(name, container);
    header->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    auto font = header->font();
    font.setBold(true);
    header->setFont(font);
    labels->addWidget(header);
    if (!description.isEmpty()) {
        auto *details = new QLabel(description, container);
        details->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        details->setWordWrap(true);
        details->setForegroundRole(QPalette::PlaceholderText);
        labels->addWidget(details);
    }
    line->addLayout(labels, 1);
    return container;
}
}

void BootUi::enableCopy(QAbstractItemView *view)
{
    auto *copy = new QAction(QObject::tr("Copy"), view);
    copy->setShortcut(QKeySequence::Copy);
    copy->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    view->addAction(copy);
    view->setContextMenuPolicy(Qt::ActionsContextMenu);
    QObject::connect(copy, &QAction::triggered, view, [view] {
        auto indexes = view->selectionModel()->selectedIndexes();
        if (indexes.isEmpty()) return;
        std::sort(indexes.begin(), indexes.end(), [](const QModelIndex &a, const QModelIndex &b) {
            return a.row() == b.row() ? a.column() < b.column() : a.row() < b.row();
        });
        QString text;
        QModelIndex previous;
        for (const auto &index : indexes) {
            if (previous.isValid()) text += index.row() == previous.row() && index.parent() == previous.parent()
                ? QLatin1Char('\t') : QLatin1Char('\n');
            text += index.data().toString();
            previous = index;
        }
        QApplication::clipboard()->setText(text);
    });
}

void BootUi::enableCopy(QComboBox *combo)
{
    auto *copy = new QAction(QObject::tr("Copy"), combo);
    copy->setShortcut(QKeySequence::Copy);
    copy->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    combo->addAction(copy);
    combo->setContextMenuPolicy(Qt::ActionsContextMenu);
    QObject::connect(copy, &QAction::triggered, combo, [combo] {
        QApplication::clipboard()->setText(combo->currentText());
    });
}

QVBoxLayout *BootUi::page(QWidget *parent)
{
    auto *outer = new QVBoxLayout(parent);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto *content = new QWidget(scroll);
    scroll->setWidget(content);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 18, 34, 28);
    layout->setSpacing(6);
    return layout;
}

void BootUi::title(QVBoxLayout *layout, QWidget *parent, const QString &name,
                   const QString &description)
{
    auto *heading = new QLabel(name, parent);
    heading->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    auto font = heading->font();
    font.setPointSize(font.pointSize() + 9);
    font.setBold(true);
    heading->setFont(font);
    layout->addWidget(heading);
    layout->addWidget(muted(description, parent));
}

void BootUi::section(QVBoxLayout *layout, QWidget *parent, const QString &name)
{
    layout->addSpacing(19);
    auto *heading = new QLabel(name, parent);
    heading->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    auto font = heading->font();
    font.setBold(true);
    font.setPointSize(font.pointSize() + 2);
    heading->setFont(font);
    layout->addWidget(heading);
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    layout->addWidget(line);
}

void BootUi::line(QVBoxLayout *layout, QWidget *parent, const QString &name,
                  const QString &description, const QString &icon)
{
    layout->addWidget(rowWidget(parent, name, description, icon));
}

QLabel *BootUi::muted(const QString &message, QWidget *parent)
{
    auto *label = new QLabel(message, parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

QPushButton *BootUi::action(QVBoxLayout *layout, QWidget *parent, const QString &name,
                            const QString &description, const QString &icon)
{
    auto *button = new QPushButton(parent);
    button->setFlat(true);
    button->setMinimumHeight(60);
    button->setCursor(Qt::PointingHandCursor);
    button->setAccessibleName(name);
    button->setAccessibleDescription(description);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { border: 0; border-radius: 9px; background: transparent; text-align: left; }"
        "QPushButton:hover { background: palette(alternate-base); }"
        "QPushButton:focus { border: 1px solid palette(highlight); }"));
    auto *line = new QHBoxLayout(button);
    line->setContentsMargins(10, 2, 10, 2);
    line->setSpacing(8);
    auto *contents = rowWidget(button, name, description, icon);
    contents->setAttribute(Qt::WA_TransparentForMouseEvents);
    line->addWidget(contents, 1);
    auto *chevron = muted(QStringLiteral("›"), button);
    line->addWidget(chevron);
    layout->addWidget(button);
    return button;
}

void BootUi::setAvailable(QWidget *widget, bool enabled, const QString &reason, const QString &help)
{
    widget->setEnabled(enabled);
    widget->setToolTip(enabled ? help : reason);
}
