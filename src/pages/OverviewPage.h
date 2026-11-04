#pragma once

#include "model/BootTypes.h"

#include <QWidget>

class QVBoxLayout;

class OverviewPage : public QWidget {
    Q_OBJECT
public:
    explicit OverviewPage(QWidget *parent = nullptr);
    void updateSnapshot(const BootSnapshot &snapshot);

private:
    QVBoxLayout *layout_ = nullptr;
};
