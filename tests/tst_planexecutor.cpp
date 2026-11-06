#include <QtTest>
#include "unixonly.h"
#include <QCryptographicHash>
#include <QMutex>
#include <QSemaphore>
#include <stdexcept>
#include "fakelibrary.h"
#include "librarydb.h"
#include "plan.h"
#include "pathsafety.h"
#include "pathsafety_testing.h"
#include "planexecutor.h"

using namespace smt;
using T = PlanStep::Type;
using S = PlanStep::Status;

static const QStringList kTokens{"TR01", "MO01", "CL01"};
static const QStringList kSections{"brass", "strings", "woodwinds"};
static const int kInstruments = 4;
static const qint64 kFileSize = 1000;

static MicStatuses micsOn(const QStringList &tokens)
{
    MicStatuses m;
    forAllMicSignalGroupKeys(builtInProfile(kBbcsoProfileId), [&](const MicSignalGroupKey &k) {
        m[k.mic][k.group] = tokens.contains(k.mic.toUpper());
    });
    return m;
}

static Plan makePlan(const QString &src, const QString &dst, const QStringList &tokens)
{
    PlanInput in;
    in.profile = builtInProfile(kBbcsoProfileId);
    in.sourceRoot = src;
    in.destRoot = dst;
    in.source = scanTree(src);
    in.dest = QFileInfo::exists(dst) ? scanTree(dst) : QList<FileInfo>{};
    in.mics = micsOn(tokens);
    in.freeBytes = qint64(1) << 40;
    return buildPlan(in);
}

// Every entry under root with size and mtime: equal before and after means nothing was written.
static QStringList snapshotOf(const QString &root)
{
    QStringList out;
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo fi = it.nextFileInfo();
        out << QString("%1 %2 %3").arg(QDir(root).relativeFilePath(fi.filePath()))
                   .arg(fi.isDir() ? -1 : fi.size())
                   .arg(snapshotMtime(fi));
    }
    out.sort();
    return out;
}

static int firstStep(const Plan &p, T type)
{
    for (int i = 0; i < p.steps.size(); ++i)
        if (p.steps[i].type == type)
            return i;
    return -1;
}

static int countSteps(const Plan &p, T type)
{
    int n = 0;
    for (const auto &s : p.steps)
        n += s.type == type;
    return n;
}

struct Outcome
{
    bool finished = false, ok = false;
    QString message;
};

static Outcome runPlan(PlanExecutor &ex, const Plan &plan)
{
    QSignalSpy spy(&ex, &PlanExecutor::finished);
    ex.start(plan);
    Outcome o;
    o.finished = spy.count() > 0 || spy.wait(30000);
    if (o.finished) {
        o.ok = spy.at(0).at(0).toBool();
        o.message = spy.at(0).at(1).toString();
    }
    return o;
}

static Outcome apply(const QString &src, const QString &dst, const QStringList &tokens)
{
    PlanExecutor ex(src, dst);
    return runPlan(ex, makePlan(src, dst, tokens));
}

// Records step statuses on the worker thread (DirectConnection). `hook` also runs there,
// so it must not use QVERIFY; it collects problems into `problems` instead.
struct Recorder
{
    QMutex mutex;
    QHash<int, S> last;
    int done = 0;
    QStringList problems;
    std::function<void(int, S)> hook;

    void attach(PlanExecutor &ex)
    {
        QObject::connect(&ex, &PlanExecutor::stepChanged, &ex,
                         [this](int i, S s, const QString &) {
                             {
                                 QMutexLocker l(&mutex);
                                 last[i] = s;
                                 done += s == S::Done;
                             }
                             if (hook)
                                 hook(i, s);
                         },
                         Qt::DirectConnection);
    }
};

static bool skipped(const QString &rel)
{
    return rel.startsWith(".smt-staging/") || rel.endsWith("-journal");
}

// relPath -> size, without staging and sqlite journals
static QMap<QString, qint64> tree(const QString &root)
{
    QMap<QString, qint64> m;
    for (const auto &f : scanTree(root))
        if (!skipped(f.path))
            m.insert(f.path, qint64(f.size));
    return m;
}

// DB relPath -> sorted Containers.File
static QMap<QString, QStringList> dbContents(const QString &root)
{
    QMap<QString, QStringList> m;
    const auto t = tree(root);
    for (auto it = t.cbegin(); it != t.cend(); ++it)
        if (it.key().endsWith(".db")) {
            QStringList names = librarydb::containerNames(root + "/" + it.key());
            names.sort();
            m.insert(it.key(), names);
        }
    return m;
}

// The invariant: every Containers.File of every dest DB exists beside it.
static QStringList invariantViolations(const QString &root)
{
    QStringList bad;
    const auto dbs = dbContents(root);
    for (auto it = dbs.cbegin(); it != dbs.cend(); ++it) {
        const QString dir = QFileInfo(root + "/" + it.key()).path();
        for (const auto &name : it.value())
            if (!QFileInfo::exists(dir + "/" + name))
                bad << it.key() + " lists missing " + name;
    }
    return bad;
}

// Every file under root: path, size, mtime and content.
static QByteArray treeHash(const QString &root)
{
    QCryptographicHash h(QCryptographicHash::Sha256);
    auto files = scanTree(root);
    std::sort(files.begin(), files.end(), [](const FileInfo &a, const FileInfo &b) { return a.path < b.path; });
    for (const auto &f : files) {
        h.addData(f.path.toUtf8());
        h.addData(QByteArray::number(qint64(f.size)));
        h.addData(QByteArray::number(f.modificationTime.toMSecsSinceEpoch()));
        QFile file(root + "/" + f.path);
        if (file.open(QIODevice::ReadOnly))
            h.addData(file.readAll());
    }
    // directories too, so a stray (even empty) folder is noticed
    QStringList dirs;
    QDirIterator it(root, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext())
        dirs << QDir(root).relativeFilePath(it.next());
    dirs.sort();
    h.addData(dirs.join('\n').toUtf8());
    return h.result();
}

