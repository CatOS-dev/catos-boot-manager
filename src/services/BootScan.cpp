#include "BootScan.h"
#include "BootProbe.h"

#include <QtConcurrentRun>

BootScan::BootScan(QObject *parent) : BootScan(BootProbe::inspect, parent) {}

BootScan::BootScan(std::function<BootSnapshot()> inspect, QObject *parent)
    : QObject(parent), inspect_(std::move(inspect))
{
    connect(&watcher_, &QFutureWatcher<BootSnapshot>::finished, this, [this] {
        running_ = false;
        emit completed(watcher_.result());
    });
}

bool BootScan::start()
{
    if (running_) return false;
    running_ = true;
    watcher_.setFuture(QtConcurrent::run(inspect_));
    return true;
}
