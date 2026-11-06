#include "corelib.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <utility>
#include <QStandardPaths>
#include <QThreadPool>
#include "completeness.h"
#include "fileinfogatherer.h"
#include "pathsafety.h"
#include "planexecutor.h"
#include "profileregistry.h"
#include "projectconfig.h"

namespace {

const QString kScanCacheDir("ScanCache");
constexpr int kSessionScans = 3; // finished source scans kept in memory

// Storage of path, or of its nearest existing ancestor if it does not exist yet.
QStorageInfo storageFor(const QString &path)
{
    if (path.isEmpty())
        return {};
    QString p = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    while (!QFileInfo::exists(p)) {
        const QString parent = QFileInfo(p).path();
        if (parent == p)
            return {};
        p = parent;
    }
    return QStorageInfo(p);
}

void discard(QPointer<smt::FileInfoGatherer> &gatherer, QObject *receiver)
{
    if (!gatherer)
        return;
    gatherer->cancel();
    QObject::disconnect(gatherer, nullptr, receiver, nullptr);
    gatherer->deleteLater();
    gatherer.clear();
}

} // namespace

namespace smt {

Core::Core(QObject *parent)
    : QObject(parent)
    , mProfile(builtInProfile(kBbcsoProfileId))
{
    const QString dataDir = qEnvironmentVariable("SMT_APP_DATA_DIR");
    setApplicationDataDir(dataDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                                            : dataDir);
    QDir().mkpath(mApplicationDataDir); // the source scan caches live here
    // Old caches (the single-folder file, per-folder JSON files) and scans unused for 90 days go, off the GUI thread.
    QThreadPool::globalInstance()->start([dir = mApplicationDataDir] {
        QFile::remove(dir + "/sourceDirectoryCache.json");
        const QDir cache(dir + '/' + kScanCacheDir);
        for (const QString &name : cache.entryList({"*.json"}, QDir::Files))
            QFile::remove(cache.filePath(name));
        const QDateTime oldest = QDateTime::currentDateTime().addDays(-90);
        for (const QFileInfo &fi : cache.entryInfoList({"*.smtcache"}, QDir::Files))
            if (fi.lastModified() < oldest)
                QFile::remove(fi.filePath());
    });
    qRegisterMetaType<std::function<void()>>("std::function<void()>");
    mRelay = std::shared_ptr<WorkRelay>(new WorkRelay, [](WorkRelay *r) { r->deleteLater(); }); // may die on a pool thread
    connect(mRelay.get(), &WorkRelay::post, this, [](std::function<void()> completion) {
        if (completion)
            completion();
    });
    mProfiles = loadProfiles(userProfilesDir());
    for (const ProfileEntry &e : std::as_const(mProfiles)) // a valid user override of the built-in wins
        if (e.valid() && e.id == kBbcsoProfileId) mProfile = *e.profile;
}

QString Core::sourceDir() const
{
    return mSourceDir;
}

QString Core::cacheFilePathFor(const QString &sourceDir) const
{
    const QByteArray key = QCryptographicHash::hash(QDir::cleanPath(sourceDir).toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(applicationDataDir()).absoluteFilePath(kScanCacheDir + '/' + QString::fromLatin1(key) + ".smtcache");
}

void Core::startSourceFileGatherer()
{
    discard(mSourceFileInfoTask, this);
    ++mGeneration;
    mScanFinishedEmitted = false;

    ++mDeriveToken;
    mDeriving = false;
    mSpaceRequired.clear();
    mSizeOfLibraryFiles = 0;
    mScan.source = false;
    mScan.sourceFiles = 0;
    mScan.sourceExpected = -1;
    emit totalSpaceRequiredChanged(0);
    forAllMicSignalGroupKeys(mProfile, [&](const MicSignalGroupKey &key) { emit spaceRequiredChanged(key, 0); });
    recalculateRemainingSpace();

    if (mSourceDir.isEmpty()) { // QDir("") is the current directory
        emit scanStatusChanged();
        deriveSelection();
        return;
    }

    const QString dir = QDir::cleanPath(mSourceDir), cache = cacheFilePathFor(dir);
    mSourceFileInfoTask = new FileInfoGatherer(dir, cache, this);
    mSourceFileInfoTask->setSkipCacheRead(std::exchange(mSkipCacheOnce, false)); // a rescan walks the disk
    if (mSourceScans.contains(dir)) { // scanned earlier in this session: reuse as is (Rescan source refreshes)
        mSourceFileInfoTask->setPreloaded(mSourceScans.value(dir));
        mScan.sourceExpected = mSourceScans.value(dir).size();
    } else {
        // Known from this session, else the task reads it from the cache header on its worker.
        mScan.sourceExpected = mExpectedFiles.contains(dir) ? mExpectedFiles.value(dir) : -1;
        mSourceFileInfoTask->setExpectedTotal(mScan.sourceExpected);
    }
    mScan.source = true;

    connect(mSourceFileInfoTask.data(), &FileInfoGatherer::foundFileInfos, this, &Core::sourceBatch);
    connect(mSourceFileInfoTask.data(), &FileInfoGatherer::progress, this, [this](qint64 scanned, qint64 expected) {
        mScan.sourceFiles = scanned;
        mScan.sourceExpected = expected;
        emit scanStatusChanged();
    });
    connect(mSourceFileInfoTask.data(), &FileInfoGatherer::finished, this, [this, dir](bool success) {
        mScan.source = false;
        const QList<FileInfo> infos = mSourceFileInfoTask->infos();
        if (success) {
            mExpectedFiles[dir] = infos.size();
            const bool hasDb = std::any_of(infos.cbegin(), infos.cend(), [](const FileInfo &i) { return isDatabasePath(i.path); });
            if (hasDb) { // same rule as the disk cache: a scan without a section DB is not remembered
                mSourceScans.insert(dir, infos);
                mSourceScanOrder.removeAll(dir);
                mSourceScanOrder.prepend(dir);
                while (mSourceScanOrder.size() > kSessionScans) // bounded: the oldest folder's scan is dropped
                    mSourceScans.remove(mSourceScanOrder.takeLast());
            }
        }
        emit scanStatusChanged();
        emit sourceScanFinished();
        checkScansFinished();
    });
    emit scanStatusChanged();
    mSourceFileInfoTask->run();
}

// One batch of the source scan: accounting once per batch instead of once per file.
void Core::sourceBatch(const QList<FileInfo> &files)
{
    QSet<MicSignalGroupKey> touched;
    for (const FileInfo &info : files) {
        const auto c = classify(mProfile, info.path);
        if (c.kind == FileKind::Mic) {
            mSpaceRequired[c.key] += info.size;
            touched.insert(c.key);
        } else {
            mSizeOfLibraryFiles += info.size;
        }
    }
    for (const MicSignalGroupKey &key : std::as_const(touched))
        emit spaceRequiredChanged(key, mSpaceRequired.value(key));
    emit totalSpaceRequiredChanged(totalSpaceForSelectedItems());
    recalculateRemainingSpace();
}

void Core::destBatch(const QList<FileInfo> &files)
{
    for (const FileInfo &info : files)
        mTotalSpaceUsed += info.size;
    recalculateRemainingSpace();
}

void Core::startDestFileGatherer()
{
    discard(mDestFileInfoTask, this);
    ++mGeneration;
    mScanFinishedEmitted = false;
    mScanFinishedOwed = false;

    mTotalSpaceUsed = 0;
    mScan.dest = false;
    mScan.destFiles = 0;
    mScan.destExpected = -1;
    ++mDeriveToken; // a derivation for the old scans is stale
    mDeriving = false;
    refreshFreeSpace();

    if (mBackupDir.isEmpty()) {
        emit scanStatusChanged();
        deriveSelection();
        return;
    }

    // Never trusted stale: a destination is always scanned again (what is on disk decides the plan).
    mDestFileInfoTask = new FileInfoGatherer(mBackupDir, this);
    mScan.dest = true;
    connect(mDestFileInfoTask.data(), &FileInfoGatherer::foundFileInfos, this, &Core::destBatch);
    connect(mDestFileInfoTask.data(), &FileInfoGatherer::progress, this, [this](qint64 scanned, qint64 expected) {
        mScan.destFiles = scanned;
        mScan.destExpected = expected;
        emit scanStatusChanged();
    });
    connect(mDestFileInfoTask.data(), &FileInfoGatherer::finished, this, [this] {
        mScan.dest = false;
        emit scanStatusChanged();
        checkScansFinished();
    });
    emit scanStatusChanged();
    mDestFileInfoTask->run();
}

void Core::checkScansFinished()
{
    if (mScanFinishedEmitted || !mSourceFileInfoTask || !mDestFileInfoTask || mConfigPending
        || !mSourceFileInfoTask->isFinished() || !mDestFileInfoTask->isFinished())
        return;
    mScanFinishedEmitted = true;
    mScanFinishedOwed = true; // scanFinished means "settled": after the derivation and the config read
    if (mBackground) {
        startDerive();
    } else {
        deriveSelection();
        settleScans();
    }
}

// scanFinished is owed once both scans are done and is emitted exactly once, as soon as no configuration read and no
// derivation is pending (so scanning() is false when it fires), whichever of them finishes last.
void Core::settleScans()
{
    if (!mScanFinishedOwed || mConfigPending || mDeriving)
        return;
    mScanFinishedOwed = false;
    emit scanFinished();
}

void Core::deriveSelection()
{
    if (mBackground) {
        startDerive();
        return;
    }
    if (!mSelectionFollowsDestination || mExecutor)
        return;
    const auto done = [](const QPointer<FileInfoGatherer> &g) { return g && g->isFinished(); };
    QList<FileInfo> source, dest; // an empty destination path stays empty: nothing complete
    if (!mBackupDir.isEmpty()) {
        if (!done(mDestFileInfoTask) || (!mSourceDir.isEmpty() && !done(mSourceFileInfoTask)))
            return;
        dest = mDestFileInfoTask->infos();
        if (!mSourceDir.isEmpty())
            source = mSourceFileInfoTask->infos();
    }
    applyDerived(completeMics(mProfile, source, dest));
}

void Core::applyDerived(const MicCompleteness &complete)
{
    mInternalMicChange = true;
    forAllMicSignalGroupKeys(mProfile, [&](const MicSignalGroupKey &key) {
        if (mTouchedDuringDerive.contains(key))
            return; // the user changed it while this was being worked out: that wins
        const Completeness c = complete.value(key.mic).value(key.group);
        setMicEnabled(key, c != Completeness::Empty); // Partial stays wanted: Apply completes it
        if (!mProfile.mic(key.mic)->locked)
            setCompleteness(key, c);
    });
    mInternalMicChange = false;
    mTouchedDuringDerive.clear();
}

// completeMics walks every scanned file, so in background mode a worker does it on copies (implicitly shared lists)
// of the scans; the result is applied here only if no scan, profile or configuration change came in meanwhile.
void Core::startDerive()
{
    if (!mSelectionFollowsDestination || mExecutor) {
        mTouchedDuringDerive.clear();
        settleScans();
        return;
    }
    const auto done = [](const QPointer<FileInfoGatherer> &g) { return g && g->isFinished(); };
    QList<FileInfo> source, dest;
    if (!mBackupDir.isEmpty()) {
        if (!done(mDestFileInfoTask) || (!mSourceDir.isEmpty() && !done(mSourceFileInfoTask)))
            return; // the scans are not done: derived when they are
        dest = mDestFileInfoTask->infos();
        if (!mSourceDir.isEmpty())
            source = mSourceFileInfoTask->infos();
    }
    const quint64 token = ++mDeriveToken;
    mDeriving = true;
    emit scanStatusChanged();
    runAsync([profile = mProfile, source, dest, token, this]() -> std::function<void()> {
        const MicCompleteness complete = completeMics(profile, source, dest);
        return [this, complete, token] {
            if (token != mDeriveToken)
                return;
            mDeriving = false;
            applyDerived(complete);
            emit scanStatusChanged();
            settleScans();
        };
    });
}

Completeness Core::micCompleteness(const MicSignalGroupKey &key) const
{
    if (mSelectionFollowsDestination && mCompleteness.contains(key))
        return mCompleteness.value(key);
    return isMicEnabled(key) ? Completeness::Complete : Completeness::Empty;
}

void Core::setCompleteness(const MicSignalGroupKey &key, Completeness c)
{
    if (micCompleteness(key) == c)
        return;
    mCompleteness.insert(key, c);
    emit micCompletenessChanged(key, c);
}

void Core::setSelectionFollowsDestination(bool follow)
{
    mSelectionFollowsDestination = follow;
    deriveSelection();
}

void Core::setSourceDir(const QString &newSourceDir)
{
    if (mSourceDir == newSourceDir)
        return;

    mSourceDir = newSourceDir;
    emit sourceDirChanged(mSourceDir);
    const ProfileEntry *entry = suggestProfile(mProfiles, QFileInfo(mSourceDir).fileName());
    if (!mProfileChosen && !mExecutor && entry && entry->profile->id != mProfile.id)
        switchProfile(*entry->profile); // rescans
    else
        startSourceFileGatherer();
}

RepointResult Core::useLibrary(bool trimmed, const QString &propertiesFile) const
{
    RepointResult r;
    if (mSourceDir.isEmpty() || mBackupDir.isEmpty()) {
        r.error = tr("Choose the source and the destination folder first.");
        return r;
    }
    if (trimmed) { // the Spitfire app refuses a library folder with another name
        r.error = checkDestinationName(mSourceDir, mBackupDir);
        if (!r.error.isEmpty())
            return r;
    }
    return repointLibrary(propertiesFile, mProfile, trimmed ? mBackupDir : mSourceDir);
}

void Core::rescanSource()
{
    if (mSourceDir.isEmpty())
        return; // nothing to scan: and the skip flag must not wait around for an unrelated scan
    // Only this folder's scan is dropped; its old file count still gives the percentage a denominator.
    const QString dir = QDir::cleanPath(mSourceDir), cache = cacheFilePathFor(dir);
    if (!mExpectedFiles.contains(dir)) // not scanned in this session: the old count is still in the cache header
        if (const qint64 n = FileInfoGatherer::cachedFileCount(cache); n >= 0)
            mExpectedFiles[dir] = n;
    mSkipCacheOnce = true; // the scan must not read a cache file, even one a pending write is just finishing
    QFile::remove(cache);
    mSourceScans.remove(dir);
    mSourceScanOrder.removeAll(dir);
    startSourceFileGatherer();
}

ScanStatus Core::scanStatus() const
{
    ScanStatus s = mScan;
    s.preparing = mDeriving || mConfigPending;
    return s;
}

bool Core::scanning() const
{
    return scanRunning();
}

QString Core::destinationDirectoryPath() const
{
    return mBackupDir;
}

void Core::setDestinationDirectoryPath(const QString &newDestinationDir)
{
    if (mBackupDir == newDestinationDir) {
        reloadConfiguration(); // re-choosing the folder picks up a fixed config; no rescan needed
        return;
    }
    mBackupDir = newDestinationDir;
    emit destinationDirectoryPathChanged(mBackupDir);
    startDestFileGatherer(); // first: loading the configuration may derive from the scans
    reloadConfiguration();
}

bool Core::scanRunning() const
{
    const auto running = [](const QPointer<FileInfoGatherer> &g) { return g && !g->isFinished(); };
    return running(mSourceFileInfoTask) || running(mDestFileInfoTask) || mDeriving || mConfigPending;
}

// Everything a plan depends on, copied (the file lists are implicitly shared, so this is cheap) so that a worker thread
// can build it without touching Core.
struct Core::PlanSnapshot
{
    ProductProfile profile;
    QString sourceDir, backupDir;
    QList<FileInfo> source, dest;
    MicStatuses mics;
    QString configError;
    bool executorRunning = false, scanning = false, follow = false, anyMic = false, scansDone = false;
    quint64 generation = 0;
};

Core::PlanSnapshot Core::snapshot() const
{
    PlanSnapshot s;
    s.profile = mProfile;
    s.sourceDir = mSourceDir;
    s.backupDir = mBackupDir;
    s.mics = mConfiguration;
    s.configError = mConfigError;
    s.executorRunning = mExecutor;
    s.scanning = scanRunning();
    s.follow = mSelectionFollowsDestination;
    s.anyMic = anyMicEnabled();
    s.generation = mGeneration;
    if (mSourceFileInfoTask && mDestFileInfoTask && !s.scanning) {
        s.source = mSourceFileInfoTask->infos();
        s.dest = mDestFileInfoTask->infos();
        s.scansDone = true;
    }
    return s;
}

// Reads the file system (path checks, free space): on the GUI thread only through the synchronous buildPlan().
Plan Core::computePlan(const PlanSnapshot &snap)
{
    Plan refused;
    if (snap.executorRunning)
        refused.refusals << tr("An apply is running.");
    if (snap.scanning || !snap.scansDone)
        refused.refusals << tr("Please wait: scan not finished.");
    if (!snap.configError.isEmpty())
        refused.refusals << snap.configError;
    if (snap.follow && !snap.anyMic)
        refused.refusals << tr("Select at least one microphone.");
    const QString unsafe = checkSourceAndDestination(snap.sourceDir, snap.backupDir);
    if (!unsafe.isEmpty())
        refused.refusals << unsafe;
    else if (const QString seen = checkSourceLock(snap.sourceDir, snap.backupDir); !seen.isEmpty())
        refused.refusals << seen;
    if (const QString name = checkDestinationName(snap.sourceDir, snap.backupDir); !name.isEmpty())
        refused.refusals << name;
    refused.builtForSource = snap.sourceDir;
    refused.builtForDest = snap.backupDir;
    refused.generation = snap.generation;
    if (!refused.refusals.isEmpty())
        return refused; // a plan from a partial scan would be wrong, so no steps at all

    PlanInput in;
    in.profile = snap.profile;
    in.sourceRoot = snap.sourceDir;
    in.destRoot = snap.backupDir;
    in.source = snap.source;
    in.dest = snap.dest;
    in.mics = snap.mics;
    in.freeBytes = storageFor(snap.backupDir).bytesAvailable();
    Plan plan = smt::buildPlan(in);
    plan.generation = snap.generation;
    return plan;
}

Plan Core::buildPlan() const
{
    return computePlan(snapshot());
}

namespace {
PendingChange pendingChangeOf(const Plan &plan, bool scanning)
{
    PendingChange c;
    c.known = !scanning;
    c.refused = !plan.refusals.isEmpty();
    c.firstRefusal = c.refused ? plan.refusals.first() : QString();
    c.hasSteps = !plan.steps.isEmpty();
    c.any = plan.hasFileChanges();
    c.add = plan.bytesToAdd;
    c.remove = plan.bytesToFree;
    return c;
}
} // namespace

// A plan for the GUI: the path checks, the free-space query and the plan itself run on a worker thread. Only the latest
// request is answered, and only if no input changed meanwhile (the generation); a stale answer is dropped.
quint64 Core::requestPlan()
{
    const quint64 token = ++mPlanToken;
    runAsync([snap = snapshot(), token, this]() -> std::function<void()> {
        const Plan plan = computePlan(snap);
        return [this, plan, token] {
            if (token == mPlanToken && plan.generation == mGeneration)
                emit planReady(plan, token);
        };
    });
    return token;
}

quint64 Core::requestPendingChange()
{
    const quint64 token = ++mPendingToken;
    runAsync([snap = snapshot(), token, this]() -> std::function<void()> {
        const PendingChange change = pendingChangeOf(computePlan(snap), snap.scanning);
        return [this, change, token, generation = snap.generation] {
            if (token == mPendingToken && generation == mGeneration)
                emit pendingChangeReady(change, token);
        };
    });
    return token;
}

bool Core::startApply(const Plan &plan)
{
    if (mExecutor)
        return false;
    const auto refuse = [this](const QString &why) {
        QMetaObject::invokeMethod(this, [this, why] { emit applyFinished(false, why); }, Qt::QueuedConnection);
        return true;
    };
    // The executor checks source and destination before it writes anything (its start() refuses an overlap first).
    if (plan.builtForSource != mSourceDir || plan.builtForDest != mBackupDir || plan.generation != mGeneration
        || scanRunning())
        return refuse(tr("The plan is out of date; build it again."));
    if (!plan.canRun())
        return refuse(plan.refusals.join('\n'));
    if (!mConfigError.isEmpty())
        return refuse(mConfigError);
    ++mGeneration;

    mExecutor = new PlanExecutor(mSourceDir, mBackupDir, this);
    connect(mExecutor.data(), &PlanExecutor::stepChanged, this, &Core::applyStepChanged);
    connect(mExecutor.data(), &PlanExecutor::bytesProgress, this, &Core::applyProgress);
    connect(mExecutor.data(), &PlanExecutor::finished, this, [this](bool success, const QString &message) {
        mExecutor->deleteLater();
        mExecutor.clear();
        ++mGeneration;
        startDestFileGatherer(); // before applyFinished, so listeners see the rescan running
        emit applyFinished(success, message);
    });
    // The config is written only once the executor has passed all its checks and holds the lock,
    // on its worker thread: so it gets copies, never Core state.
    mExecutor->start(plan, [dest = mBackupDir, profile = mProfile, mics = mConfiguration](QString *error) {
        QString why;
        if (saveProjectConfig(dest, profile, mics, &why))
            return true;
        *error = tr("Could not save the configuration to %1: %2").arg(dest, why);
        return false;
    }); // a fresh executor is never busy
    return true;
}

void Core::cancelApply()
{
    if (mExecutor)
        mExecutor->cancel();
}

bool Core::saveConfiguration(QString *error) const
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (!mConfigError.isEmpty())
        return fail(mConfigError);
    if (mBackupDir.isEmpty() || !QFileInfo(mBackupDir).isDir())
        return fail(tr("The destination folder does not exist."));
    // A destination inside a missing source is impossible, so only check an existing source.
    if (QFileInfo(mSourceDir).isDir()) {
        const QString unsafe = checkSourceAndDestination(mSourceDir, mBackupDir);
        if (!unsafe.isEmpty())
            return fail(unsafe);
    }
    return saveProjectConfig(mBackupDir, mProfile, mConfiguration, error);
}

namespace {
struct ConfigRead
{
    std::optional<MicStatuses> loaded;
    QString error;
};
ConfigRead readConfig(const QString &dest, const ProductProfile &profile) // file IO
{
    ConfigRead r;
    r.loaded = dest.isEmpty() ? MicStatuses{} : loadProjectConfig(dest, profile, &r.error);
    return r;
}
} // namespace

void Core::loadConfiguration()
{
    ++mConfigToken; // an asynchronous read still on its way is stale
    mConfigPending = false;
    const ConfigRead r = readConfig(mBackupDir, mProfile);
    applyConfiguration(r.loaded.has_value(), r.loaded.value_or(MicStatuses{}), r.error);
}

// Background mode: project.smt is read on a worker (it can sit on a slow mount); the scans' end waits for it.
void Core::reloadConfiguration()
{
    if (!mBackground) {
        loadConfiguration();
        return;
    }
    const quint64 token = ++mConfigToken;
    ++mDeriveToken; // a derivation still out was made from the old configuration
    mDeriving = false;
    mConfigPending = true;
    emit scanStatusChanged();
    runAsync([dest = mBackupDir, profile = mProfile, token, this]() -> std::function<void()> {
        const ConfigRead r = readConfig(dest, profile);
        return [this, token, r] {
            if (token != mConfigToken)
                return;
            mConfigPending = false;
            applyConfiguration(r.loaded.has_value(), r.loaded.value_or(MicStatuses{}), r.error);
            emit scanStatusChanged();
            checkScansFinished();
            settleScans(); // the derivation of the new configuration may have had nothing to do
        };
    });
}

void Core::applyConfiguration(bool ok, const MicStatuses &stored, const QString &error)
{
    mCompleteness.clear(); // the derive below sets it again
    ++mGeneration;
    mConfigError = ok ? QString()
                      : tr("Could not use %1/project.smt - %2. Fix or remove the file; it will not be "
                           "overwritten.").arg(mBackupDir, error);
    // Following the destination, the stored mics are ignored: nothing is checked until derived.
    MicStatuses mics = stored;
    if (mSelectionFollowsDestination) // all off
        forAllMicSignalGroupKeys(mProfile, [&](const MicSignalGroupKey &key) { mics[key.mic][key.group] = false; });
    mInternalMicChange = true;
    forAllMicSignalGroupKeys(mProfile, [&](const MicSignalGroupKey &key) {
        if (!mTouchedDuringDerive.contains(key)) // ticked by the user while this was being read: that wins
            setMicEnabled(key, micEnabled(mProfile, mics, key));
    });
    mInternalMicChange = false;
    deriveSelection();
    if (!mSelectionFollowsDestination || !mBackground)
        mTouchedDuringDerive.clear(); // no derivation follows to consume them
    if (!mConfigError.isEmpty())
        emit configurationError(mConfigError);
    else
        emit configurationLoaded();
}

void Core::applyQuickSet(int index)
{
    if (index < 0 || index >= mProfile.quickSets.size())
        return;
    const QStringList &families = mProfile.quickSets.at(index).families;
    forAllMicSignalGroupKeys(mProfile, [&](const MicSignalGroupKey &key) {
        setMicEnabled(key, families.contains(mProfile.mic(key.mic)->family));
    });
}

const ProductProfile &Core::profile() const
{
    return mProfile;
}

const QList<ProfileEntry> &Core::availableProfiles() const
{
    return mProfiles;
}

QString Core::profileSuggestion() const
{
    if (mSourceDir.isEmpty())
        return {};
    const ProfileEntry *e = suggestProfile(mProfiles, QFileInfo(mSourceDir).fileName());
    return e ? e->id : QString();
}

void Core::setProfile(const ProductProfile &profile)
{
    if (mExecutor)
        return;
    mProfileChosen = true;
    switchProfile(profile);
}

bool Core::setProfileById(const QString &id)
{
    if (mExecutor)
        return false;
    for (const auto &e : mProfiles)
        if (e.id == id && e.valid()) {
            setProfile(*e.profile);
            return true;
        }
    return false;
}

void Core::switchProfile(const ProductProfile &profile)
{
    mProfile = profile;
    emit profileChanged();
    startSourceFileGatherer(); // first, like the destination: loading the configuration may derive from the scans
    reloadConfiguration();
}

QString Core::configurationFilePath() const
{
    return projectConfigPath(mBackupDir);
}

QList<FileInfo> Core::sourceFileInfos() const
{
    return mSourceFileInfoTask && mSourceFileInfoTask->isFinished() ? mSourceFileInfoTask->infos() : QList<FileInfo>{};
}

QString Core::applicationDataDir() const
{
    return mApplicationDataDir;
}

// The destination volume's free space: a query that can block on a slow mount, so a worker answers it in background
// mode (the figure stays unknown until then).
void Core::refreshFreeSpace()
{
    const QString dest = mBackupDir;
    const quint64 token = ++mFreeToken;
    if (!mBackground) {
        mAvailable = storageFor(dest).bytesAvailable();
        recalculateRemainingSpace();
        return;
    }
    mAvailable = -1;
    runAsync([dest, token, this]() -> std::function<void()> {
        const qint64 available = storageFor(dest).bytesAvailable();
        return [this, token, available] {
            if (token != mFreeToken)
                return;
            mAvailable = available;
            recalculateRemainingSpace();
        };
    });
}

void Core::runAsync(std::function<std::function<void()>()> job)
{
    QThreadPool::globalInstance()->start([relay = mRelay, job = std::move(job)] {
        std::function<void()> completion = job();
        emit relay->post(std::move(completion));
    });
}

void Core::setBackgroundWork(bool on)
{
    mBackground = on;
}

void Core::recalculateRemainingSpace()
{
    // Free space plus what the destination already holds, minus what the selection needs.
    if (mAvailable < 0)
        return;
    emit remainingSpaceChanged(mAvailable + qint64(mTotalSpaceUsed) - qint64(totalSpaceForSelectedItems()));
}

void Core::setApplicationDataDir(const QString &newApplicationDataDir)
{
    if (mApplicationDataDir == newApplicationDataDir)
        return;
    mApplicationDataDir = newApplicationDataDir;
    emit applicationDataDirChanged(mApplicationDataDir);
}

PendingChange Core::pendingChange() const
{
    return pendingChangeOf(buildPlan(), scanRunning());
}

bool Core::hasSourceContent() const
{
    if (mSourceDir.isEmpty())
        return false;
    return !(mSourceFileInfoTask && mSourceFileInfoTask->isFinished() && mSourceFileInfoTask->infos().isEmpty());
}

size_t Core::spaceRequired(const MicSignalGroupKey &key) const
{
    return mSpaceRequired.value(key, 0);
}

size_t Core::totalSpaceForSelectedItems() const
{
    size_t result = mSizeOfLibraryFiles;
    forAllMicSignalGroupKeys(mProfile, [&](const MicSignalGroupKey &key) {
        if (isMicEnabled(key))
            result += mSpaceRequired.value(key, 0);
    });
    return result;
}

bool Core::isMicEnabled(const MicSignalGroupKey &key) const
{
    return micEnabled(mProfile, mConfiguration, key);
}

bool Core::anyMicEnabled() const
{
    bool any = false;
    forAllMicSignalGroupKeys(mProfile, [&](const MicSignalGroupKey &key) { any = any || isMicEnabled(key); });
    return any;
}

void Core::setMicEnabled(const MicSignalGroupKey &key, bool enabled)
{
    const MicDef *mic = mProfile.mic(key.mic);
    if (!mic || mic->locked || !mProfile.group(key.group) || isMicEnabled(key) == enabled)
        return;

    if ((mDeriving || mConfigPending) && !mInternalMicChange)
        mTouchedDuringDerive.insert(key); // a click during the configuration read or the derivation sticks
    mConfiguration[key.mic].insert(key.group, enabled);
    ++mGeneration;
    mCompleteness.insert(key, enabled ? Completeness::Complete : Completeness::Empty); // a plain box again
    emit micEnabledChanged(key, enabled);
    emit micCompletenessChanged(key, mCompleteness.value(key));
    emit totalSpaceRequiredChanged(totalSpaceForSelectedItems());
    recalculateRemainingSpace();
}

} // namespace smt
