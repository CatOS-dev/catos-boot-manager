#include "LoaderSelector.h"
#include "BootUi.h"
#include "services/BootDeploy.h"

#include <QComboBox>
#include <QDir>
#include <QTabBar>
#include <QVBoxLayout>

LoaderSelector::LoaderSelector(QWidget *parent, bool menuScope) : QWidget(parent), menuScope_(menuScope)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabBar(this);
    tabs_->setObjectName(QStringLiteral("loaderMethodTabs"));
    tabs_->setExpanding(false);
    tabs_->setUsesScrollButtons(true);
    for (const auto kind : loaderKinds) tabs_->addTab(loaderName(kind));
    layout->addWidget(tabs_);
    instances_ = new QComboBox(this);
    BootUi::enableCopy(instances_);
    instances_->setObjectName(QStringLiteral("loaderInstallation"));
    instances_->setPlaceholderText(tr("No installation detected for this method"));
    layout->addWidget(instances_);
    connect(tabs_, &QTabBar::currentChanged, this, [this] {
        initialized_ = true;
        rebuild();
        emit methodChanged(method());
    });
    connect(instances_, &QComboBox::currentIndexChanged, this, [this] {
        selected_.insert(static_cast<int>(method()), currentId());
        emit selectionChanged();
        emit installationChanged(method(), currentId());
    });
}

LoaderKind LoaderSelector::method() const
{
    return loaderKinds.at(tabs_->currentIndex());
}

void LoaderSelector::selectMethod(LoaderKind kind)
{
    initialized_ = true;
    for (int i = 0; i < static_cast<int>(loaderKinds.size()); ++i)
        if (loaderKinds[i] == kind) tabs_->setCurrentIndex(i);
}

void LoaderSelector::selectInstallation(LoaderKind kind, const QString &id)
{
    if (method() == kind && currentId() == id) return;
    bool available = false;
    for (const auto &installation : installations_)
        available |= installation.kind == kind && installation.id == id;
    if (available) selected_.insert(static_cast<int>(kind), id);
    if (method() != kind) selectMethod(kind);
    else if (available) instances_->setCurrentIndex(instances_->findData(id));
}

void LoaderSelector::updateSnapshot(const BootSnapshot &snapshot)
{
    installations_.clear();
    QSet<int> deployments;
    QSet<int> configurations;
    for (const auto &installation : snapshot.installations) {
        if (!installation.binary.isEmpty()) deployments.insert(static_cast<int>(installation.kind));
        if (installation.binary.isEmpty() && !installation.config.isEmpty()
            && (installation.kind == LoaderKind::Grub || BootDeploy::directImage(installation.kind)))
            configurations.insert(static_cast<int>(installation.kind));
    }
    for (const auto &installation : snapshot.installations) {
        const int kind = static_cast<int>(installation.kind);
        const bool generator = configurations.contains(kind) && installation.binary.isEmpty() && !installation.config.isEmpty();
        if (menuScope_ && configurations.contains(kind) && !generator) continue;
        if (!menuScope_ && generator && deployments.contains(kind)) continue;
        installations_.append(installation);
    }
    tabs_->blockSignals(true);
    if (!initialized_ && !installations_.isEmpty()) {
        LoaderKind initial = installations_.first().kind;
        for (const auto &loader : installations_) if (loader.current) { initial = loader.kind; break; }
        selectMethod(initial);
    }
    for (int i = 0; i < static_cast<int>(loaderKinds.size()); ++i) {
        int count = 0;
        bool program = false;
        bool issue = false;
        bool defaultEntry = false;
        for (const auto &loader : installations_) {
            if (loader.kind != loaderKinds[i]) continue;
            ++count;
            program |= loader.binaryPresent;
            issue |= !loader.issues.isEmpty();
            defaultEntry |= loader.defaultEntry;
        }
        const QString status = issue ? tr("Needs attention") : defaultEntry ? tr("Default entry")
            : program ? tr("Program detected") : count ? tr("Configuration only") : tr("Not detected");
        tabs_->setTabText(i, loaderName(loaderKinds[i]) + QStringLiteral(" · ") + status);
        tabs_->setTabToolTip(i, tr("Detected manageable installations: %1").arg(count));
    }
    tabs_->blockSignals(false);
    rebuild();
}

void LoaderSelector::rebuild()
{
    const QString previous = selected_.value(static_cast<int>(method()));
    filtered_.clear();
    instances_->blockSignals(true);
    instances_->clear();
    for (const auto &loader : installations_) {
        if (loader.kind != method()) continue;
        filtered_.append(loader);
        const QString location = loader.esp.isEmpty() ? tr("Current system") : loader.esp;
        QString label = location + QStringLiteral(" · ")
            + (loader.binary.isEmpty() ? loader.config : QDir(loader.esp).relativeFilePath(loader.binary));
        if (loader.current) label += QStringLiteral(" · ") + tr("Current boot");
        if (loader.defaultEntry) label += QStringLiteral(" · ") + tr("Default entry");
        if (loader.nextEntry) label += QStringLiteral(" · ") + tr("Next boot only");
        instances_->addItem(label, loader.id);
    }
    int index = instances_->findData(previous);
    if (index < 0 && !filtered_.isEmpty()) index = 0;
    instances_->setCurrentIndex(index);
    instances_->setEnabled(!filtered_.isEmpty());
    instances_->blockSignals(false);
    selected_.insert(static_cast<int>(method()), currentId());
    emit selectionChanged();
    emit installationChanged(method(), currentId());
}

int LoaderSelector::currentIndex() const { return instances_->currentIndex(); }
QString LoaderSelector::currentId() const { return instances_->currentData().toString(); }
