#pragma once

#include "model/BootTransaction.h"
#include "model/BootTypes.h"

#include <QObject>
#include <QProcess>

class BootSession : public QObject {
    Q_OBJECT
public:
    explicit BootSession(QObject *parent = nullptr);
    ~BootSession() override;
    void start();
    void stop();
    bool scan();
    bool apply(const BootTransaction &transaction);
    bool isAuthorized() const { return state_ != State::Stopped && state_ != State::Starting; }
    bool isBusy() const { return state_ == State::Scanning || state_ == State::Applying; }

signals:
    void authorized();
    void authorizationCancelled();
    void snapshotReady(const BootSnapshot &snapshot);
    void operationFinished(bool success);
    void output(const QString &text);
    void failed(const QString &message);

private:
    enum class State { Stopped, Starting, Ready, Scanning, Applying };
    void receive();
    void fail(const QString &message);
    bool send(const QJsonObject &request, State state);

    QProcess process_;
    QByteArray input_;
    State state_ = State::Stopped;
};
