#include "SecurityPage.h"
#include "services/BootSecurity.h"
#include "widgets/BootUi.h"
#include "widgets/LoaderSelector.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
QString flag(SecureBootState state)
{
    switch (state) {
    case SecureBootState::Enabled: return QObject::tr("Enabled");
    case SecureBootState::Disabled: return QObject::tr("Disabled");
    case SecureBootState::Unsupported: return QObject::tr("Not available in BIOS mode");
    case SecureBootState::Unknown: return QObject::tr("Unknown / could not read");
    }
    return {};
}

QTreeWidget *certificateList(QWidget *parent, const QString &name)
{
    auto *list = new QTreeWidget(parent);
    list->setObjectName(name);
    list->setHeaderLabels({QObject::tr("Name"), QObject::tr("Issuer"), QObject::tr("Expires"), QObject::tr("SHA1 fingerprint")});
    list->setRootIsDecorated(false);
    list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list->setMinimumHeight(90);
    list->setMaximumHeight(160);
    BootUi::enableCopy(list);
    return list;
}

void fill(QTreeWidget *list, const QString &output)
{
    list->clear();
    for (const auto &certificate : BootSecurity::certificates(output)) {
        auto *item = new QTreeWidgetItem(list, {certificate.commonName, certificate.issuer, certificate.notAfter, certificate.fingerprint});
        item->setToolTip(0, certificate.subject);
    }
    for (int column = 1; column < 4; ++column) list->resizeColumnToContents(column);
}
}

SecurityPage::SecurityPage(QWidget *parent) : QWidget(parent)
{
    auto *layout = BootUi::page(this);
    auto *content = layout->parentWidget();
    BootUi::title(layout, content, tr("Secure Boot"),
        tr("Firmware state, Machine Owner Keys (MOK) and signature information for deployed EFI programs."));
    state_ = new QLabel(content);
    state_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    state_->setTextFormat(Qt::PlainText);
    state_->setWordWrap(true);
    layout->addWidget(state_);

    BootUi::section(layout, content, tr("Machine Owner Keys"));
    layout->addWidget(BootUi::muted(tr("Enrolled certificates:"), content));
    enrolled_ = certificateList(content, QStringLiteral("enrolledCertificates"));
    layout->addWidget(enrolled_);
    layout->addWidget(BootUi::muted(tr("Waiting for confirmation at the next restart:"), content));
    pending_ = certificateList(content, QStringLiteral("pendingCertificates"));
    layout->addWidget(pending_);
    guidance_ = BootUi::muted({}, content);
    guidance_->setObjectName(QStringLiteral("mokGuidance"));
    guidance_->setTextFormat(Qt::PlainText);
    layout->addWidget(guidance_);
    auto *buttons = new QHBoxLayout;
    enroll_ = new QPushButton(tr("Request enrollment…"), content);
    enroll_->setObjectName(QStringLiteral("requestMokEnrollment"));
    revoke_ = new QPushButton(tr("Cancel pending enrollment"), content);
    revoke_->setObjectName(QStringLiteral("revokeMokEnrollment"));
    buttons->addWidget(enroll_);
    buttons->addWidget(revoke_);
    buttons->addStretch();
    layout->addLayout(buttons);
    rawToggle_ = new QToolButton(content);
    rawToggle_->setText(tr("mokutil output"));
    rawToggle_->setCheckable(true);
    rawToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    rawToggle_->setArrowType(Qt::RightArrow);
    layout->addWidget(rawToggle_, 0, Qt::AlignLeft);
    certificates_ = new QPlainTextEdit(content);
    certificates_->setObjectName(QStringLiteral("mokCertificates"));
    certificates_->setReadOnly(true);
    certificates_->setMinimumHeight(160);
    certificates_->hide();
    layout->addWidget(certificates_);
    connect(rawToggle_, &QToolButton::toggled, certificates_, &QWidget::setVisible);
    connect(rawToggle_, &QToolButton::toggled, rawToggle_, [this](bool expanded) {
        rawToggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    });
    connect(enroll_, &QPushButton::clicked, this, &SecurityPage::requestEnrollment);
    connect(revoke_, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Cancel pending enrollment"),
                tr("Remove all certificates waiting for enrollment? MokManager will not ask for them at the next restart."))
            != QMessageBox::Yes) return;
        emit mokRequested({{QStringLiteral("operation"), QStringLiteral("revoke-import")}}, tr("Cancel pending MOK enrollment"));
    });

    BootUi::section(layout, content, tr("EFI signature information"));
    selector_ = new LoaderSelector(content);
    layout->addWidget(selector_);
    signature_ = new QPlainTextEdit(content);
    signature_->setObjectName(QStringLiteral("efiSignatureDetails"));
    signature_->setReadOnly(true);
    signature_->setMinimumHeight(150);
    layout->addWidget(signature_);
    layout->addStretch();
    connect(selector_, &LoaderSelector::selectionChanged, this, &SecurityPage::showInstance);
    connect(selector_, &LoaderSelector::methodChanged, this, &SecurityPage::methodChanged);
}