static QString sectionDir(const QString &s)
{
    return fakeSectionDir(s);
}

// What a library trimmed to `tokens` looks like: files and DB contents.
static QMap<QString, QStringList> expectedDbs(const QStringList &tokens)
{
    QMap<QString, QStringList> m;
    for (const auto &s : kSections) {
        const QStringList dbs = fakeDbNames(s);
        for (int i = 0; i < dbs.size(); ++i) {
            QStringList names{"IR1.spitfire"};
            for (int n = 1; n <= kInstruments; ++n)
                if (i == 0 || n % 2)
                    for (const auto &t : tokens)
                        names << QString("INSTR%1_%2_01.spitfire").arg(n).arg(t);
            names.sort();
            m.insert(sectionDir(s) + "/" + dbs[i], names);
        }
    }
    return m;
}

static QStringList expectedFiles(const QStringList &tokens)
{
    QStringList files;
    const auto dbs = expectedDbs(tokens);
    for (auto it = dbs.cbegin(); it != dbs.cend(); ++it) {
        files << it.key();
        const QString dir = QFileInfo(it.key()).path();
        for (const auto &name : it.value())
            files << dir + "/" + name;
    }
    files.removeDuplicates();
    files.sort();
    return files;
}

static void checkLibrary(const QString &dst, const QStringList &tokens)
{
    QCOMPARE(tree(dst).keys(), expectedFiles(tokens));
    QCOMPARE(dbContents(dst), expectedDbs(tokens));
    QCOMPARE(invariantViolations(dst), QStringList());
    QVERIFY(!QFileInfo::exists(dst + "/.smt-staging"));
    QVERIFY(!QFileInfo::exists(dst + "/.smt.lock"));
}

static void verifyConsistentFailure(const Outcome &o)
{
    QVERIFY(o.finished);
    QVERIFY(!o.ok);
    QVERIFY2(o.message.contains("consistent") && o.message.contains("Apply"), qPrintable(o.message));
}

class TstPlanExecutor : public QObject
{
    Q_OBJECT

    QTemporaryDir mTmp;
    QString mSrc, mDst;

private slots:
    void init()
    {
        QVERIFY(mTmp.isValid());
        mSrc = makeFakeLibrary(mTmp.filePath("src"), kTokens, kSections, kInstruments, kFileSize);
        mDst = mTmp.filePath("dst/lib"); // not created yet: the executor creates it
    }

    void cleanup()
    {
        testing::setMountIdProvider({});
        QDir(mTmp.path()).removeRecursively();
        QDir().mkpath(mTmp.path());
    }

    void firstInstallCopiesEnabledMicsAndPatchesDb()
    {
        const Plan p = makePlan(mSrc, mDst, {"TR01"});
        PlanExecutor ex(mSrc, mDst);
        // Default (queued) connections, as the GUI uses them: delivered on this thread.
        int doneSteps = 0, progressCalls = 0;
        bool offThread = false;
        qint64 lastDone = -1, lastTotal = -1;
        connect(&ex, &PlanExecutor::stepChanged, this, [&](int, S s, const QString &) {
            offThread |= QThread::currentThread() != thread();
            doneSteps += s == S::Done;
        });
        connect(&ex, &PlanExecutor::bytesProgress, this, [&](qint64 d, qint64 t, double) {
            offThread |= QThread::currentThread() != thread();
            ++progressCalls;
            lastDone = d;
            lastTotal = t;
        });
        const auto o = runPlan(ex, p);
        QVERIFY2(o.ok, qPrintable(o.message));
        QTRY_COMPARE(doneSteps, p.steps.size());
        QVERIFY(!offThread);
        QCOMPARE(lastDone, p.bytesToAdd);
        QCOMPARE(lastTotal, p.bytesToAdd);
        QVERIFY(progressCalls >= 1 && progressCalls < 10); // throttled; this copy takes well under 1 s
        checkLibrary(mDst, {"TR01"});
    }

    void removingMicDeletesFilesAndPatchesDb()
    {
        QVERIFY(apply(mSrc, mDst, {"TR01", "MO01"}).ok);
        checkLibrary(mDst, {"TR01", "MO01"});
        const Plan p = makePlan(mSrc, mDst, {"TR01"});
        QCOMPARE(countSteps(p, T::DeleteFile), 12);
        PlanExecutor ex(mSrc, mDst);
        const auto o = runPlan(ex, p);
        QVERIFY2(o.ok, qPrintable(o.message));
        checkLibrary(mDst, {"TR01"});
    }

    void wrongSizeDestFilesAreReplaced()
    {
        QVERIFY(apply(mSrc, mDst, {"TR01"}).ok);
        const QString mic = mDst + "/Samples/_Brass/INSTR1_TR01_01.spitfire";
        const QString other = mDst + "/Samples/_Strings/IR1.spitfire";
        writeFile(mic, 10);
        writeFile(other, kFileSize + 3);
        const auto o = apply(mSrc, mDst, {"TR01"});
        QVERIFY2(o.ok, qPrintable(o.message));
        QCOMPARE(QFileInfo(mic).size(), kFileSize);
        QCOMPARE(QFileInfo(other).size(), kFileSize);
        checkLibrary(mDst, {"TR01"});
    }

    void keptFileDeletedAfterPlanIsNotListed()
    {
        QVERIFY(apply(mSrc, mDst, {"TR01"}).ok);
        const Plan p = makePlan(mSrc, mDst, {"TR01"});
        QVERIFY(QFile::remove(mDst + "/Samples/_Brass/INSTR1_TR01_01.spitfire"));
        QVERIFY(QFile::remove(mDst + "/Samples/_Woodwinds/IR1.spitfire"));
        PlanExecutor ex(mSrc, mDst);
        const auto o = runPlan(ex, p);
        QVERIFY2(o.ok, qPrintable(o.message));
        QCOMPARE(invariantViolations(mDst), QStringList());
        QVERIFY(!librarydb::containerNames(mDst + "/Samples/_Brass/BBCSO_BRASS_V020.db")
                     .contains("INSTR1_TR01_01.spitfire"));
    }

