#include "planexecutor.h"
#include "fsutil.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QThread>
#include <QTimer>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <mutex>
#include "copier.h"
#include "coreutil.h"
#include "librarydb.h"
#include "pathsafety.h"

namespace smt {

namespace {

constexpr int kBatchFiles = 16;
const QString kStaging = QStringLiteral(".smt-staging");

// QFile::rename refuses to overwrite, and remove+rename leaves a moment with no file;
// fs::replaceFile (rename(2) on Unix) replaces the target atomically.
bool renameOver(const QString &from, const QString &to, QString *error)
{
    QString why;
    if (fs::replaceFile(from, to, &why))
        return true;
    *error = QStringLiteral("cannot move %1 to %2: %3").arg(from, to, why);
    return false;
}

// fsync, so the renamed DB is never an empty or partial file after a crash.
bool syncToDisk(const QString &path, QString *error)
{
    QString why;
    if (fs::syncFile(path, &why))
        return true;
    *error = QStringLiteral("%1: cannot sync to disk: %2").arg(path, why);
    return false;
}

// root + "/" + rel, or "" with *error set if it could land outside root (defence in depth:
// plans come from scans, but this code deletes and replaces files).
QString underRoot(const QString &canonicalRoot, const QString &rel, QString *error)
{
    if (rel.isEmpty() || QDir::isAbsolutePath(rel) || rel.split('/').contains(QStringLiteral(".."))) {
        *error = QStringLiteral("refusing unsafe path: ") + rel;
        return {};
    }
    const QString path = canonicalRoot + '/' + rel;
    QString dir = QFileInfo(path).path();
    while (!QFileInfo::exists(dir))
        dir = QFileInfo(dir).path();
    const QString resolved = QFileInfo(dir).canonicalFilePath(); // catches symlinked folders
    if (resolved != canonicalRoot && !resolved.startsWith(canonicalRoot + '/')) {
        *error = QStringLiteral("refusing path outside the destination: ") + rel;
        return {};
    }
    return path;
}

// <root>/.smt-staging is absent or a real folder at exactly that path. Never follow a
// link there: removeRecursively would empty the link's target (possibly the source).
bool stagingIsPlain(const QString &canonicalRoot)
{
    const QFileInfo fi(canonicalRoot + '/' + kStaging);
    if (fi.isSymLink())
        return false;
    return !fi.exists() || fi.canonicalFilePath() == canonicalRoot + '/' + kStaging;
}

} // namespace

PlanExecutor::PlanExecutor(QString sourceRoot, QString destRoot, QObject *parent)
    : QObject(parent), mSource(std::move(sourceRoot)), mDest(std::move(destRoot))
{
}

PlanExecutor::~PlanExecutor()
{
    mCancel = true;
    if (mThread) {
        mThread->wait();
        delete mThread;
    }
}

bool PlanExecutor::start(Plan plan, std::function<bool(QString *error)> prepare)
{
    if (mBusy)
        return false;
    if (mThread) { // finished, but may still be unwinding
        mThread->wait();
        delete mThread;
        mThread = nullptr;
    }
    mLock.reset();
    mCreatedDirs.clear();
    // Refuse before touching the disk.
    if (!plan.canRun())
        return refuse(plan.refusals.join('\n'));
    const QString unsafe = checkSourceAndDestination(mSource, mDest);
    if (!unsafe.isEmpty())
        return refuse(unsafe);
    // Remember which folders mkpath creates, deepest first, to remove them again on a refusal.
    for (QString d = QDir::cleanPath(QFileInfo(mDest).absoluteFilePath()); !QFileInfo::exists(d);
         d = QFileInfo(d).path())
        mCreatedDirs << d;
    if (!QDir().mkpath(mDest)) {
        removeCreatedDirs();
        return refuse(QStringLiteral("Cannot create the destination folder %1.").arg(mDest));
    }
    mDestCanonical = QFileInfo(mDest).canonicalFilePath();
    if (!stagingIsPlain(mDestCanonical)) {
        removeCreatedDirs();
        return refuse(QStringLiteral("%1/%2 is a link or not a plain folder; remove it and try again.")
                          .arg(mDestCanonical, kStaging));
    }
    auto lock = std::make_unique<QLockFile>(mDestCanonical + QStringLiteral("/.smt.lock"));
    lock->setStaleLockTime(0); // stale only if the holder process is gone, never by age
    if (!lock->tryLock()) {
        removeCreatedDirs();
        return refuse(lock->error() == QLockFile::LockFailedError
                          ? QStringLiteral("Another apply is running on this destination (lock file %1); wait for "
                                           "it to finish. Remove the lock file manually only if no other apply is "
                                           "running, on this or any other computer.")
                                .arg(lock->fileName())
                          : QStringLiteral("Cannot create the lock file %1.").arg(lock->fileName()));
    }
    // Our lock seen from the source means the destination is the source (or a folder in it) by a
    // path that the checks above cannot tell apart (e.g. a second SMB mount of the same share).
    if (const QString seen = checkSourceLock(mSource, mDestCanonical); !seen.isEmpty()) {
        lock->unlock();
        removeCreatedDirs();
        return refuse(seen);
    }
    mLock = std::move(lock);
    mCancel = false;
    mBusy = true;
    mThread = QThread::create([this, steps = plan.steps, prepare = std::move(prepare)] {
        QString error;
        bool prepared = true;
        if (prepare) {
            try {
                prepared = prepare(&error);
            } catch (const std::exception &e) {
                prepared = false;
                error = QStringLiteral("Preparing the apply failed: %1").arg(QString::fromLocal8Bit(e.what()));
            } catch (...) {
                prepared = false;
                error = QStringLiteral("Preparing the apply failed.");
            }
        }
        if (!prepared) { // nothing else was written yet: undo the lock and folders
            mLock->unlock();
            removeCreatedDirs();
            mBusy = false;
            emit finished(false, error);
            return;
        }
        run(steps);
    });
    mThread->start();
    return true;
}

// Only empty folders go (rmdir fails on others), and only those this start() created.
void PlanExecutor::removeCreatedDirs()
{
    for (const QString &d : std::as_const(mCreatedDirs))
        QDir().rmdir(d);
    mCreatedDirs.clear();
}

void PlanExecutor::cancel()
{
    mCancel = true;
}

bool PlanExecutor::refuse(const QString &reason)
{
    QTimer::singleShot(0, this, [this, reason] { emit finished(false, reason); });
    return true;
}

void PlanExecutor::run(QList<PlanStep> steps)
{
    using Type = PlanStep::Type;
    using Status = PlanStep::Status;
    using Clock = std::chrono::steady_clock;

    const QString &root = mDestCanonical;
    const QString staging = root + '/' + kStaging;
    qint64 total = 0, freed = 0;
    for (const auto &s : steps) {
        total += s.type == Type::CopyToStaging ? s.bytes : 0;
        freed += s.type == Type::DeleteFile ? s.bytes : 0;
    }

    // bytesProgress: at most ~10/s from the copier's threads, plus a final one.
    std::atomic<qint64> done{0};
    std::mutex progressMutex;
    auto lastEmit = Clock::now();
    qint64 lastDone = 0;
    double rate = 0;
    auto report = [&](bool final) {
        std::unique_lock lock(progressMutex, std::defer_lock);
        if (final)
            lock.lock();
        else if (!lock.try_lock())
            return;
        const auto now = Clock::now();
        const double dt = std::chrono::duration<double>(now - lastEmit).count();
        if (!final && dt < 0.1)
            return;
        const qint64 d = done.load();
        if (dt > 0) {
            const double inst = double(d - lastDone) / dt;
            rate = rate == 0 ? inst : 0.7 * rate + 0.3 * inst; // rolling estimate
        }
        lastEmit = now;
        lastDone = d;
        emit bytesProgress(d, total, rate);
    };

    auto setStatus = [&](int i, Status s, const QString &error = {}) {
        steps[i].status = s;
        steps[i].error = error;
        emit stepChanged(i, s, error);
    };
    auto finish = [&](bool ok, const QString &message) {
        if (total > 0)
            report(true);
        if (ok && stagingIsPlain(root)) // re-checked: it may have changed during the run
            QDir(staging).removeRecursively();
        mLock->unlock();
        mBusy = false;
        emit finished(ok, message);
    };
    const QString cancelled = QStringLiteral("Cancelled. The library is still consistent: every database lists "
                                             "only files that are present. Run Apply again to resume.");
    auto fail = [&](int i, const QString &error) {
        setStatus(i, Status::Failed, error);
        finish(false, QStringLiteral("Step %1 failed: %2\n%3\nThe library is still consistent: every database "
                                     "lists only files that are present. Run Apply again to resume.")
                          .arg(i + 1)
                          .arg(steps[i].describe(), error));
    };
    auto stagedComplete = [&](int i, const QString &stagedPath) {
        const QFileInfo fi(stagedPath);
        return fi.exists() && fi.size() == steps[i].bytes;
    };

    for (int i = 0; i < steps.size();) {
        if (mCancel)
            return finish(false, cancelled);
        const PlanStep &step = steps[i];
        QString err;

        if (step.type == Type::CopyToStaging) {
            int end = i;
            QList<CopyJob> jobs;
            for (; end < steps.size() && end - i < kBatchFiles && steps[end].type == Type::CopyToStaging; ++end) {
                const QString to = underRoot(root, kStaging + '/' + steps[end].relPath, &err);
                if (to.isEmpty())
                    return fail(end, err);
                jobs << CopyJob{mSource + '/' + steps[end].relPath, to, steps[end].bytes, steps[end].modified, steps[end].refresh};
            }
            for (int k = i; k < end; ++k)
                setStatus(k, Status::Running);
            const bool ok = copyFiles(
                jobs, CopyOptions{}, [&](qint64 delta) { done += delta; report(false); }, mCancel, &err);
            int failed = -1;
            if (ok) {
                for (int k = i; k < end && failed < 0; ++k)
                    if (!stagedComplete(k, jobs[k - i].dest)) {
                        failed = k;
                        err = QStringLiteral("%1: staged size %2, expected %3")
                                  .arg(jobs[k - i].dest)
                                  .arg(QFileInfo(jobs[k - i].dest).size())
                                  .arg(steps[k].bytes);
                    }
            } else if (!mCancel) {
                for (int k = i; k < end && failed < 0; ++k)
                    if (err.startsWith(jobs[k - i].source + ':') || err.startsWith(jobs[k - i].dest + ':')
                        || err.startsWith(jobs[k - i].dest + QStringLiteral(".part:")))
                        failed = k;
                for (int k = i; k < end && failed < 0; ++k)
                    if (!stagedComplete(k, jobs[k - i].dest))
                        failed = k;
                if (failed < 0)
                    failed = i;
            }
            for (int k = i; k < end; ++k)
                if (k != failed)
                    setStatus(k, stagedComplete(k, jobs[k - i].dest) ? Status::Done : Status::Pending);
            if (failed >= 0)
                return fail(failed, err);
            if (!ok)
                return finish(false, cancelled);
            i = end;
            continue;
        }

        setStatus(i, Status::Running);
        const QString target = underRoot(root, step.relPath, &err);
        if (target.isEmpty())
            return fail(i, err);
        switch (step.type) {
        case Type::DeleteFile: {
            QFile f(target);
            const QFileInfo fi(target);
            if ((fi.exists() || fi.isSymLink()) && !f.remove()) // already gone counts as done
                return fail(i, QStringLiteral("%1: %2").arg(target, f.errorString()));
            break;
        }
        case Type::MoveIntoPlace: {
            const QString staged = underRoot(root, kStaging + '/' + step.relPath, &err);
            if (staged.isEmpty())
                return fail(i, err);
            const QFileInfo fi(target);
            if (!step.refresh && fi.exists() && fi.size() == step.bytes)
                break; // already in place (a settings file is replaced whatever it holds)
            if (!QFileInfo::exists(staged))
                return fail(i, QStringLiteral("%1: staged file is missing").arg(staged));
            if (!QDir().mkpath(fi.path()))
                return fail(i, QStringLiteral("%1: cannot create directory").arg(fi.path()));
            if (!renameOver(staged, target, &err))
                return fail(i, err);
            break;
        }
        case Type::ReplaceDb: {
            const QString staged = underRoot(root, kStaging + '/' + step.relPath, &err);
            if (staged.isEmpty())
                return fail(i, err);
            for (const QString &dir : {QFileInfo(staged).path(), QFileInfo(target).path()})
                if (!QDir().mkpath(dir))
                    return fail(i, QStringLiteral("%1: cannot create directory").arg(dir));
            // Derived from the files actually present: one may have gone since the plan was built.
            QSet<QString> keep;
            for (const QString &name : QDir(QFileInfo(target).path()).entryList(QDir::Files | QDir::Hidden))
                if (step.keepFileNames.contains(name))
                    keep.insert(name);
            if (!librarydb::writePatchedCopy(mSource + '/' + step.relPath, staged, keep, &err))
                return fail(i, err);
            const QStringList listed = librarydb::containerNames(staged, &err);
            if (!err.isEmpty())
                return fail(i, err);
            for (const QString &name : listed)
                if (!keep.contains(name)) {
                    QFile::remove(staged);
                    return fail(i, QStringLiteral("%1: the patched database still lists %2, which will not be in "
                                                  "the destination").arg(staged, name));
                }
            if (!syncToDisk(staged, &err) || !renameOver(staged, target, &err))
                return fail(i, err);
            break;
        }
        case Type::CopyToStaging:
            break; // handled above
        }
        setStatus(i, Status::Done);
        ++i;
    }
    finish(true, QStringLiteral("Applied %1 steps: copied %2, freed %3.")
                     .arg(steps.size())
                     .arg(formatByteSize(size_t(total)), formatByteSize(size_t(freed))));
}

} // namespace smt
