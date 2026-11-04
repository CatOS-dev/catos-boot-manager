#include "ApplyDialog.h"
#include "widgets/BootUi.h"
#include "services/BootRepair.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {
class ExecutionDialog : public QDialog {
public:
    using QDialog::QDialog;
    void finished(bool success) { done_ = true; success_ = success; }
    bool succeeded() const { return success_; }
    void reject() override { if (done_) QDialog::reject(); }
protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (done_) QDialog::closeEvent(event);
        else event->ignore();
    }
private:
    bool done_ = false;
    bool success_ = false;
};
}

ApplyResult ApplyDialog::execute(QWidget *parent, const BootTransaction &transaction)
{
    if (!transaction.count()) return ApplyResult::Cancelled;
    QDialog preview(parent);
    preview.setWindowTitle(QObject::tr("Review boot changes"));
    preview.resize(640, 460);
    auto *layout = new QVBoxLayout(&preview);
    layout->addWidget(BootUi::muted(QObject::tr("These pending changes will be applied with one administrator authorization."), &preview));
    auto *scroll = new QScrollArea(&preview);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *items = new QVBoxLayout(content);
    for (const auto &op : transaction.operations()) {
        BootUi::line(items, content, op.description, {}, QStringLiteral("document-edit"));
        if (BootRepair::supports(op.kind)) {
            auto *detail = BootUi::muted(BootRepair::preview(op).join(QLatin1Char('\n')), content);
            detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
            items->addWidget(detail);
        }
    }
    items->addStretch();
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    auto *warning = BootUi::muted(QObject::tr("Firmware/NVRAM changes are not fully atomic."
        " If an operation fails, earlier changes may remain; review the execution log."), &preview);
    layout->addWidget(warning);
    auto *controls = new QHBoxLayout;
    controls->addStretch();
    auto *cancel = new QPushButton(QObject::tr("Cancel"), &preview);
    auto *confirm = new QPushButton(QObject::tr("Authorize and apply"), &preview);
    confirm->setDefault(true);
    controls->addWidget(cancel);
    controls->addWidget(confirm);
    layout->addLayout(controls);
    QObject::connect(cancel, &QPushButton::clicked, &preview, &QDialog::reject);
    QObject::connect(confirm, &QPushButton::clicked, &preview, &QDialog::accept);
    if (preview.exec() != QDialog::Accepted) return ApplyResult::Cancelled;

    const QString systemExecutor = QStringLiteral("/usr/lib/catos/catos-boot-apply");
    const QString sibling = QCoreApplication::applicationDirPath() + QStringLiteral("/catos-boot-apply");
    const QString executor = QFileInfo::exists(systemExecutor) ? systemExecutor : sibling;
    if (!QFileInfo::exists(executor)) {
        QMessageBox::warning(parent, QObject::tr("Cannot apply changes"),
                             QObject::tr("The privileged boot transaction executor is not installed."));
        return ApplyResult::Cancelled;
    }

    ExecutionDialog running(parent);
    running.setWindowTitle(QObject::tr("Applying boot changes"));
    running.resize(680, 430);
    auto *main = new QVBoxLayout(&running);
    auto *status = new QLabel(QObject::tr("Waiting for administrator authorization…"), &running);
    main->addWidget(status);
    auto *log = new QPlainTextEdit(&running);
    log->setReadOnly(true);
    main->addWidget(log, 1);
    auto *close = new QPushButton(QObject::tr("Close"), &running);
    close->setEnabled(false);
    main->addWidget(close, 0, Qt::AlignRight);
    QProcess process(&running);
    process.setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(&process, &QProcess::readyRead, &running, [&] {
        log->insertPlainText(QString::fromUtf8(process.readAll()));
        log->ensureCursorVisible();
    });
    QObject::connect(&process, &QProcess::errorOccurred, &running, [&](QProcess::ProcessError) {
        if (process.state() == QProcess::NotRunning && !close->isEnabled()) {
            running.finished(false);
            status->setText(QObject::tr("Authorization or execution could not start."));
            close->setEnabled(true);
        }
    });
    QObject::connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &running, [&](int code, QProcess::ExitStatus exitStatus) {
        log->insertPlainText(QString::fromUtf8(process.readAll()));
        const bool success = code == 0 && exitStatus == QProcess::NormalExit;
        running.finished(success);
        status->setText(success ? QObject::tr("Changes applied. Reload the boot state before restarting.")
                                : QObject::tr("The transaction stopped. Review the log for changes already applied."));
        close->setEnabled(true);
    });
    QObject::connect(close, &QPushButton::clicked, &running, &QDialog::accept);
    QObject::connect(&process, &QProcess::started, &running, [&] {
        process.write(transaction.encoded());
        process.closeWriteChannel();
    });
    process.start(QStringLiteral("pkexec"), {executor, QStringLiteral("--apply-stdin")});
    running.exec();
    return running.succeeded() ? ApplyResult::Applied : ApplyResult::Failed;
}
