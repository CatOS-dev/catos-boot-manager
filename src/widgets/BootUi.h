#pragma once

#include <QString>

class QLabel;
class QVBoxLayout;
class QPushButton;
class QWidget;

namespace BootUi {
QVBoxLayout *page(QWidget *parent);
void title(QVBoxLayout *layout, QWidget *parent, const QString &name, const QString &description);
void section(QVBoxLayout *layout, QWidget *parent, const QString &name);
void line(QVBoxLayout *layout, QWidget *parent, const QString &name, const QString &description,
          const QString &icon);
QLabel *muted(const QString &message, QWidget *parent);
QPushButton *action(QVBoxLayout *layout, QWidget *parent, const QString &name,
                    const QString &description, const QString &icon);
}
