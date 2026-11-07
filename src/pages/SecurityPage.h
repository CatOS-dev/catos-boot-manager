#pragma once

#include "model/BootTypes.h"
#include <QJsonObject>
#include <QWidget>

class LoaderSelector;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QToolButton;
class QTreeWidget;

class SecurityPage : public QWidget {
    Q_OBJECT
public:
    explicit SecurityPage(QWidget *parent = nullptr);
    void updateSnapshot(const BootSnapshot &snapshot);
    void selectMethod(LoaderKind kind);
    LoaderSelector *selector() const { return selector_; }

signals:
    void methodChanged(LoaderKind kind);
    // A MOK request to run immediately (see BootSecurity::executeMokRequest).
    void mokRequested(const QJsonObject &request, const QString &description);

private:
    void showInstance();
    void requestEnrollment();
    LoaderSelector *selector_ = nullptr;
    QLabel *state_ = nullptr;
    QLabel *guidance_ = nullptr;
    QTreeWidget *enrolled_ = nullptr;
    QTreeWidget *pending_ = nullptr;
    QPushButton *enroll_ = nullptr;
    QPushButton *revoke_ = nullptr;
    QToolButton *rawToggle_ = nullptr;
    QPlainTextEdit *certificates_ = nullptr;
    QPlainTextEdit *signature_ = nullptr;
};
