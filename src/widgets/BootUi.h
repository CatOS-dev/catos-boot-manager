#pragma once

#include <QString>

class QLabel;
class QAbstractItemView;
class QComboBox;
class QVBoxLayout;
class QPushButton;
class QWidget;

namespace BootUi {
void enableCopy(QAbstractItemView *view);
void enableCopy(QComboBox *combo);
QVBoxLayout *page(QWidget *parent);
void title(QVBoxLayout *layout, QWidget *parent, const QString &name, const QString &description);
void section(QVBoxLayout *layout, QWidget *parent, const QString &name);
void line(QVBoxLayout *layout, QWidget *parent, const QString &name, const QString &description,
          const QString &icon);
QLabel *muted(const QString &message, QWidget *parent);
QPushButton *action(QVBoxLayout *layout, QWidget *parent, const QString &name,
                    const QString &description, const QString &icon);
}
