#include "ApplyDialog.h"
#include "widgets/BootUi.h"
#include "services/BootSession.h"

#include <QCloseEvent>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
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

ApplyResult ApplyDialog::execute(QWidget *parent, BootSession &session, const BootTransaction &transaction, bool review)
{
    if (!transaction.count()) return ApplyResult::Cancelled;
    if (review) {
    QDialog preview(parent);
    preview.setWindowTitle(QObject::tr("Review boot changes"));
    preview.resize(640, 460);
    auto *layout = new QVBoxLayout(&preview);
    layout->addWidget(BootUi::muted(QObject::tr("Apply the following pending changes."), &preview));
    auto *scroll = new QScrollArea(&preview);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *items = new QVBoxLayout(content);
    for (const auto &op : transaction.operations()) {
        BootUi::line(items, content, op.description, {}, QStringLiteral("document-edit"));

    }
    items->addStretch();
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    auto *controls = new QHBoxLayout;
    controls->addStretch();
    auto *cancel = new QPushButton(QObject::tr("Cancel"), &preview);
    auto *confirm = new QPushButton(QObject::tr("Apply"), &preview);
    confirm->setDefault(true);
    controls->addWidget(cancel);
    controls->addWidget(confirm);
    layout->addLayout(controls);
    QObject::connect(cancel, &QPushButton::clicked, &preview, &QDialog::reject);
    QObject::connect(confirm, &QPushButton::clicked, &preview, &QDialog::accept);
    if (preview.exec() != QDialog::Accepted) return ApplyResult::Cancelled;
    }

    ExecutionDialog running(parent);
    running.setWindowTitle(QObject::tr("Applying boot changes"));
    running.resize(680, 430);
    auto *main = new QVBoxLayout(&running);
    auto *status = new QLabel(QObject::tr("Executing…"), &running);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    main->addWidget(status);
    auto *log = new QPlainTextEdit(&running);
    log->setReadOnly(true);
    main->addWidget(log, 1);
    auto *close = new QPushButton(QObject::tr("Close"), &running);
    close->setEnabled(false);
    main->addWidget(close, 0, Qt::AlignRight);
    QObject::connect(&session, &BootSession::output, &running, [&](const QString &text) {
        log->insertPlainText(text);
        log->ensureCursorVisible();
    });
    QObject::connect(&session, &BootSession::operationFinished, &running, [&](bool success) {
        running.finished(success);
        status->setText(success ? QObject::tr("Completed.")
                                : QObject::tr("Operation failed."));
        close->setEnabled(true);
    });
    QObject::connect(close, &QPushButton::clicked, &running, &QDialog::accept);
    if (!session.apply(transaction)) {
        if (session.isAuthorized())
            QMessageBox::warning(parent, QObject::tr("Cannot apply changes"),
                                 QObject::tr("Unable to start the operation."));
        return ApplyResult::Failed;
    }
    running.exec();
    return running.succeeded() ? ApplyResult::Applied : ApplyResult::Failed;
}
