#ifndef _smt_corelib_h_
#define _smt_corelib_h_

#include <QHash>
#include <QSet>
#include <QObject>
#include <QPointer>
#include <QStorageInfo>
#include <QString>
#include "completeness.h"
#include "coreutil.h"
#include "plan.h"
#include "profileregistry.h"
#include "smt.h"
#include "spitfiresettings.h"
#include "workrelay.h"
#include <functional>
#include <memory>

namespace smt {

inline const QString kApplicationName = "Sample Mic Trimmer";
inline const QString kOrganizationName = "name.mbishop";

class FileInfoGatherer;
class PlanExecutor;

class Core : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString sourceDir READ sourceDir WRITE setSourceDir NOTIFY sourceDirChanged FINAL)
    Q_PROPERTY(QString destinationDirectoryPath READ destinationDirectoryPath WRITE
                   setDestinationDirectoryPath NOTIFY destinationDirectoryPathChanged FINAL)
    Q_PROPERTY(QString applicationDataDir READ applicationDataDir WRITE setApplicationDataDir NOTIFY
                   applicationDataDirChanged FINAL)
public:
    explicit Core(QObject *parent = nullptr);

    QString sourceDir() const;
    void setSourceDir(const QString &newSourceDir); // cancels the old scan and rescans

    QString destinationDirectoryPath() const;
    // Cancels the old destination scan, loads <dest>/project.smt and rescans.
    void setDestinationDirectoryPath(const QString &newDestinationDir);

    void rescanSource(); // drops the cached source scan first

    // Points the current profile's entries in Spitfire.properties at the trimmed copy (or back at the source),
    // whatever they pointed at before; only that file is rewritten, no library file is touched.
    RepointResult useLibrary(bool trimmed, const QString &propertiesFile = defaultSpitfireProperties()) const;

    // Reads <dest>/project.smt; a missing file means all enabled. A corrupt one emits
    // configurationError, leaves defaults in memory and blocks saveConfiguration().
    void loadConfiguration();
    QString configurationFilePath() const; // <dest>/project.smt, which holds the mic selection
    // Writes <dest>/project.smt. False if the destination does not exist (never creates
    // it), overlaps the source, or its config file could not be read.
    bool saveConfiguration(QString *error = nullptr) const;

    // Plans from the current scans and selection. buildPlan() is synchronous (CLI, tests): it reads the file system
    // (path checks, free space). The GUI uses the requests below, which do all of that on a worker thread.
    Plan buildPlan() const;
    // Background work (GUI): free-space queries, plans, the completeness derivation and the destination's
    // project.smt read run on pool threads and report by signal. Off by default (CLI and tests are synchronous).
    void setBackgroundWork(bool on);
    // Each returns a token; only the latest request of a kind is delivered, and only if nothing the plan depends on
    // changed in between (stale results are dropped; the caller asks again when its inputs change).
    quint64 requestPlan();
    quint64 requestPendingChange();
    // False, emitting nothing, while an apply runs; otherwise true and always ends with applyFinished().
    bool startApply(const Plan &plan);
    void cancelApply();

    QString applicationDataDir() const; // $SMT_APP_DATA_DIR if set (tests), else AppLocalDataLocation
    QList<FileInfo> sourceFileInfos() const; // the finished source scan; empty before

    size_t totalSpaceForSelectedItems() const;
    // What Apply would do now, from buildPlan(): the sizes to copy and delete, whether any step exists and the first
    // refusal. `known` is false only while a scan runs.
    PendingChange pendingChange() const;
    // False without a source folder, or once its scan has finished and found no file at all.
    bool hasSourceContent() const;
    // Which scans run and how far (for the status text; also signalled by scanStatusChanged()).
    ScanStatus scanStatus() const;
    bool scanning() const; // a source or destination scan is running

    size_t spaceRequired(const MicSignalGroupKey &key) const;

    bool isMicEnabled(const MicSignalGroupKey &key) const;
    // Ignored for a locked mic or a key the profile does not know.
    void setMicEnabled(const MicSignalGroupKey &key, bool enabled);
    bool anyMicEnabled() const; // some {mic, group} of the profile's listed mics is enabled (locked counts)

    // Acts on every group via setMicEnabled (signals fire); does not save. Locked mics stay on.
    void applyQuickSet(int index); // index into profile().quickSets; out of range is ignored

    // GUI mode (default off): once source and destination scans are done, the selection is
    // re-derived from what is complete in the destination (completeMics), ignoring the mics stored
    // in project.smt; empty destination = nothing but locked mics. Never while an apply runs.
    // A partly present key (Partial) is enabled, so Apply completes it and deletes nothing; only
    // the user unticking it removes it. Any setMicEnabled() change resets the key to Complete/Empty.
    void setSelectionFollowsDestination(bool follow);
    // Not tracked without follow mode: Complete for an enabled key, else Empty.
    Completeness micCompleteness(const MicSignalGroupKey &key) const;

    const ProductProfile &profile() const; // the built-in BBCSO profile until set
    // Reclassifies the source (rescan, cheap from the cache) and reloads <dest>/project.smt.
    // Both do nothing while an apply runs, and mark the profile as chosen (no more automatic suggestion).
    void setProfile(const ProductProfile &profile);
    bool setProfileById(const QString &id); // false (nothing changed) for an unknown or invalid id
    const QList<ProfileEntry> &availableProfiles() const;
    QString profileSuggestion() const; // id matching the source folder name, empty if none

