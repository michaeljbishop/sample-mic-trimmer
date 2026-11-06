#include "mainwindow.h"
#include "spitfiresettings.h"
#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QMenuBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QUrl>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>
#include "corelib.h"
#include "coreutil.h"
#include <algorithm>

#include "elidedlabel.h"
#include "imagefetcher.h"
#include "micmatrixcolors.h"
#include "micmatrixmodel.h"
#include "palette.h"
#include "micmatrixview.h"
#include "pathsafety.h"
#include "plandialog.h"
#include "profileartwork.h"
#include "profilepaths.h"
#include "workrelay.h"
#include "darktheme.h"
#include "ui_mainwindow.h"

const QString kWindowGeometry = "WindowGeometry";
const QString kWindowState = "WindowState";
const QString kDownloadImages = "DownloadProfileImages"; // bool, default true

using namespace smt;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui_MainWindow)
    , mCore(new smt::Core(this))
{
    ui->setupUi(this);
    mCore->setBackgroundWork(true); // free-space queries, plans, derivations and config reads never run on this thread
    mCore->setSelectionFollowsDestination(true); // boxes show what is complete in the destination

    ui->sourceDirLabel->setElideMode(Qt::TextElideMode::ElideMiddle);
    ui->destDirLabel->setElideMode(Qt::TextElideMode::ElideMiddle);
    setPathLabel(ui->sourceDirLabel, mCore->sourceDir());
    setPathLabel(ui->destDirLabel, mCore->destinationDirectoryPath());

    QSettings s;

    qRegisterMetaType<std::function<void()>>("std::function<void()>");
    mRelay = std::shared_ptr<WorkRelay>(new WorkRelay, [](WorkRelay *r) { r->deleteLater(); });
    connect(mRelay.get(), &WorkRelay::post, this, [](std::function<void()> completion) {
        if (completion)
            completion();
    });
    mFetcher = new ImageFetcher(QStringLiteral("www.spitfireaudio.com"), true, this);
    connect(mFetcher, &ImageFetcher::fetched, this, [this] { updateArtwork(); });
    auto *fileMenu = menuBar()->addMenu(tr("File"));
    auto *reveal = fileMenu->addAction(tr("Reveal Profiles Folder"));
    mDownloadImages = fileMenu->addAction(tr("Download Profile Pictures"));
    mDownloadImages->setCheckable(true);
    mDownloadImages->setChecked(s.value(kDownloadImages, true).toBool());
    connect(mDownloadImages, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue(kDownloadImages, on);
        updateArtwork(); // fetches now when needed
    });

    matrixView = new MicMatrixView(new MicMatrixModel(mCore, this), this);
    matrixView->setObjectName(QStringLiteral("matrixView"));
    ui->matrixLayout->addWidget(matrixView);
    mEmptyHint = new QLabel;
    mHintChoose = tr("<p style=\"font-size:15px; color:%1\"><b>Choose a Source Installation to begin</b></p>"
                     "<p>Its microphones and sizes will appear here.</p>").arg(Palette::alto.name());
    mHintScanning = tr("<p style=\"font-size:15px; color:%1\"><b>Scanning…</b></p>"
                       "<p>Its microphones and sizes will appear here in a moment.</p>").arg(Palette::alto.name());
    mEmptyHint->setText(mHintChoose);
    mEmptyHint->setObjectName(QStringLiteral("emptyHintLabel"));
    mEmptyHint->setAlignment(Qt::AlignCenter);
    mEmptyHint->setMinimumHeight(160);
    mEmptyHint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); // fills the matrix's room, so nothing below moves
    mEmptyHint->setStyleSheet(QStringLiteral("color: %1;").arg(Palette::body.name()));
    ui->matrixLayout->addWidget(mEmptyHint);
    QStringList legend;
    for (const auto &entry : MatrixColors::legend())
        legend << QStringLiteral("<span style=\"color:%1\">&#9632;</span> %2").arg(entry.first.name(), entry.second.toHtmlEscaped());
    ui->legendLabel->setText(legend.join(QStringLiteral("&nbsp;&nbsp;&nbsp; ")));
    ui->legendLabel->setStyleSheet(QStringLiteral("color: %1;").arg(Palette::body.name()));
    stylePrimaryButton(ui->applyButton);
    // Apply keeps one width whatever it says: a minimum as wide as its longest text; the status label beside it
    // (Expanding, stretch 1, even when empty) takes all the remaining room.
    {
        const QString shown = ui->applyButton->text();
        ui->applyButton->setText(applyButtonText(longestPendingChange()));
        ui->applyButton->setMinimumWidth(ui->applyButton->sizeHint().width());
        ui->applyButton->setText(shown);
    }
    // The footer (installation size, space remaining) ends under the Row total column's numbers.
    auto alignFooter = [this] {
        ui->footerLayout->setContentsMargins(0, 0, matrixView->totalTextRightInset(), 0);
    };
    connect(matrixView, &MicMatrixView::totalTextRightInsetChanged, this, alignFooter);
    alignFooter();
    rebuildProfileUi();
    connect(mCore, &smt::Core::profileChanged, this, [this] { rebuildProfileUi(); });
    connect(ui->libraryChoiceComboBox, &QComboBox::activated, this, [this](int index) {
        const QString id = ui->libraryChoiceComboBox->itemData(index).toString();
        if (id != mCore->profile().id)
            switchProfile(id);
        rebuildProfileUi(); // snaps back to the real profile if the switch was refused
    });
    connect(reveal, &QAction::triggered, this, [] {
        QDir().mkpath(userProfilesDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(userProfilesDir()));
    });

    ui->chooseSourceButton->setAttribute(Qt::WA_MacSmallSize);
    ui->rescanSourceButton->setAttribute(Qt::WA_MacSmallSize);
    ui->chooseDestDirButton->setAttribute(Qt::WA_MacSmallSize);

    connect(mCore, &smt::Core::totalSpaceRequiredChanged, this, [this] { updateFooter(); });
    updateFooter();

    connect(mCore, &smt::Core::sourceDirChanged, this, [this](const QString &text) {
        setPathLabel(ui->sourceDirLabel, text);
        updateArtwork();
    });

    connect(mCore, &smt::Core::destinationDirectoryPathChanged, this, [this](const QString &text) {
        setPathLabel(ui->destDirLabel, text);
        // The remaining figure belongs to the old destination: unknown until the new volume has been asked
        // (Core emits this before the next scan starts).
        mFooter.remaining.reset();
        updateFooter();
    });
    connect(mCore, &smt::Core::remainingSpaceChanged, this, [this](qint64 size) {
        mFooter.remaining = size;
        updateFooter();
    });

    connect(ui->chooseSourceButton, &QPushButton::clicked, this, [=] {
        if (mApplying)
            return;
        ProfilePaths paths;
        const QString path
            = QFileDialog::getExistingDirectory(this, {}, paths.source(mCore->profile().id));
        if (path.isEmpty())
            return;
        mCore->setSourceDir(path);
        // The only place that picks a profile from a folder name (Core's own auto-switch is off once chosen).
        const QString suggestion = mCore->profileSuggestion();
        if (!suggestion.isEmpty() && suggestion != mCore->profile().id)
            switchProfile(suggestion, false); // the picked path stays the source
        paths.setSource(mCore->profile().id, path);
    });

    connect(ui->chooseDestDirButton, &QPushButton::clicked, this, [=] {
        if (mApplying)
            return;
        ProfilePaths paths;
        const QString picked
            = QFileDialog::getExistingDirectory(this, {}, paths.destination(mCore->profile().id));
        if (!picked.isEmpty())
            chooseDestinationFolder(picked);
    });

    // Rescan: enabled only while the source scan is finished and no apply runs.
    ui->rescanSourceButton->setEnabled(false);
    connect(ui->rescanSourceButton, &QPushButton::clicked, this, [this] {
        mSourceScanned = false;
        ui->rescanSourceButton->setEnabled(false);
        mCore->rescanSource();
    });
    connect(mCore, &smt::Core::sourceDirChanged, this, [this] {
        mSourceScanned = false;
        ui->rescanSourceButton->setEnabled(false);
    });
    connect(mCore, &smt::Core::sourceScanFinished, this, [this] {
        mSourceScanned = true;
        ui->rescanSourceButton->setEnabled(!mApplying);
    });
    // After the handlers above, which keep mSourceScanned current.
    connect(mCore, &smt::Core::sourceDirChanged, this, [this] {
        updateEmptyState();
    });
    connect(mCore, &smt::Core::sourceScanFinished, this, [this] {
        updateEmptyState();
    });
    connect(mCore, &smt::Core::scanFinished, this, [this] {
        updateApplyEnabled();
    });
    connect(mCore, &smt::Core::scanStatusChanged, this, [this] {
        updateEmptyState();
        updateApplyEnabled();
    });
    connect(mCore, &smt::Core::destinationDirectoryPathChanged, this, [this] { updateApplyEnabled(); });
    connect(mCore, &smt::Core::applyStepChanged, this, [this] {
        mApplying = true;
        ui->rescanSourceButton->setEnabled(false);
        updateProfileControlsEnabled();
        updateApplyEnabled();
    });
    connect(mCore, &smt::Core::applyFinished, this, [this] {
        mApplying = false;
        ui->rescanSourceButton->setEnabled(mSourceScanned);
        updateProfileControlsEnabled();
        updateApplyEnabled();
    });

    connect(ui->useInSpitfireButton, &QPushButton::clicked, this, [this] {
        QMessageBox box(QMessageBox::Question, tr("Change current installation location"),
                        tr("Which installation should the Spitfire app and plugins use?\n\nOnly Spitfire's "
                           "Spitfire.properties changes (a backup is kept next to it). Reload the plugin afterwards."),
                        QMessageBox::Cancel, this);
        const auto *trimmed = box.addButton(tr("Reduced mic installation"), QMessageBox::AcceptRole);
        const auto *original = box.addButton(tr("Source installation"), QMessageBox::AcceptRole);
        box.exec();
        if (box.clickedButton() != trimmed && box.clickedButton() != original)
            return;
        const bool toReduced = box.clickedButton() == trimmed;
        const auto r = mCore->useLibrary(toReduced);
        if (r.ok())
            QMessageBox::information(this, tr("Change current installation location"),
                                     (r.written ? tr("Spitfire now uses the %1:\n%2\n\nChanged entries: %3.\nReload the plugin in your DAW.")
                                                : tr("Spitfire already used the %1:\n%2\n\nEntries: %3.\nNothing was changed."))
                                         .arg(toReduced ? tr("Reduced Mic Installation") : tr("Source Installation"),
                                              toReduced ? mCore->destinationDirectoryPath() : mCore->sourceDir(),
                                              r.products.join(", ")));
        else
            QMessageBox::warning(this, tr("Change current installation location"), r.error);
    });

    // The plan (path checks, free space, the plan itself) is built on a worker; the dialog opens when it is ready.
    connect(ui->applyButton, &QPushButton::clicked, this, [this] {
        mClickedPlan = mCore->requestPlan();
        ui->applyStatusLabel->setText(tr("Preparing\u2026"));
        ui->applyStatusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(Palette::body.name()));
    });
    connect(mCore, &smt::Core::planReady, this, [this](const Plan &plan, quint64 token) {
        if (token != mClickedPlan)
            return; // the plan of an earlier look at the state, not of the click
        mClickedPlan = 0;
        updateApplyEnabled();
        PlanDialog dlg(mCore, plan, this);
        dlg.exec(); // Core rescans the destination after an apply; nothing to refresh here
    });
    connect(mCore, &smt::Core::pendingChangeReady, this, [this](const PendingChange &change, quint64 token) {
        if (token == mPendingRequest)
            showApplyState(change, false);
    });
    connect(mCore, &smt::Core::applyProgress, this, [this] { updateApplyEnabled(); });
    connect(mCore, &smt::Core::micEnabledChanged, this, [this] { updateApplyEnabled(); });
    connect(mCore, &smt::Core::profileChanged, this, [this] { updateApplyEnabled(); });
    connect(mCore, &smt::Core::configurationLoaded, this, [this] { updateApplyEnabled(); });
    connect(mCore, &smt::Core::sourceScanFinished, this, [this] { updateApplyEnabled(); });

    connect(mCore, &smt::Core::destinationDirectoryPathChanged, this, [this] { mConfigErrors.rearm(); });
    connect(mCore, &smt::Core::configurationLoaded, this, [this] { mConfigErrors.rearm(); });
    connect(mCore, &smt::Core::configurationError, this, [this](const QString &message) {
        if (!mConfigErrors.shouldShow(message))
            return;
        // Queued: never open a modal box inside Core's emit (it fires from the constructor too).
        QMetaObject::invokeMethod(this, [this, message] { QMessageBox::warning(this, tr("Configuration"), message); },
                                  Qt::QueuedConnection);
    });

    // Always an explicit switch (marks the profile chosen), so Core never auto-switches behind our back.
    ProfilePaths paths;
    QString id = paths.lastProfile();
    if (!switchProfile(id))
        switchProfile(mCore->profile().id);

    restoreGeometry(s.value(kWindowGeometry).toByteArray());
    restoreState(s.value(kWindowState).toByteArray());
}

