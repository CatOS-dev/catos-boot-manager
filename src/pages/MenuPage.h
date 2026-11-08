#pragma once

#include "model/BootTransaction.h"
#include "model/BootTypes.h"
#include <QWidget>
#include <QJsonObject>
#include <QMap>

class QComboBox;
class LoaderSelector;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class MenuPage : public QWidget {
    Q_OBJECT
public:
    explicit MenuPage(QWidget *parent = nullptr, bool embedded = false);
    void updateSnapshot(const BootSnapshot &snapshot, bool preserveDrafts = false);
    void updateTransaction(const BootTransaction &transaction);
    void selectMethod(LoaderKind kind);
    LoaderSelector *selector() const { return selector_; }

signals:
    void operationRequested(const BootOperation &operation);
    void methodChanged(LoaderKind kind);

private:
    void loadMenu();
    void editEntry(bool create, bool group = false);
    void removeEntry();
    void moveEntry(int direction);
    void changeGroup(bool indent);
    void setDefault();
    void updateDefaultChoices();
    void showSelected(QTreeWidgetItem *item);
    void stageMenu(const QString &source, const QJsonObject &data, const QString &fingerprint);
    const BootInstallation *installation() const;

    QWidget *entries_ = nullptr;
    QPushButton *rebuild_ = nullptr;
    BootSnapshot snapshot_;
    LoaderSelector *selector_ = nullptr;
    QTreeWidget *menu_ = nullptr;
    QPlainTextEdit *details_ = nullptr;
    QPushButton *add_ = nullptr;
    QPushButton *edit_ = nullptr;
    QPushButton *group_ = nullptr;
    QPushButton *remove_ = nullptr;
    QPushButton *up_ = nullptr;
    QPushButton *down_ = nullptr;
    QPushButton *indent_ = nullptr;
    QPushButton *outdent_ = nullptr;
    QComboBox *defaultChoice_ = nullptr;
    QWidget *defaultField_ = nullptr;
    QMap<QString, QString> pendingDefaults_;
    QMap<QString, QJsonObject> drafts_;
    bool staging_ = false;
    QMap<QString, QString> limineDefaults_;
};