signals:
    void profileChanged();
    void micEnabledChanged(smt::MicSignalGroupKey key, bool enabled);
    void micCompletenessChanged(smt::MicSignalGroupKey key, smt::Completeness completeness);
    void sourceDirChanged(const QString &);
    void destinationDirectoryPathChanged(QString destinationDir);
    void applicationDataDirChanged(QString applicationDataDir);
    void configurationError(const QString &message);
    void configurationLoaded(); // loadConfiguration() succeeded (or there was no file)

    void totalSpaceRequiredChanged(size_t totalSpaceRequired);
    void remainingSpaceChanged(qint64 remaining); // negative: that much is missing
    void spaceRequiredChanged(smt::MicSignalGroupKey key, size_t totalSpaceRequired);

    void planReady(const smt::Plan &plan, quint64 token);
    void pendingChangeReady(const PendingChange &change, quint64 token);
    void scanStatusChanged(); // a scan (or the work after it) started, progressed or finished
    void scanFinished(); // once both scans are done; again after each rescan
    void sourceScanFinished(); // the source scan alone (no destination needed)

    void applyStepChanged(int index, smt::PlanStep::Status status, const QString &error);
    void applyProgress(qint64 done, qint64 total, double bytesPerSecond);
    void applyFinished(bool success, const QString &message);

private:
    void switchProfile(const ProductProfile &profile);
    void recalculateRemainingSpace();
    void setApplicationDataDir(const QString &newApplicationDataDir);
    void startSourceFileGatherer();
    void startDestFileGatherer();
    void checkScansFinished();
    void deriveSelection();
    void startDerive(); // background mode: completeMics on a worker
    void settleScans(); // emits the scanFinished the scans owe, once nothing is pending any more
    void applyDerived(const MicCompleteness &complete);
    void applyConfiguration(bool ok, const MicStatuses &stored, const QString &error);
    void reloadConfiguration(); // loadConfiguration(), on a worker in background mode
    void refreshFreeSpace();
    void runAsync(std::function<std::function<void()>()> job); // job on a pool thread; its result runs here
    struct PlanSnapshot;
    PlanSnapshot snapshot() const;
    static Plan computePlan(const PlanSnapshot &s);
    void setCompleteness(const MicSignalGroupKey &key, Completeness c);
    bool scanRunning() const;
    QString cacheFilePathFor(const QString &sourceDir) const; // <app data>/ScanCache/<sha1 of the folder>.smtcache
    void sourceBatch(const QList<FileInfo> &files);
    void destBatch(const QList<FileInfo> &files);

    QPointer<FileInfoGatherer> mSourceFileInfoTask;
    QPointer<FileInfoGatherer> mDestFileInfoTask;
    bool mScanFinishedEmitted = false;
    bool mScanFinishedOwed = false; // both scans are done: scanFinished follows once the derivation and config read settled
    bool mSkipCacheOnce = false;    // the next source scan ignores the disk cache (Rescan source)
    bool mInternalMicChange = false; // setMicEnabled calls made by applying a derivation or a loaded configuration
    QSet<MicSignalGroupKey> mTouchedDuringDerive; // keys the user changed while a derivation ran: they win over it
    bool mSelectionFollowsDestination = false;
    QPointer<PlanExecutor> mExecutor;
    QString mSourceDir;
    QString mBackupDir;
    qint64 mAvailable = -1; // free bytes on the destination's volume, -1 unknown
    bool mBackground = false;
    std::shared_ptr<WorkRelay> mRelay;
    quint64 mPlanToken = 0, mPendingToken = 0, mFreeToken = 0, mDeriveToken = 0, mConfigToken = 0;
    bool mDeriving = false, mConfigPending = false;
    ProductProfile mProfile;
    QList<ProfileEntry> mProfiles;
    bool mProfileChosen = false;
    MicStatuses mConfiguration;
    QHash<MicSignalGroupKey, Completeness> mCompleteness; // follow mode only
    QString mConfigError; // non-empty while <dest>/project.smt is unreadable
    quint64 mGeneration = 0; // bumped whenever an input of the plan changes
    QString mApplicationDataDir;
    QHash<MicSignalGroupKey, size_t> mSpaceRequired;
    size_t mTotalSpaceUsed = 0;
    size_t mSizeOfLibraryFiles = 0;
    QHash<QString, QList<FileInfo>> mSourceScans; // finished source scans of this session: switching back is instant
    QStringList mSourceScanOrder; // most recently used first, at most 3
    QHash<QString, qint64> mExpectedFiles;        // per source folder: its last file count (the percentage's denominator)
    ScanStatus mScan;
};
}; // namespace smt

#endif // _smt_corelib_h_
