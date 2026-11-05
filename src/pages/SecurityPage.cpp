#include "SecurityPage.h"
#include "widgets/BootUi.h"
#include "widgets/LoaderSelector.h"

#include <QLabel>
#include <QPlainTextEdit>
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
}

SecurityPage::SecurityPage(QWidget *parent) : QWidget(parent)
{
    auto *layout = BootUi::page(this);
    auto *content = layout->parentWidget();
    BootUi::title(layout, content, tr("Secure Boot"),
        tr("Firmware state, public MOK certificates and signature information for deployed EFI programs."));
    state_ = new QLabel(content);
    state_->setTextFormat(Qt::PlainText);
    state_->setWordWrap(true);
    layout->addWidget(state_);
    BootUi::section(layout, content, tr("MOK certificates and pending enrollment"));
    certificates_ = new QPlainTextEdit(content);
    certificates_->setObjectName(QStringLiteral("mokCertificates"));
    certificates_->setReadOnly(true);
    certificates_->setMinimumHeight(180);
    layout->addWidget(certificates_);
    BootUi::section(layout, content, tr("EFI signature information"));
    selector_ = new LoaderSelector(content);
    layout->addWidget(selector_);
    signature_ = new QPlainTextEdit(content);
    signature_->setObjectName(QStringLiteral("efiSignatureDetails"));
    signature_->setReadOnly(true);
    signature_->setMinimumHeight(150);
    layout->addWidget(signature_);
    layout->addWidget(BootUi::muted(tr("A signature table does not prove firmware trust or a verified boot chain. Key creation, enrollment and signing are not yet available here."), content));
    layout->addStretch();
    connect(selector_, &LoaderSelector::selectionChanged, this, &SecurityPage::showInstance);
    connect(selector_, &LoaderSelector::methodChanged, this, &SecurityPage::methodChanged);
}

void SecurityPage::selectMethod(LoaderKind kind) { selector_->selectMethod(kind); }

void SecurityPage::updateSnapshot(const BootSnapshot &snapshot)
{
    state_->setText(tr("Firmware Secure Boot: %1\nFirmware SetupMode: %2\nKernel module signature enforcement: %3")
        .arg(flag(snapshot.secureBoot), flag(snapshot.setupMode), flag(snapshot.moduleSignatureEnforcement)));
    certificates_->setPlainText(tr("Enrolled MOK certificates:\n%1\nPending MOK certificates:\n%2\nCheck messages:\n%3")
        .arg(snapshot.enrolledMok, snapshot.pendingMok, snapshot.securityMessages.join(QLatin1Char('\n'))));
    selector_->updateSnapshot(snapshot);
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
    const QString state = installation.signature == SignatureState::Present ? tr("Signature table present; trust unverified")
        : installation.signature == SignatureState::Absent ? tr("No signature table present") : tr("Signature state unknown");
    signature_->setPlainText(tr("Program: %1\nState: %2\n%3")
        .arg(installation.binary, state, installation.signatureDetails));
}