    void patchedDbListingMissingFileFailsStep()
    {
        // The trigger makes the DELETEs silently do nothing, so the patched copy keeps every row.
        const QString db = mSrc + "/Samples/_Brass/BBCSO_BRASS_V020.db";
        execSql(db, "CREATE TRIGGER keep BEFORE DELETE ON Containers BEGIN SELECT RAISE(IGNORE); END");
        const Plan p = makePlan(mSrc, mDst, {"TR01"});
        PlanExecutor ex(mSrc, mDst);
        Recorder rec;
        rec.attach(ex);
        verifyConsistentFailure(runPlan(ex, p));
        int failed = -1;
        for (auto it = rec.last.cbegin(); it != rec.last.cend(); ++it)
            if (it.value() == S::Failed)
                failed = it.key();
        QVERIFY(failed >= 0);
        QCOMPARE(p.steps[failed].type, T::ReplaceDb);
        QCOMPARE(p.steps[failed].relPath, QString("Samples/_Brass/BBCSO_BRASS_V020.db"));
        QCOMPARE(invariantViolations(mDst), QStringList());
    }

    void addingMicBack()
    {
        QVERIFY(apply(mSrc, mDst, {"TR01", "MO01"}).ok);
        QVERIFY(apply(mSrc, mDst, {"TR01"}).ok);
        const auto o = apply(mSrc, mDst, {"TR01", "MO01"});
        QVERIFY2(o.ok, qPrintable(o.message));
        checkLibrary(mDst, {"TR01", "MO01"});
    }

    void nonMicFilesCopiedAndNeverDeleted()
    {
        QDir().mkpath(mSrc + "/Instruments");
        writeFile(mSrc + "/Instruments/Patches.zmulti", 10);
        QDir().mkpath(mDst + "/Samples/_Brass");
        writeFile(mDst + "/Samples/_Brass/notes.txt", 3);
        writeFile(mDst + "/Samples/_Brass/INSTR9_ZZ01_01.spitfire", 4); // unknown token: not a mic
        for (const auto &tokens : {QStringList{}, QStringList{"TR01"}, QStringList{}}) {
            const auto o = apply(mSrc, mDst, tokens);
            QVERIFY2(o.ok, qPrintable(o.message));
            const auto t = tree(mDst);
            QCOMPARE(t.value("Instruments/Patches.zmulti", -1), 10);
            QCOMPARE(t.value("Samples/_Brass/notes.txt", -1), 3);
            QCOMPARE(t.value("Samples/_Brass/INSTR9_ZZ01_01.spitfire", -1), 4);
            for (const auto &s : kSections)
                QCOMPARE(t.value(sectionDir(s) + "/IR1.spitfire", -1), kFileSize);
            QCOMPARE(dbContents(mDst), expectedDbs(tokens));
        }
    }

    void sourceNeverModified()
    {
        const QByteArray before = treeHash(mSrc);
        for (const auto &tokens : {QStringList{"TR01", "MO01"}, QStringList{"TR01"}, QStringList{"CL01"}})
            QVERIFY(apply(mSrc, mDst, tokens).ok);
        QCOMPARE(treeHash(mSrc), before);
    }

    void invariantHoldsAfterEveryStep()
    {
        for (const auto &tokens : {QStringList{"TR01", "MO01"}, QStringList{"TR01", "CL01"}, QStringList{}}) {
            const Plan p = makePlan(mSrc, mDst, tokens);
            PlanExecutor ex(mSrc, mDst);
            Recorder rec;
            rec.hook = [&](int i, S s) {
                if (s != S::Done)
                    return;
                const QStringList bad = invariantViolations(mDst);
                QMutexLocker l(&rec.mutex);
                for (const auto &b : bad)
                    rec.problems << QString("after step %1: %2").arg(i).arg(b);
            };
            rec.attach(ex);
            const auto o = runPlan(ex, p);
            QVERIFY2(o.ok, qPrintable(o.message));
            QCOMPARE(rec.done, p.steps.size());
            QCOMPARE(rec.problems, QStringList());
            checkLibrary(mDst, tokens);
        }
    }

    void cancelMidwayLeavesWorkingLibrary()
    {
        const QString other = mTmp.filePath("dst/uninterrupted");
        for (const auto &d : {mDst, other})
            QVERIFY(apply(mSrc, d, {"TR01", "MO01"}).ok);

        const Plan p = makePlan(mSrc, mDst, {"TR01", "CL01"});
        const int firstCopy = firstStep(p, T::CopyToStaging);
        QVERIFY(firstStep(p, T::DeleteFile) >= 0);
        QVERIFY(firstCopy > 0);
        PlanExecutor ex(mSrc, mDst);
        Recorder rec;
        rec.hook = [&](int i, S s) {
            if (i == firstCopy && s == S::Running) // phase 1 is done, phase 2 just starting
                ex.cancel();
        };
        rec.attach(ex);
        const auto o = runPlan(ex, p);
        QVERIFY(o.finished);
        QVERIFY(!o.ok);
        QVERIFY2(o.message.startsWith("Cancelled") && o.message.contains("consistent")
                     && o.message.contains("Apply"),
                 qPrintable(o.message));
        QCOMPARE(rec.done, firstCopy);
        QCOMPARE(rec.last.value(firstCopy), S::Pending);
        QCOMPARE(invariantViolations(mDst), QStringList());
        QVERIFY(!QFileInfo::exists(mDst + "/.smt.lock"));

        const auto again = apply(mSrc, mDst, {"TR01", "CL01"});
        QVERIFY2(again.ok, qPrintable(again.message));
        QVERIFY(apply(mSrc, other, {"TR01", "CL01"}).ok);
        QCOMPARE(tree(mDst), tree(other));
        QCOMPARE(dbContents(mDst), dbContents(other));
        checkLibrary(mDst, {"TR01", "CL01"});
    }

