#include "ApplyDialog.h"
#include "widgets/BootUi.h"
#include "services/BootSession.h"

#include <QCloseEvent>
#include <QFileDialog>
#include <QSaveFile>
#include <QClipboard>
#include <QApplication>
#include <QJsonDocument>
#include <QTableWidget>
#include <QHeaderView>
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

ApplyResult ApplyDialog::execute(QWidget *parent, BootSession &session, const BootTransaction &transaction, bool immediate)
{
    if (!transaction.count()) return ApplyResult::Cancelled;
    {
    QDialog preview(parent);
    preview.setWindowTitle(immediate ? QObject::tr("Confirm immediate execution") : QObject::tr("Review boot changes"));
    preview.resize(640, 460);
    auto *layout = new QVBoxLayout(&preview);
    layout->addWidget(BootUi::muted(immediate ? QObject::tr("This action runs immediately after confirmation. Existing pending changes will not be applied.") : QObject::tr("Apply the following pending changes."), &preview));
    auto *scroll = new QScrollArea(&preview);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *items = new QVBoxLayout(content);
    for (const auto &op : transaction.operations()) {
        QString details;
        if (op.kind == QLatin1String("repair-install")) {
            const auto data = QJsonDocument::fromJson(op.value.toUtf8()).object();
            details = QObject::tr("Target system: %1\nDestination: %2\nRegister UEFI entry: %3\nReplace removable EFI loader: %4")
                .arg(data.value("root").toString(),
                     data.value("mode").toString() == "bios" ? data.value("disk").toString()
                         : data.value("rootDevice").toString().isEmpty() ? (data.value("esp").toString().isEmpty() ? QObject::tr("Configured EFI system partition") : data.value("esp").toString()) : data.value("espDevice").toString(),
                     data.value("register").toBool() ? QObject::tr("Yes") : QObject::tr("No"),
                     data.value("fallback").toBool() ? QObject::tr("Yes") : QObject::tr("No"));
            details += '\n' + QObject::tr("Boot loader files at this destination may be replaced.");
        }
        BootUi::line(items, content, op.description, details, QStringLiteral("document-edit"));

    }
    items->addStretch();
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    auto *controls = new QHBoxLayout;
    controls->addStretch();
    auto *cancel = new QPushButton(QObject::tr("Cancel"), &preview);
    QString confirmText = QObject::tr("Apply");
    if (immediate) {
        const auto operation = transaction.operations().first();
        const auto action = QJsonDocument::fromJson(operation.value.toUtf8()).object().value("action").toString();
        confirmText = operation.kind == "install-packages" ? QObject::tr("Install packages now")
            : operation.kind == "firmware-add" ? QObject::tr("Add entry now")
            : action == "repair" ? QObject::tr("Repair now")
            : action == "update" ? QObject::tr("Update now") : QObject::tr("Deploy now");
    }
    auto *confirm = new QPushButton(confirmText, &preview);
    confirm->setObjectName("confirmExecution");
    cancel->setDefault(true);
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
    status->setWordWrap(true);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    main->addWidget(status);
    auto *results = new QTableWidget(&running);
    results->setObjectName("executionResults");
    results->setColumnCount(2);
    results->setHorizontalHeaderLabels({QObject::tr("Operation / step"), QObject::tr("Result")});
    results->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    results->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results->hide();
    main->addWidget(results);
    auto *log = new QPlainTextEdit(&running);
    log->setReadOnly(true);
    main->addWidget(log, 1);
    auto *tools = new QHBoxLayout;
    auto *copy = new QPushButton(QObject::tr("Copy log"), &running);
    auto *save = new QPushButton(QObject::tr("Save execution log"), &running);
    tools->addWidget(copy);
    tools->addWidget(save);
    tools->addStretch();
    main->addLayout(tools);
    QObject::connect(copy, &QPushButton::clicked, &running, [log] {
        QApplication::clipboard()->setText(log->toPlainText());
    });
    QObject::connect(save, &QPushButton::clicked, &running, [&running, log] {
        const auto path = QFileDialog::getSaveFileName(&running, QObject::tr("Save execution log"), "boot-execution.txt");
        if (path.isEmpty()) return;
        QSaveFile file(path);
        const auto data = log->toPlainText().toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
            QMessageBox::warning(&running, QObject::tr("Save execution log"), QObject::tr("Unable to save the execution log."));
    });
    auto *close = new QPushButton(QObject::tr("Close"), &running);
    close->setEnabled(false);
    main->addWidget(close, 0, Qt::AlignRight);
    QObject::connect(&session, &BootSession::output, &running, [&](const QString &text) {
        log->insertPlainText(text);
        log->ensureCursorVisible();
    });
    QObject::connect(&session, &BootSession::operationFinished, &running, [&](bool success) {
        running.finished(success);
        const auto outcomes = session.lastOutcomes();
        const auto operations = transaction.operations();
        for (auto it = outcomes.begin(); it != outcomes.end(); ++it) {
            QString name = it.key();
            for (const auto &operation : operations) if (operation.key() == it.key()) name = operation.description;
            const QString state = it.value().toString();
            const QString text = state == "succeeded" ? QObject::tr("Succeeded")
                : state == "failed" ? QObject::tr("Failed; changes may be partial") : QObject::tr("Not executed");
            const int row = results->rowCount();
            results->insertRow(row);
            results->setItem(row, 0, new QTableWidgetItem(name));
            results->setItem(row, 1, new QTableWidgetItem(text));
            log->appendPlainText(name + ": " + text);
        }
        results->setVisible(!outcomes.isEmpty());
        if (!session.lastReportDirectory().isEmpty()) log->appendPlainText(QObject::tr("Execution report: %1").arg(session.lastReportDirectory()));
        status->setText(success ? QObject::tr("Completed. Changes have been applied.")
            : session.lastApplyCode() == 2 || session.lastApplyCode() == 3
                ? QObject::tr("Rejected before execution. No changes were applied.")
                : QObject::tr("Execution incomplete. Some changes may have been applied. Save the log before retrying."));
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
