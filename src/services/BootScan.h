#pragma once

#include "model/BootTypes.h"

#include <QFutureWatcher>
#include <QObject>
#include <functional>

class BootScan : public QObject {
    Q_OBJECT
public:
    explicit BootScan(QObject *parent = nullptr);
    BootScan(std::function<BootSnapshot()> inspect, QObject *parent);
    bool start();
    bool isRunning() const { return running_; }

signals:
    void completed(const BootSnapshot &snapshot);

private:
    std::function<BootSnapshot()> inspect_;
    QFutureWatcher<BootSnapshot> watcher_;
    bool running_ = false;
};