    void cancelDuringCopyBufferLeavesNoPartFile()
    {
        QVERIFY(apply(mSrc, mDst, {"TR01"}).ok);
        const Plan p = makePlan(mSrc, mDst, kTokens);
        const int firstCopy = firstStep(p, T::CopyToStaging);
        QVERIFY(firstCopy >= 0);
        PlanExecutor ex(mSrc, mDst);
        Recorder rec;
        // Wait out the 0.1 s progress throttle, so the copier's first buffer emits bytesProgress.
        rec.hook = [&](int i, S s) {
            if (i == firstCopy && s == S::Running)
                QThread::msleep(150);
        };
        rec.attach(ex);
        const QString staging = mDst + "/.smt-staging";
        auto partFiles = [&] {
            QStringList parts;
            QDirIterator it(staging, {"*.part"}, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
            while (it.hasNext())
                parts << it.next();
            return parts;
        };
        std::atomic_bool cancelledMidCopy{false};
        connect(&ex, &PlanExecutor::bytesProgress, &ex, [&](qint64, qint64, double) {
            // runs on a copier thread, inside the copy of a buffer whose .part is open
            if (!cancelledMidCopy && !partFiles().isEmpty()) {
                cancelledMidCopy = true;
                ex.cancel();
            }
        }, Qt::DirectConnection);
        const auto o = runPlan(ex, p);
        QVERIFY(o.finished);
        QVERIFY2(!o.ok && o.message.startsWith("Cancelled"), qPrintable(o.message));
        QVERIFY(cancelledMidCopy.load());
        QCOMPARE(partFiles(), QStringList());
        QCOMPARE(invariantViolations(mDst), QStringList());
        QVERIFY(!QFileInfo::exists(mDst + "/.smt.lock"));

        const auto again = apply(mSrc, mDst, kTokens);
        QVERIFY2(again.ok, qPrintable(again.message));
        checkLibrary(mDst, kTokens);
    }

    void sourceVanishesMidCopyFailsAndRerunResumes()
    {
        QVERIFY(apply(mSrc, mDst, {"TR01"}).ok);
        const Plan p = makePlan(mSrc, mDst, kTokens);
        const int firstCopy = firstStep(p, T::CopyToStaging);
        QVERIFY(countSteps(p, T::CopyToStaging) > 16); // so the second batch is a separate copy run
        const int victimIndex = firstCopy + 16;
        QCOMPARE(p.steps[victimIndex].type, T::CopyToStaging);
        const QString victim = mSrc + "/" + p.steps[victimIndex].relPath;

        PlanExecutor ex(mSrc, mDst);
        Recorder rec;
        rec.hook = [&](int i, S s) {
            if (i == victimIndex && s == S::Running)
                QFile::remove(victim);
        };
        rec.attach(ex);
        verifyConsistentFailure(runPlan(ex, p));
        QCOMPARE(rec.last.value(victimIndex), S::Failed);
        QCOMPARE(invariantViolations(mDst), QStringList());
        QCOMPARE(dbContents(mDst), expectedDbs({"TR01"})); // final DB swap never happened

        // Overwrite the first batch's staged files with same-size marker bytes (the source is
        // all 'x'); the rerun must move them, not copy them again.
        const QByteArray marker(kFileSize, 'm');
        QStringList firstBatch;
        for (int i = firstCopy; i < victimIndex; ++i) {
            firstBatch << p.steps[i].relPath;
            QFile f(mDst + "/.smt-staging/" + p.steps[i].relPath);
            QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(f.fileName()));
            QCOMPARE(f.write(marker), kFileSize);
        }

        writeFile(victim, kFileSize);
        const auto o = apply(mSrc, mDst, kTokens);
        QVERIFY2(o.ok, qPrintable(o.message));
        checkLibrary(mDst, kTokens);
        for (const auto &rel : firstBatch) {
            QFile f(mDst + "/" + rel);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), marker);
        }
    }

    void copiedFilesKeepSourceModificationTime()
    {
        const QDateTime when(QDate(2018, 3, 4), QTime(5, 6, 7), QTimeZone::UTC);
        QStringList copied;
        for (const auto &f : scanTree(mSrc))
            if (!f.path.endsWith(".db")) {
                QFile file(mSrc + "/" + f.path);
                QVERIFY(file.open(QIODevice::ReadWrite));
                QVERIFY(file.setFileTime(when, QFileDevice::FileModificationTime));
                copied << f.path;
            }
        const auto o = apply(mSrc, mDst, kTokens);
        QVERIFY2(o.ok, qPrintable(o.message));
        QCOMPARE(copied.size(), 3 * (kInstruments * kTokens.size() + 1));
        for (const auto &rel : copied)
            QCOMPARE(QFileInfo(mDst + "/" + rel).lastModified().toSecsSinceEpoch(), when.toSecsSinceEpoch());
    }

    void secondExecutorRefusedByLock()
    {
        const Plan p = makePlan(mSrc, mDst, {"TR01"});
        Recorder rec; // outlives `first`, whose worker calls the hook
        PlanExecutor first(mSrc, mDst);
        QSemaphore gate;
        std::atomic_bool blocked{false};
        struct Release
        {
            QSemaphore &s;
            ~Release() { s.release(1000); } // never leave the worker blocked, even on failure
        } release{gate};
        rec.hook = [&](int, S s) {
            if (s == S::Running && !blocked.exchange(true))
                gate.acquire();
        };
        rec.attach(first);
        QSignalSpy firstDone(&first, &PlanExecutor::finished);
        QVERIFY(first.start(p));
        QTRY_VERIFY(blocked.load());
        QVERIFY(!first.start(p)); // already running: refused synchronously, nothing emitted

        PlanExecutor second(mSrc, mDst);
        const auto o = runPlan(second, p);
        QVERIFY(o.finished);
        QVERIFY(!o.ok);
        QVERIFY2(o.message.contains("another", Qt::CaseInsensitive), qPrintable(o.message));
        const QString lockPath = QFileInfo(mDst).canonicalFilePath() + "/.smt.lock";
        QVERIFY2(o.message.contains(lockPath) && o.message.contains("manually"), qPrintable(o.message));

        gate.release(1000);
        QVERIFY(firstDone.count() > 0 || firstDone.wait(30000));
        QVERIFY2(firstDone.at(0).at(0).toBool(), qPrintable(firstDone.at(0).at(1).toString()));
        QCOMPARE(firstDone.count(), 1);
        // the lock was released, and the same executor can run again
        const auto again = runPlan(first, makePlan(mSrc, mDst, {"TR01"}));
        QVERIFY2(again.ok, qPrintable(again.message));
        checkLibrary(mDst, {"TR01"});
    }

    void refusesOverlappingDest()
    {
        const QByteArray before = treeHash(mSrc);
        for (const QString &dst : {mSrc, mSrc + "/Trimmed", mSrc + "/Samples"}) {
            PlanExecutor ex(mSrc, dst);
            QSignalSpy spy(&ex, &PlanExecutor::finished);
            QVERIFY(ex.start(makePlan(mSrc, mDst, {"TR01"})));
            QCOMPARE(spy.count(), 0); // refusal is asynchronous
            QVERIFY(spy.wait(5000));
            QVERIFY(!spy.at(0).at(0).toBool());
            QVERIFY(!spy.at(0).at(1).toString().isEmpty());
        }
        QVERIFY(!QFileInfo::exists(mSrc + "/Trimmed"));
        QCOMPARE(treeHash(mSrc), before);

        Plan refused = makePlan(mSrc, mDst, {"TR01"});
        refused.refusals << "Not enough free space";
        PlanExecutor ex(mSrc, mDst);
        const auto o = runPlan(ex, refused);
        QVERIFY(!o.ok);
        QVERIFY(o.message.contains("Not enough free space"));
        QVERIFY(!QFileInfo::exists(mDst));
    }

    void sourceAliasAsDestRefused()
    {
        SMT_UNIX_ONLY();
        const QByteArray before = treeHash(mSrc);
        const QString link = mTmp.filePath("alias");
        QVERIFY(QFile::link(mSrc, link));
        QStringList aliases{link}; // caught by the canonical-path check
        const QString firmlink = "/System/Volumes/Data" + QFileInfo(mSrc).canonicalFilePath();
        if (QFileInfo(firmlink).isDir())
            aliases << firmlink; // canonicalFilePath keeps this path as it is
        for (const QString &dst : aliases) {
            PlanExecutor ex(mSrc, dst);
            const auto o = runPlan(ex, makePlan(mSrc, mDst, {"TR01"}));
            QVERIFY(o.finished);
            QVERIFY2(!o.ok, qPrintable(dst));
            QCOMPARE(treeHash(mSrc), before);
        }
    }

    void sourceLockVisibleRefused()
    {
        SMT_UNIX_ONLY();
        // Simulates a second path to the source that no path check can see (e.g. a second SMB
        // mount): the source's .smt.lock resolves to the lock the executor takes in the destination.
        QVERIFY(QFile::link(mDst + "/.smt.lock", mSrc + "/.smt.lock"));
        PlanExecutor ex(mSrc, mDst);
        const auto o = runPlan(ex, makePlan(mSrc, mDst, {"TR01"}));
        QVERIFY(o.finished);
        QVERIFY(!o.ok);
        QVERIFY2(o.message.contains(".smt.lock"), qPrintable(o.message));
        QVERIFY(!QFileInfo::exists(mDst + "/.smt.lock"));
        QVERIFY(!QFileInfo::exists(mDst + "/Samples"));
    }

    void integrityOrSizeMismatchFailsStep()
    {
        QVERIFY(apply(mSrc, mDst, {"TR01"}).ok);

        // size mismatch: the scan said one size, the copy produced another
        Plan p = makePlan(mSrc, mDst, {"TR01", "MO01"});
        const int copy = firstStep(p, T::CopyToStaging);
        QVERIFY(copy >= 0);
        p.steps[copy].bytes += 1;
        {
            PlanExecutor ex(mSrc, mDst);
            Recorder rec;
            rec.attach(ex);
            verifyConsistentFailure(runPlan(ex, p));
            QCOMPARE(rec.last.value(copy), S::Failed);
        }
        QCOMPARE(tree(mDst).keys(), expectedFiles({"TR01"}));
        QCOMPARE(dbContents(mDst), expectedDbs({"TR01"}));

        // integrity: a source DB that is not a database fails its ReplaceDb step
        const QString src2 = makeFakeLibrary(mTmp.filePath("src2"), kTokens, kSections, kInstruments, kFileSize);
        Plan p2 = makePlan(src2, mDst, {"TR01"});
        const int db = firstStep(p2, T::ReplaceDb);
        QVERIFY(db >= 0);
        {
            QFile f(src2 + "/" + p2.steps[db].relPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(4096, 'g'));
        }
        {
            PlanExecutor ex(src2, mDst);
            Recorder rec;
            rec.attach(ex);
            verifyConsistentFailure(runPlan(ex, p2));
            QCOMPARE(rec.last.value(db), S::Failed);
        }
        QCOMPARE(dbContents(mDst), expectedDbs({"TR01"}));
        QCOMPARE(invariantViolations(mDst), QStringList());
    }

    void emptyPlanWithSymlinkedStagingRefusedAndTargetUntouched()
    {
        SMT_UNIX_ONLY();
        QVERIFY(apply(mSrc, mDst, {"TR01"}).ok);
        const QString target = mTmp.filePath("target");
        QDir().mkpath(target + "/sub");
        writeFile(target + "/keep.txt", 5);
        writeFile(target + "/sub/keep2.txt", 6);
        const QByteArray before = treeHash(target);
        QVERIFY(QFile::link(target, mDst + "/.smt-staging"));

        // An empty plan, and a re-apply of the already trimmed library.
        for (const Plan &p : {Plan{}, makePlan(mSrc, mDst, {"TR01"})}) {
            PlanExecutor ex(mSrc, mDst);
            const auto o = runPlan(ex, p);
            QVERIFY(o.finished);
            QVERIFY(!o.ok);
            QVERIFY2(o.message.contains(".smt-staging"), qPrintable(o.message));
            QCOMPARE(treeHash(target), before);
            QVERIFY(!QFileInfo::exists(mDst + "/.smt.lock"));
        }
    }

    void symlinkedStagingTargetUntouchedOnSuccessfulRun()
    {
        SMT_UNIX_ONLY();
        const QString target = mTmp.filePath("target");
        QDir().mkpath(target);
        writeFile(target + "/keep.txt", 5);
        const QByteArray before = treeHash(target);
        const Plan p = makePlan(mSrc, mDst, {"TR01"});
        PlanExecutor ex(mSrc, mDst);
        Recorder rec;
        rec.hook = [&](int i, S s) { // after the last step, staging is swapped for a link
            if (i == p.steps.size() - 1 && s == S::Done) {
                QDir(mDst + "/.smt-staging").removeRecursively();
                QFile::link(target, mDst + "/.smt-staging");
            }
        };
        rec.attach(ex);
        const auto o = runPlan(ex, p);
        QVERIFY2(o.ok, qPrintable(o.message)); // removal is skipped, the run still succeeds
        QVERIFY(QFileInfo(mDst + "/.smt-staging").isSymLink());
        QCOMPARE(treeHash(target), before);
    }

    void underRootRefusals()
    {
        SMT_UNIX_ONLY();
        QVERIFY(apply(mSrc, mDst, {"TR01"}).ok);
        const QString outside = mTmp.filePath("outside");
        QDir().mkpath(outside);
        writeFile(outside + "/victim.spitfire", 7);
        QVERIFY(QFile::link(outside, mDst + "/escape"));
        const QByteArray before = treeHash(outside);
        for (const QString &rel : {QString("../outside/victim.spitfire"), outside + "/victim.spitfire",
                                   QString("escape/victim.spitfire")}) {
            for (const auto type : {T::DeleteFile, T::MoveIntoPlace, T::ReplaceDb, T::CopyToStaging}) {
                Plan p;
                PlanStep s;
                s.type = type;
                s.relPath = rel;
                s.bytes = 7;
                p.steps << s;
                PlanExecutor ex(mSrc, mDst);
                Recorder rec;
                rec.attach(ex);
                const auto o = runPlan(ex, p);
                QVERIFY(o.finished);
                QVERIFY2(!o.ok, qPrintable(rel));
                QCOMPARE(rec.last.value(0), S::Failed);
                QCOMPARE(treeHash(outside), before);
            }
        }
        QCOMPARE(invariantViolations(mDst), QStringList());
    }

    void destContainsSourceRefused()
    {
        const QByteArray before = treeHash(mTmp.path());
        PlanExecutor ex(mSrc, mTmp.path());
        QSignalSpy spy(&ex, &PlanExecutor::finished);
        ex.start(makePlan(mSrc, mDst, {"TR01"}));
        QCOMPARE(spy.count(), 0);
        QVERIFY(spy.wait(5000));
        QVERIFY(!spy.at(0).at(0).toBool());
        QVERIFY2(spy.at(0).at(1).toString().contains("inside"), qPrintable(spy.at(0).at(1).toString()));
        QCOMPARE(treeHash(mTmp.path()), before);
    }
    // mTmp/alias is the source's share by a second mount (as /Volumes/Share-1 would be): only
    // the mount identity tells. The destination is a new folder "inside the source" that way.
    void aliasDestInsideSourceNeverWritesSource()
    {
        SMT_UNIX_ONLY();
        const QString alias = mTmp.filePath("alias");
        QVERIFY(QDir().mkpath(alias));
        const QString share = QFileInfo(mTmp.path()).canonicalFilePath();
        const QString aliasCanon = QFileInfo(alias).canonicalFilePath();
        testing::setMountIdProvider([share, aliasCanon](const QString &path) -> std::optional<testing::MountId> {
            if (path == aliasCanon || path.startsWith(aliasCanon + '/'))
                return testing::MountId{"//u@srv/Share", "src" + path.mid(aliasCanon.size())};
            if (path == share || path.startsWith(share + '/'))
                return testing::MountId{"//u@srv/Share", path.mid(share.size() + 1)};
            return std::nullopt;
        });
        const QByteArray srcBefore = treeHash(mSrc), aliasBefore = treeHash(alias);
        const QString dst = alias + "/new/out";
        PlanExecutor ex(mSrc, dst);
        bool prepared = false;
        const auto o = [&] {
            QSignalSpy spy(&ex, &PlanExecutor::finished);
            ex.start(makePlan(mSrc, dst, {"TR01"}), [&](QString *) { return prepared = true; });
            Outcome r;
            r.finished = spy.count() > 0 || spy.wait(30000);
            if (r.finished) {
                r.ok = spy.at(0).at(0).toBool();
                r.message = spy.at(0).at(1).toString();
            }
            return r;
        }();
        QVERIFY(o.finished);
        QVERIFY2(!o.ok, qPrintable(o.message));
        QVERIFY2(o.message.contains("mounted twice"), qPrintable(o.message));
        QVERIFY(!prepared);
        QCOMPARE(treeHash(mSrc), srcBefore);
        QCOMPARE(treeHash(alias), aliasBefore); // the "source by the other mount": no folder, lock or config
        QVERIFY(!QFileInfo::exists(alias + "/new"));
        QVERIFY(!QFileInfo::exists(mSrc + "/new"));
        QVERIFY(!QFileInfo::exists(mSrc + "/project.smt"));
        QVERIFY(!QFileInfo::exists(mSrc + "/.smt.lock"));
    }

    // The user picked an existing folder and the app uses <picked>/<exact name>, which does not exist yet.
    void aNewLeafInsideAnExistingChosenFolderIsCreatedByARunAndRemovedByARefusedOne()
    {
        const QString chosen = mTmp.filePath("chosen");
        QVERIFY(QDir().mkpath(chosen));
        QFile keep(chosen + "/keep.txt");
        QVERIFY(keep.open(QIODevice::WriteOnly));
        keep.write("x");
        keep.close();
        const QString dst = chosen + "/Spitfire Audio - BBC Symphony Orchestra";
        const Plan p = makePlan(mSrc, dst, {"TR01"}); // a destination that does not exist is an empty one
        QVERIFY(p.canRun() && !p.isEmpty());
        {   // refused after the leaf was created: the leaf goes again, the chosen folder stays as it was
            PlanExecutor ex(mSrc, dst);
            QSignalSpy spy(&ex, &PlanExecutor::finished);
            QVERIFY(ex.start(p, [](QString *error) {
                *error = "refused";
                return false;
            }));
            QVERIFY(spy.count() > 0 || spy.wait(30000));
            QVERIFY(!spy.at(0).at(0).toBool());
            QVERIFY(!QFileInfo::exists(dst));
            QVERIFY(QFileInfo::exists(chosen + "/keep.txt"));
            QCOMPARE(QDir(chosen).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden), QStringList{"keep.txt"});
        }
        {   // a run that passes the checks creates the leaf and fills it
            PlanExecutor ex(mSrc, dst);
            const Outcome o = runPlan(ex, p);
            QVERIFY2(o.ok, qPrintable(o.message));
            QVERIFY(QFileInfo(dst).isDir());
            checkLibrary(dst, {"TR01"});
            QVERIFY(QFileInfo::exists(chosen + "/keep.txt"));
        }
    }

    void anEmptyPickedFolderSurvivesARefusedRunThatCreatedOnlyTheLeaf()
    {
        const QString chosen = mTmp.filePath("emptychosen");
        QVERIFY(QDir().mkpath(chosen)); // empty
        const QString dst = chosen + "/Spitfire Audio - BBC Symphony Orchestra";
        PlanExecutor ex(mSrc, dst);
        QSignalSpy spy(&ex, &PlanExecutor::finished);
        QVERIFY(ex.start(makePlan(mSrc, dst, {"TR01"}), [](QString *error) {
            *error = "refused";
            return false;
        }));
        QVERIFY(spy.count() > 0 || spy.wait(30000));
        QVERIFY(!spy.at(0).at(0).toBool());
        QVERIFY(!QFileInfo::exists(dst)); // the leaf the run created is gone
        QVERIFY(QFileInfo(chosen).isDir()); // the folder the user picked stays, as it was
        QVERIFY(QDir(chosen).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty());
    }

    // Spitfire's Repair rewrites a patch in place at the same size (mtime kept): the next Apply must replace it.
    void aSameSizePatchWithNewContentIsReplacedAndTheSourceIsUntouched()
    {
        auto write = [](const QString &path, const QByteArray &data) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        auto read = [](const QString &path) {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        const QString patch = "Patches/BBC/a.zmulti";
        write(mSrc + "/" + patch, "AAAA");
        { // the first Apply copies it
            PlanExecutor ex(mSrc, mDst);
            const Outcome o = runPlan(ex, makePlan(mSrc, mDst, {"TR01", "MO01"}));
            QVERIFY2(o.ok, qPrintable(o.message));
        }
        QCOMPARE(read(mDst + "/" + patch), QByteArray("AAAA"));
        // Repair: same size, other content, the modification time kept
        const QDateTime mtime = QFileInfo(mSrc + "/" + patch).lastModified();
        write(mSrc + "/" + patch, "BBBB");
        {
            QFile f(mSrc + "/" + patch);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(mtime, QFileDevice::FileModificationTime));
        }
        // an interrupted earlier run left a same-size staged copy of the old content
        write(mDst + "/.smt-staging/" + patch, "AAAA");
        const QStringList srcBefore = snapshotOf(mSrc);
        const Plan p = makePlan(mSrc, mDst, {"TR01", "MO01"});
        QCOMPARE(countSteps(p, T::CopyToStaging), 1); // only the settings file
        QCOMPARE(p.bytesAlreadyStaged, qint64(0));
        PlanExecutor ex(mSrc, mDst);
        const Outcome o = runPlan(ex, p);
        QVERIFY2(o.ok, qPrintable(o.message));
        QCOMPARE(read(mDst + "/" + patch), QByteArray("BBBB"));
        QCOMPARE(read(mSrc + "/" + patch), QByteArray("BBBB"));
        QCOMPARE(snapshotOf(mSrc), srcBefore); // the source is never written
        QVERIFY(!QFileInfo::exists(mDst + "/.smt-staging/" + patch));
        // and a third Apply refreshes it again (it cannot know), with the same result
        PlanExecutor again(mSrc, mDst);
        const Outcome o2 = runPlan(again, makePlan(mSrc, mDst, {"TR01", "MO01"}));
        QVERIFY2(o2.ok, qPrintable(o2.message));
        QCOMPARE(read(mDst + "/" + patch), QByteArray("BBBB"));
    }

    void prepareHookRunsAfterLockAndFailureAbortsRun()
    {
        const Plan p = makePlan(mSrc, mDst, {"TR01"});
        PlanExecutor ex(mSrc, mDst);
        Recorder rec;
        rec.attach(ex);
        QSignalSpy progress(&ex, &PlanExecutor::bytesProgress);
        std::atomic_bool lockHeld{false}, offThread{false}, called{false};
        QThread *mainThread = QThread::currentThread();
        QSignalSpy spy(&ex, &PlanExecutor::finished);
        QVERIFY(ex.start(p, [&](QString *error) {
            called = true;
            lockHeld = QFileInfo::exists(QFileInfo(mDst).canonicalFilePath() + "/.smt.lock");
            offThread = QThread::currentThread() != mainThread;
            *error = "config not saved: boom";
            return false;
        }));
        QVERIFY(spy.count() > 0 || spy.wait(30000));
        QVERIFY(called.load());
        QVERIFY(lockHeld.load());
        QVERIFY(offThread.load());
        QVERIFY(!spy.at(0).at(0).toBool());
        QVERIFY2(spy.at(0).at(1).toString().contains("boom"), qPrintable(spy.at(0).at(1).toString()));
        QVERIFY(rec.last.isEmpty()); // no step started
        QCOMPARE(progress.count(), 0);
        QVERIFY(!QFileInfo::exists(mDst)); // the run created it, and removed it again
        QVERIFY(!QFileInfo::exists(mTmp.filePath("dst")));

        // a passing hook runs once, then the steps
        int calls = 0;
        QSignalSpy spy2(&ex, &PlanExecutor::finished);
        QVERIFY(ex.start(p, [&](QString *) {
            ++calls;
            return rec.last.isEmpty();
        }));
        QVERIFY(spy2.count() > 0 || spy2.wait(30000));
        QVERIFY2(spy2.at(0).at(0).toBool(), qPrintable(spy2.at(0).at(1).toString()));
        QCOMPARE(calls, 1);
        checkLibrary(mDst, {"TR01"});
    }

    void refusedRunRemovesDirsItCreated()
    {
        SMT_UNIX_ONLY();
        // the source's lock resolves to the lock taken in the destination: refused after mkpath
        const Plan p = makePlan(mSrc, mDst, {"TR01"});
        QVERIFY(QFile::link(mTmp.filePath("dst/lib/.smt.lock"), mSrc + "/.smt.lock"));
        {
            PlanExecutor ex(mSrc, mDst);
            const auto o = runPlan(ex, p);
            QVERIFY(o.finished && !o.ok);
            QVERIFY2(o.message.contains(".smt.lock"), qPrintable(o.message));
        }
        QVERIFY(!QFileInfo::exists(mTmp.filePath("dst"))); // dst and dst/lib were both created by the run

        // an existing parent (and what is in it) stays; only the created folder goes
        QVERIFY(QDir().mkpath(mTmp.filePath("dst")));
        writeFile(mTmp.filePath("dst/keep.txt"), 3);
        {
            PlanExecutor ex(mSrc, mDst);
            QVERIFY(!runPlan(ex, p).ok);
        }
        QVERIFY(!QFileInfo::exists(mDst));
        QVERIFY(QFileInfo::exists(mTmp.filePath("dst/keep.txt")));

        // an existing destination is never removed
        QVERIFY(QDir().mkpath(mDst));
        {
            PlanExecutor ex(mSrc, mDst);
            QVERIFY(!runPlan(ex, p).ok);
        }
        QVERIFY(QFileInfo(mDst).isDir());
        QVERIFY(!QFileInfo::exists(mDst + "/.smt.lock"));
    }

    void probeChecksTrailingSuffixes()
    {
        SMT_UNIX_ONLY();
        // The destination dst/lib is <source>/dst/lib by another path that no path check sees:
        // the lock taken in it shows up as <source>/dst/lib/.smt.lock.
        const Plan p = makePlan(mSrc, mDst, {"TR01"});
        QVERIFY(QDir().mkpath(mSrc + "/dst/lib"));
        QVERIFY(QFile::link(mTmp.filePath("dst/lib/.smt.lock"), mSrc + "/dst/lib/.smt.lock"));
        PlanExecutor ex(mSrc, mDst);
        const auto o = runPlan(ex, p);
        QVERIFY(o.finished);
        QVERIFY2(!o.ok, qPrintable(o.message));
        QVERIFY2(o.message.contains(mSrc + "/dst/lib/.smt.lock"), qPrintable(o.message));
        QVERIFY(!QFileInfo::exists(mTmp.filePath("dst")));

        // pure function: each trailing suffix of dest, and nothing for an unrelated source
        QVERIFY(QDir().mkpath(mDst));
        writeFile(mDst + "/.smt.lock", 1);
        QVERIFY(!checkSourceLock(mSrc, mDst).isEmpty());
        QCOMPARE(checkSourceLock(mSrc, mTmp.filePath("dst/other")), QString());
        QCOMPARE(checkSourceLock(mSrc), QString());
    }
    void prepareHookThatThrowsAbortsRun()
    {
        const Plan p = makePlan(mSrc, mDst, {"TR01"});
        PlanExecutor ex(mSrc, mDst);
        Recorder rec;
        rec.attach(ex);
        QSignalSpy spy(&ex, &PlanExecutor::finished);
        QVERIFY(ex.start(p, [](QString *) -> bool { throw std::runtime_error("disk on fire"); }));
        QVERIFY(spy.count() > 0 || spy.wait(30000));
        QVERIFY(!spy.at(0).at(0).toBool());
        QVERIFY2(spy.at(0).at(1).toString().contains("disk on fire"), qPrintable(spy.at(0).at(1).toString()));
        QVERIFY(rec.last.isEmpty());
        QVERIFY(!QFileInfo::exists(mTmp.filePath("dst"))); // lock released, created folders removed
        // not busy any more: a normal run follows
        const auto again = runPlan(ex, p);
        QVERIFY2(again.ok, qPrintable(again.message));
        checkLibrary(mDst, {"TR01"});
    }
};

QTEST_GUILESS_MAIN(TstPlanExecutor)
#include "tst_planexecutor.moc"
