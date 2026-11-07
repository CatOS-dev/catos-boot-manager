#include "BootSession.h"
#include "model/BootSnapshotCodec.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

BootSession::BootSession(QObject *parent) : QObject(parent)
{
    connect(&process_, &QProcess::readyReadStandardOutput, this, &BootSession::receive);
    connect(&process_, &QProcess::readyReadStandardError, this, [this] {
        const QString text = QString::fromUtf8(process_.readAllStandardError());
        lastError_ = text.trimmed().right(4096);
        emit output(text);
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) fail(tr("Unable to start administrator authorization."));
    });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            receive();
            if (state_ == State::Stopped) return;
            if (state_ == State::Starting && status == QProcess::NormalExit && code == 126) {
                state_ = State::Stopped;
                emit authorizationCancelled();
                return;
            }
            fail(state_ == State::Starting ? tr("Administrator authorization failed.")
                                          : tr("Administrator connection lost. Reopen the application."));
        });
}

BootSession::~BootSession() { stop(); }

void BootSession::start()
{
    if (state_ != State::Stopped || process_.state() != QProcess::NotRunning) return;
    const QDir directory(QCoreApplication::applicationDirPath());
    const QString executor = directory.dirName() == QLatin1String("bin")
        ? QDir::cleanPath(directory.filePath(QStringLiteral("../lib/catos/catos-boot-apply")))
        : directory.filePath(QStringLiteral("catos-boot-apply"));
    if (!QFileInfo(executor).isExecutable()) {
        emit failed(tr("Unable to start administrator authorization."));
        return;
    }
    input_.clear();
    state_ = State::Starting;
    process_.start(QStringLiteral("pkexec"), {executor, QStringLiteral("--session")});
}

void BootSession::stop()
{
    state_ = State::Stopped;
    process_.closeWriteChannel();
    if (process_.state() != QProcess::NotRunning && !process_.waitForFinished(1500)) {
        process_.kill();
        process_.waitForFinished(1500);
    }
}

void BootSession::fail(const QString &message)
{
    if (state_ == State::Stopped) return;
    const bool applying = state_ == State::Applying;
    state_ = State::Stopped;
    process_.closeWriteChannel();
    if (applying) emit operationFinished(false);
    emit failed(message);
}

bool BootSession::send(const QJsonObject &request, State state)
{
    if (state_ != State::Ready) return false;
    const QByteArray data = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    state_ = state;
    if (process_.write(data) == data.size()) return true;
    fail(tr("Administrator connection lost. Reopen the application."));
    return false;
}

bool BootSession::scan()
{
    return send({{QStringLiteral("action"), QStringLiteral("scan")}}, State::Scanning);
}

bool BootSession::apply(const BootTransaction &transaction)
{
    lastApplyCode_ = -1;
    lastOutcomes_ = {};
    lastReportDirectory_.clear();
    lastError_.clear();
    if (!transaction.count()) return false;
    return send({{QStringLiteral("action"), QStringLiteral("apply")},
                 {QStringLiteral("transaction"), QString::fromLatin1(transaction.encoded())}}, State::Applying);
}

bool BootSession::secureBoot(const QJsonObject &request)
{
    lastApplyCode_ = -1;
    lastOutcomes_ = {};
    lastReportDirectory_.clear();
    return send({{QStringLiteral("action"), QStringLiteral("secure-boot")}, {QStringLiteral("request"), request}}, State::Applying);
}

bool BootSession::preview(const BootTransaction &transaction)
{
    if (!transaction.count()) return false;
    return send({{QStringLiteral("action"), QStringLiteral("preview")},
                 {QStringLiteral("transaction"), QString::fromLatin1(transaction.encoded())}}, State::Previewing);
}

void BootSession::receive()
{
    input_ += process_.readAllStandardOutput();
    if (input_.size() > 64 * 1024 * 1024) {
        fail(tr("Unable to read boot information."));
        return;
    }
    while (input_.contains('\n')) {
        const auto end = input_.indexOf('\n');
        const auto document = QJsonDocument::fromJson(input_.left(end));
        input_.remove(0, end + 1);
        if (state_ == State::Stopped) return;
        const auto response = document.object();
        const QString type = response.value(QStringLiteral("type")).toString();
        if (document.isObject() && type == QLatin1String("ready") && state_ == State::Starting
            && response.value(QStringLiteral("version")).toInt() == 1
            && response.value(QStringLiteral("uid")).toInt(-1) == 0) {
            state_ = State::Ready;
            emit authorized();
        } else if (document.isObject() && type == QLatin1String("snapshot") && state_ == State::Scanning) {
            BootSnapshot snapshot;
            if (!BootSnapshotCodec::decode(response.value(QStringLiteral("data")).toString().toLatin1(), &snapshot)) {
                fail(tr("Unable to read boot information."));
                return;
            }
            state_ = State::Ready;
            emit snapshotReady(snapshot);
        } else if (document.isObject() && type == QLatin1String("progress") && state_ == State::Scanning
                   && response.value(QStringLiteral("stage")).isString()) {
            emit scanProgress(response.value(QStringLiteral("stage")).toString().left(200));
        } else if (document.isObject() && type == QLatin1String("preview") && state_ == State::Previewing
                   && response.value(QStringLiteral("files")).isArray() && response.value(QStringLiteral("error")).isString()) {
            state_ = State::Ready;
            emit previewReady(response.value(QStringLiteral("files")).toArray(), response.value(QStringLiteral("error")).toString());
        } else if (document.isObject() && type == QLatin1String("result") && state_ == State::Applying
                   && response.value(QStringLiteral("success")).isBool()
                   && response.value(QStringLiteral("outcomes")).isObject()
                   && response.value(QStringLiteral("reportDirectory")).isString()
                   && response.value(QStringLiteral("code")).isDouble()
                   && response.value(QStringLiteral("code")).toInt(-1) >= 0
                   && response.value(QStringLiteral("code")).toInt(-1) <= 4
                   && response.value(QStringLiteral("success")).toBool()
                       == (response.value(QStringLiteral("code")).toInt() == 0)) {
            emit output(QString::fromUtf8(process_.readAllStandardError()));
            lastOutcomes_ = response.value("outcomes").toObject();
            lastReportDirectory_ = response.value("reportDirectory").toString();
            lastApplyCode_ = response.value(QStringLiteral("code")).toInt(-1);
            state_ = State::Ready;
            emit operationFinished(response.value(QStringLiteral("success")).toBool());
        } else {
            fail(tr("Unable to read boot information."));
            return;
        }
    }
}
