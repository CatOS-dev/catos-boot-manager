#pragma once

#include "model/BootTypes.h"

#include <QHash>
#include <QWidget>

class QTabBar;
class QComboBox;

class LoaderSelector : public QWidget {
    Q_OBJECT
public:
    explicit LoaderSelector(QWidget *parent = nullptr, bool menuScope = false);
    void updateSnapshot(const BootSnapshot &snapshot);
    void selectMethod(LoaderKind kind);
    void selectInstallation(LoaderKind kind, const QString &id);
    LoaderKind method() const;
    const QList<BootInstallation> &installations() const { return filtered_; }
    int currentIndex() const;
    QString currentId() const;

signals:
    void selectionChanged();
    void methodChanged(LoaderKind kind);
    void installationChanged(LoaderKind kind, const QString &id);

private:
    void rebuild();
    QList<BootInstallation> installations_;
    QList<BootInstallation> filtered_;
    QHash<int, QString> selected_;
    QTabBar *tabs_ = nullptr;
    QComboBox *instances_ = nullptr;
    bool initialized_ = false;
    bool menuScope_ = false;
};