void SecurityPage::selectMethod(LoaderKind kind) { selector_->selectMethod(kind); }

void SecurityPage::updateSnapshot(const BootSnapshot &snapshot)
{
    state_->setText(tr("Firmware Secure Boot: %1\nFirmware SetupMode: %2\nKernel module signature enforcement: %3")
        .arg(flag(snapshot.secureBoot), flag(snapshot.setupMode), flag(snapshot.moduleSignatureEnforcement)));
    fill(enrolled_, snapshot.enrolledMok);
    fill(pending_, snapshot.pendingMok);
    const bool pending = pending_->topLevelItemCount() > 0;
    guidance_->setText(!snapshot.uefi ? tr("Machine Owner Keys are only available on UEFI systems.")
        : pending ? tr("Restart to finish: MokManager opens before the system starts. Choose “Enroll MOK”, confirm the certificate "
                       "and enter the password set for this request.")
        : tr("To trust a kernel or boot loader signed with your own key, request enrollment of its certificate, then restart."));
    const QString unavailable = !snapshot.uefi ? tr("This system was not started in UEFI mode.")
        : tr("mokutil is not available. Install the mokutil package.");
    const bool available = snapshot.uefi && !snapshot.securityMessages.join(QString()).contains(QLatin1String("mokutil --list-enrolled"));
    BootUi::setAvailable(enroll_, available, unavailable);
    BootUi::setAvailable(revoke_, available && pending, available ? tr("No enrollment is waiting.") : unavailable);
    certificates_->setPlainText(tr("Enrolled MOK certificates:\n%1\nPending MOK certificates:\n%2\nCheck messages:\n%3")
        .arg(snapshot.enrolledMok, snapshot.pendingMok, snapshot.securityMessages.join(QLatin1Char('\n'))));
    selector_->updateSnapshot(snapshot);
}

void SecurityPage::requestEnrollment()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose a certificate to enroll"), QString(),
        tr("DER certificates (*.der *.cer *.crt);;All files (*)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) {
        QMessageBox::warning(this, tr("Request enrollment"), tr("Unable to read the certificate."));
        return;
    }
    const QByteArray certificate = file.readAll();
    if (certificate.size() < 64 || quint8(certificate.at(0)) != 0x30) {
        QMessageBox::warning(this, tr("Request enrollment"),
            tr("This file is not a DER certificate. Convert a PEM certificate with: openssl x509 -in key.pem -outform DER -out key.der"));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Set a one-time password"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *explanation = new QLabel(tr("At the next restart MokManager asks for this password to confirm the enrollment. "
        "Choose something you can type on the boot screen; it is used once and is not stored or logged."), &dialog);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto *form = new QFormLayout;
    QLineEdit password(&dialog), repeat(&dialog);
    password.setEchoMode(QLineEdit::Password);
    repeat.setEchoMode(QLineEdit::Password);
    password.setObjectName(QStringLiteral("mokPassword"));
    repeat.setObjectName(QStringLiteral("mokPasswordRepeat"));
    form->addRow(tr("Password:"), &password);
    form->addRow(tr("Repeat:"), &repeat);
    layout->addLayout(form);
    auto *hint = BootUi::muted({}, &dialog);
    layout->addWidget(hint);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Request enrollment"));
    layout->addWidget(buttons);
    const auto validate = [&] {
        QString problem;
        if (password.text().isEmpty()) problem = tr("Enter a password.");
        else if (password.text().size() > 256) problem = tr("Use at most 256 characters.");
        else if (password.text() != repeat.text()) problem = tr("The passwords do not match.");
        hint->setText(problem);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
    };
    connect(&password, &QLineEdit::textChanged, &dialog, validate);
    connect(&repeat, &QLineEdit::textChanged, &dialog, validate);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    validate();
    if (dialog.exec() != QDialog::Accepted) return;
    emit mokRequested({{QStringLiteral("operation"), QStringLiteral("import")},
                       {QStringLiteral("certificate"), QString::fromLatin1(certificate.toBase64())},
                       {QStringLiteral("password"), password.text()}},
                      tr("Request enrollment of %1").arg(QFileInfo(path).fileName()));
}

void SecurityPage::showInstance()
{
    const int index = selector_->currentIndex();
    const auto &installations = selector_->installations();
    if (index < 0 || index >= installations.size()) {
        signature_->setPlainText(tr("No EFI program selected."));
        return;
    }
    const auto &installation = installations[index];
    const QString state = installation.signature == SignatureState::Present ? tr("Signed")
        : installation.signature == SignatureState::Absent ? tr("No signature table present") : tr("Signature state unknown");
    signature_->setPlainText(tr("Program: %1\nState: %2\n%3")
        .arg(installation.binary, state, installation.signatureDetails));
}
