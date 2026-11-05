#include "LoaderSelector.h"

#include <QComboBox>
#include <QDir>
#include <QTabBar>
#include <QVBoxLayout>

LoaderSelector::LoaderSelector(QWidget *parent) : QWidget(parent)
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

void LoaderSelector::updateSnapshot(const BootSnapshot &snapshot)
{
    installations_ = snapshot.manageableInstallations();
    tabs_->blockSignals(true);
    if (!initialized_ && !installations_.isEmpty()) {
        LoaderKind initial = installations_.first().kind;
        for (const auto &loader : installations_) if (loader.current) { initial = loader.kind; break; }
        selectMethod(initial);
    }
    for (int i = 0; i < static_cast<int>(loaderKinds.size()); ++i) {
        int count = 0;
        for (const auto &loader : installations_) if (loader.kind == loaderKinds[i]) ++count;
        tabs_->setTabText(i, loaderName(loaderKinds[i]));
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
        instances_->addItem(location + QStringLiteral(" · ")
            + (loader.binary.isEmpty() ? loader.config : QDir(loader.esp).relativeFilePath(loader.binary)), loader.id);
    }
    int index = instances_->findData(previous);
    if (index < 0 && !filtered_.isEmpty()) index = 0;
    instances_->setCurrentIndex(index);
    instances_->setEnabled(!filtered_.isEmpty());
    instances_->blockSignals(false);
    selected_.insert(static_cast<int>(method()), currentId());
    emit selectionChanged();
}

int LoaderSelector::currentIndex() const { return instances_->currentIndex(); }
QString LoaderSelector::currentId() const { return instances_->currentData().toString(); }