// A destination the user picked: when its name is not the one the source needs, the folder with that name inside it
// is used instead (created by Apply, not here: nothing is written to the destination before the checks pass).
void MainWindow::chooseDestinationFolder(const QString &picked)
{
    const QString path = resolveDestinationFolder(mCore->sourceDir(), picked);
    ProfilePaths paths;
    paths.setDestination(mCore->profile().id, path); // the stored folder is the resolved one
    if (!mConfigErrors.shouldSetDestination(path, mCore->destinationDirectoryPath()))
        return;
    mConfigErrors.rearm(); // a still-broken file is reported again
    mCore->setDestinationDirectoryPath(path);
}

// The one place that switches profile: remember it and load its stored folders.
bool MainWindow::switchProfile(const QString &id, bool loadSource)
{
    const auto &list = mCore->availableProfiles();
    const bool valid = std::any_of(list.begin(), list.end(), [&](const auto &e) { return e.id == id && e.valid(); });
    if (mApplying || !valid)
        return false;
    // Clear first: the old destination's project.smt belongs to the old profile and would raise a false error.
    if (id != mCore->profile().id)
        mCore->setDestinationDirectoryPath({});
    mCore->setProfileById(id);
    ProfilePaths paths;
    paths.setLastProfile(id);
    if (loadSource)
        mCore->setSourceDir(paths.source(id));
    mCore->setDestinationDirectoryPath(paths.destination(id)); // also loads its config
    return true;
}

