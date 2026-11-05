#pragma once

#include "model/BootTypes.h"
#include <QWidget>

class LoaderSelector;
class QPlainTextEdit;
class QLabel;

class SecurityPage : public QWidget {
    Q_OBJECT
public:
    explicit SecurityPage(QWidget *parent = nullptr);
    void updateSnapshot(const BootSnapshot &snapshot);
    void selectMethod(LoaderKind kind);
    LoaderSelector *selector() const { return selector_; }

signals:
    void methodChanged(LoaderKind kind);

private:
    void showInstance();
    LoaderSelector *selector_ = nullptr;
    QLabel *state_ = nullptr;
    QPlainTextEdit *certificates_ = nullptr;
    QPlainTextEdit *signature_ = nullptr;
};