// Pop-up, tooltip, artwork and quick buttons: everything that follows the current profile.
void MainWindow::rebuildProfileUi()
{
    auto *combo = ui->libraryChoiceComboBox;
    const QSignalBlocker block(combo);
    combo->clear();
    int current = -1;
    for (const ProfileEntry &e : mCore->availableProfiles()) {
        combo->addItem(e.valid() ? e.name : tr("%1 (invalid: %2)").arg(e.name, e.error), e.id);
        if (!e.valid())
            qobject_cast<QStandardItemModel *>(combo->model())->item(combo->count() - 1)->setEnabled(false);
        if (e.id == mCore->profile().id)
            current = combo->count() - 1;
    }
    combo->setCurrentIndex(current);
    combo->setToolTip(mCore->profile().notes);

    while (QLayoutItem *item = ui->quickButtonsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    auto addButton = [this](const QString &text, std::function<void()> fn) {
        auto *b = new QPushButton(text);
        connect(b, &QPushButton::clicked, this, [fn] { fn(); });
        ui->quickButtonsLayout->addWidget(b);
    };
    const auto &sets = mCore->profile().quickSets;
    for (int i = 0; i < sets.size(); ++i)
        addButton(sets[i].name, [this, i] { mCore->applyQuickSet(i); });
    ui->quickButtonsWidget->setVisible(!sets.isEmpty()); // no empty gap in the header when a profile has none
    setWindowTitle(mainWindowTitle(smt::kApplicationName, mCore->profile().name));
    updateEmptyState();

    updateProfileControlsEnabled();
    updateApplyEnabled();
    updateArtwork();
}

void MainWindow::updateProfileControlsEnabled()
{
    ui->libraryChoiceComboBox->setEnabled(!mApplying);
    ui->quickButtonsWidget->setEnabled(!mApplying);
    ui->useInSpitfireButton->setEnabled(!mApplying);
}

// The one place the Apply button is set: not while an apply runs, only with a mic selected and something to do;
// its text says what the plan would change.
void MainWindow::updateApplyEnabled()
{
    // Coalesced: a quick set or a column toggle changes many cells in a row, and each plan covers every file.
    if (mApplyUpdatePending)
        return;
    mApplyUpdatePending = true;
    QTimer::singleShot(0, this, [this] {
        mApplyUpdatePending = false;
        if (mApplying || mCore->scanning() || !mCore->anyMicEnabled() || !mCore->hasSourceContent()) {
            mPendingRequest = 0;
            showApplyState({}, false); // nothing to ask a worker: waiting, applying, no mic or no source
            return;
        }
        // Everything else needs the plan: path checks, free space and the plan run on a worker.
        mPendingRequest = mCore->requestPendingChange();
        showApplyState({}, true);
    });
}

// Apply's text, enabled state and the message beside it, from what the plan said (or "Preparing…" while it is built).
void MainWindow::showApplyState(const PendingChange &pending, bool preparing)
{
    const bool any = mCore->anyMicEnabled();
    const bool source = mCore->hasSourceContent();
    const bool scanning = mCore->scanning();
    ui->applyButton->setText(applyButtonText(pending));
    ui->applyButton->setEnabled(!mApplying && !preparing && any && source && applyEnabled(pending));
    // Why Apply is greyed, said right beside it (and as its tooltip). Nothing while it is enabled, while applying,
    // or without a source (the hint says it); while a scan or the plan runs, the progress in the muted grey.
    QString why;
    QColor color = Palette::errorMuted;
    if (scanning || preparing) {
        why = preparing && !scanning ? tr("Preparing\u2026") : scanStatusText(mCore->scanStatus());
        color = Palette::body;
    } else if (!any) {
        why = tr("Select at least one microphone.");
        color = Palette::amber;
    } else if (pending.known && pending.refused) {
        why = pending.firstRefusal;
    }
    if (mApplying || (!source && !scanning))
        why.clear();
    ui->applyButton->setToolTip(scanning || preparing ? QString() : why);
    ui->applyStatusLabel->setText(why);
    ui->applyStatusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
}

void MainWindow::setPathLabel(ElidedLabel *label, const QString &path)
{
    label->setText(path.isEmpty() ? tr("No folder chosen") : path);
    label->setToolTip(path);
}

// No source (or an empty one): a hint replaces the matrix, the legend and the footer.
void MainWindow::updateEmptyState()
{
    // Nothing to show yet: no source, or a scan that has not delivered its first files.
    const ScanStatus scan = mCore->scanStatus();
    const bool scanningNothingYet = scan.source && scan.sourceFiles == 0;
    const bool empty = !mCore->hasSourceContent() || scanningNothingYet;
    mEmptyHint->setText(scanningNothingYet ? mHintScanning : mHintChoose);
    matrixView->setVisible(!empty);
    mEmptyHint->setVisible(empty);
    ui->legendLabel->setVisible(!empty);
    ui->installationSizeLabel->setVisible(!empty);
    updateApplyEnabled();
}

void MainWindow::updateFooter()
{
    ui->installationSizeLabel->setText(mFooter.text(mCore->totalSpaceForSelectedItems()));
    ui->installationSizeLabel->setStyleSheet(mFooter.needsMoreSpace() ? "color: " + Palette::errorMuted.name() : QString());
}

// Which picture to show: resolving it looks into the Spitfire app's cache and the library folder (maybe a slow mount),
// so a worker thread does that; the result is applied here if no newer request came in.
void MainWindow::updateArtwork()
{
    const quint64 token = ++mArtworkToken;
    const bool download = mDownloadImages->isChecked();
    QThreadPool::globalInstance()->start([this, relay = mRelay, profile = mCore->profile(), source = mCore->sourceDir(), token, download] {
        const QString image = QStringLiteral("main");
        const QString urlDir = profileImageUrlCacheDir();
        const QString shown = resolveProfileImage(profile, image, source, spitfireImageCacheDir(), urlDir);
        const QList<QUrl> pending = download ? pendingImageUrls(profile, image, source, spitfireImageCacheDir(), urlDir) : QList<QUrl>{};
        const QString credit = profile.imageCredits.value(image);
        emit relay->post([this, token, shown, urlDir, pending, credit] { applyArtwork(token, shown, urlDir, pending, credit); });
    });
}

void MainWindow::applyArtwork(quint64 token, const QString &shown, const QString &urlDir, const QList<QUrl> &pending,
                              const QString &credit)
{
    if (token != mArtworkToken)
        return;
    ui->profileArtwork->setImageFile(shown);
    // The credit belongs to Spitfire's picture only.
    const bool theirs = shown.startsWith(spitfireImageCacheDir() + QLatin1Char('/')) || shown.startsWith(urlDir + QLatin1Char('/'));
    ui->profileArtwork->setToolTip(credit.isEmpty() || !theirs ? QString() : tr("Picture: %1").arg(credit));
    for (const QUrl &url : pending)
        mFetcher->fetch(url, urlDir + QLatin1Char('/') + imageUrlCacheName(url)); // silent on failure
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings s;
    s.setValue(kWindowGeometry, saveGeometry());
    s.setValue(kWindowState, saveState());
    QMainWindow::closeEvent(event);
}
