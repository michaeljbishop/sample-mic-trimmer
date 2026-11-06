#include <QtTest>
#include "coreutil.h"
#include "plan.h"
#include "profileregistry.h"

using namespace smt;
using T = PlanStep::Type;

static const QString kBrassDb = QStringLiteral("Samples/_Brass/BBCSO_BRASS_V020.db");
static const QString kStringsDb = QStringLiteral("Samples/_Strings/BBCSO_STRINGS_V020.db");
static const QString kBTr = QStringLiteral("Samples/_Brass/B_TR01.spitfire");
static const QString kBMo = QStringLiteral("Samples/_Brass/B_MO01.spitfire");
static const QString kBZz = QStringLiteral("Samples/_Brass/B_ZZ01.spitfire");
static const QString kSTr = QStringLiteral("Samples/_Strings/S_TR01.spitfire");
static const QString kSMo = QStringLiteral("Samples/_Strings/S_MO01.spitfire");
static const QString kInst = QStringLiteral("Instruments/a.zmulti");

static FileInfo fi(const QString &path, size_t size)
{
    FileInfo f{};
    f.path = path;
    f.size = size;
    return f;
}

// sizes: dbs 1000/2000, B_TR 100, B_MO 200, B_ZZ 7, S_TR 300, S_MO 400, inst 5
static QList<FileInfo> sourceFiles()
{
    return {fi(kBrassDb, 1000), fi(kBTr, 100), fi(kBMo, 200), fi(kBZz, 7),
            fi(kStringsDb, 2000), fi(kSTr, 300), fi(kSMo, 400), fi(kInst, 5)};
}

static MicStatuses allMics(bool enabled)
{
    MicStatuses m;
    forAllMicSignalGroupKeys(builtInProfile(kBbcsoProfileId), [&](const MicSignalGroupKey &k) { m[k.mic][k.group] = enabled; });
    return m;
}

static PlanInput input(const QList<FileInfo> &dest, const MicStatuses &mics, qint64 freeBytes = 1000000)
{
    PlanInput in;
    in.profile = builtInProfile(kBbcsoProfileId);
    in.sourceRoot = "/src";
    in.destRoot = "/dst";
    in.source = sourceFiles();
    in.dest = dest;
    in.mics = mics;
    in.freeBytes = freeBytes;
    return in;
}

static QStringList paths(const Plan &p, T type, int phase)
{
    QStringList r;
    for (const auto &s : p.steps)
        if (s.type == type && s.phase == phase)
            r << s.relPath;
    return r;
}

static const PlanStep *dbStep(const Plan &p, int phase, const QString &db)
{
    for (const auto &s : p.steps)
        if (s.type == T::ReplaceDb && s.phase == phase && s.relPath == db)
            return &s;
    return nullptr;
}

static int fileStepCount(const Plan &p)
{
    int n = 0;
    for (const auto &s : p.steps)
        n += s.type != T::ReplaceDb;
    return n;
}

// Simulates a successful run: dest minus deletes plus adds.
static QList<FileInfo> applied(const PlanInput &in, const Plan &p)
{
    QHash<QString, FileInfo> d;
    for (const auto &f : in.dest)
        d.insert(f.path, f);
    QHash<QString, size_t> srcSize;
    for (const auto &f : in.source)
        srcSize.insert(f.path, f.size);
    for (const auto &s : p.steps) {
        if (s.type == T::DeleteFile)
            d.remove(s.relPath);
        else if (s.type == T::MoveIntoPlace)
            d.insert(s.relPath, fi(s.relPath, srcSize.value(s.relPath)));
        else if (s.type == T::ReplaceDb)
            d.insert(s.relPath, fi(s.relPath, srcSize.value(s.relPath)));
    }
    return d.values();
}

// A flat Whitacre library: tutti CL01 (switchable), CL02 and CL06 (not in the profile: always kept).
static const QString kWDb = QStringLiteral("Samples/Tutti.db");
static const QString kWCl01 = QStringLiteral("Samples/ERICW_TUTTI_X_RR1_CL01_01.spitfire");
static const QString kWCl02 = QStringLiteral("Samples/ERICW_TUTTI_X_RR1_CL02_01.spitfire");
static const QString kWCl06 = QStringLiteral("Samples/ERICW_TUTTI_X_RR1_CL06_01.spitfire");

static PlanInput whitacreInput(const MicStatuses &mics)
{
    const QList<FileInfo> files{fi(kWDb, 1000), fi(kWCl01, 10), fi(kWCl02, 20), fi(kWCl06, 30)};
    PlanInput in;
    in.profile = builtInProfile(kEwcProfileId);
    in.sourceRoot = "/src";
    in.destRoot = "/dst";
    in.source = files;
    in.dest = files;
    in.mics = mics;
    in.freeBytes = 1000000;
    return in;
}

class TstPlan : public QObject
{
    Q_OBJECT
private slots:
    void whitacreUnlistedTokenIsKeptNotDeleted()
    {
        MicStatuses mics;
        mics["cl01"]["tutti"] = false;
        const Plan p = buildPlan(whitacreInput(mics));
        QCOMPARE(paths(p, T::DeleteFile, 1), QStringList{kWCl01});
        QVERIFY(p.canRun());
        QCOMPARE(dbStep(p, 1, kWDb)->keepFileNames, (QSet<QString>{QStringLiteral("ERICW_TUTTI_X_RR1_CL02_01.spitfire"), QStringLiteral("ERICW_TUTTI_X_RR1_CL06_01.spitfire")}));
    }
    void unlistedCl02NeverDeleted()
    {
        MicStatuses mics; // every listed mic off, plus cl02 which is not a mic of this profile
        forAllMicSignalGroupKeys(builtInProfile(kEwcProfileId), [&](const MicSignalGroupKey &k) { mics[k.mic][k.group] = false; });
        mics["cl02"]["tutti"] = false;
        const Plan p = buildPlan(whitacreInput(mics));
        QCOMPARE(paths(p, T::DeleteFile, 1), QStringList{kWCl01}); // never the unlisted CL02 or CL06
        QVERIFY(p.canRun());
    }
    void lockedBbcsoMixNeverDeleted()
    {
        const QString mi = QStringLiteral("Samples/_Brass/B_MI01.spitfire");
        MicStatuses mics = allMics(false);
        mics["mi01"]["brass"] = false; // hand-edited config: Mix 1 is locked, so still on
        PlanInput in = input({fi(kBrassDb, 1000), fi(mi, 50), fi(kBMo, 200)}, mics); // MO01 is really off: DB patched
        in.source << fi(mi, 50);
        const Plan p = buildPlan(in);
        QCOMPARE(paths(p, T::DeleteFile, 1), QStringList{kBMo});
        QVERIFY(dbStep(p, 1, kBrassDb) && dbStep(p, 1, kBrassDb)->keepFileNames.contains(QStringLiteral("B_MI01.spitfire")));
        QVERIFY(p.canRun());
    }
    void emptyDestInstallsEverythingEnabled()
    {
        auto mics = allMics(true);
        mics["mo01"]["brass"] = false;
        const Plan p = buildPlan(input({}, mics, 10000));
        const QStringList adds = {kInst, kBTr, kBZz, kSMo, kSTr};
        QCOMPARE(paths(p, T::CopyToStaging, 2), adds);
        QCOMPARE(paths(p, T::MoveIntoPlace, 2), adds);
        QCOMPARE(paths(p, T::ReplaceDb, 2), (QStringList{kBrassDb, kStringsDb}));
        QVERIFY(paths(p, T::ReplaceDb, 1).isEmpty());
        QVERIFY(paths(p, T::DeleteFile, 1).isEmpty());
        QCOMPARE(p.bytesToAdd, 812);
        QCOMPARE(p.bytesToFree, 0);
        QCOMPARE(p.bytesForDatabases, 3000);
        QCOMPARE(p.freeBytesAfter, 10000 - 812 - 3000);
        QVERIFY(p.canRun());
        QVERIFY(!p.isEmpty());
    }
    void disabledMicOnlyDeletedWhenPresent()
    {
        auto mics = allMics(true);
        mics["mo01"]["brass"] = false;
        mics["tr01"]["strings"] = false; // absent from dest
        const Plan p = buildPlan(input({fi(kBrassDb, 1000), fi(kBTr, 100), fi(kBMo, 222)}, mics));
        QCOMPARE(paths(p, T::DeleteFile, 1), QStringList{kBMo});
        QCOMPARE(p.bytesToFree, 222);
        QVERIFY(!paths(p, T::CopyToStaging, 2).contains(kSTr));
        QVERIFY(paths(p, T::CopyToStaging, 2).contains(kSMo));
        for (const auto &s : p.steps)
            if (s.type == T::DeleteFile)
                QCOMPARE(s.bytes, 222);
    }
    void otherFilesNeverDeleted()
    {
        const QList<FileInfo> dest = {fi(kBrassDb, 1000), fi(kStringsDb, 2000), fi(kInst, 5),
                                      fi("Samples/_Brass/notes.txt", 3), fi(kBTr, 100)};
        const Plan p = buildPlan(input(dest, allMics(false)));
        QCOMPARE(paths(p, T::DeleteFile, 1), QStringList{kBTr});
    }
    void unknownTokenFilesNeverDeleted()
    {
        const QList<FileInfo> dest = {fi(kBrassDb, 1000), fi(kBZz, 7),
                                      fi("Samples/_Misc/X_TR01.spitfire", 9)};
        const Plan p = buildPlan(input(dest, allMics(false)));
        QVERIFY(paths(p, T::DeleteFile, 1).isEmpty());
        QCOMPARE(p.bytesToFree, 0);
    }
    void phase1SkippedWhenNoDeletes()
    {
        const QList<FileInfo> dest = {fi(kBrassDb, 1000), fi(kBTr, 100), fi(kBZz, 7)};
        const Plan p = buildPlan(input(dest, allMics(true)));
        for (const auto &s : p.steps)
            QCOMPARE(s.phase, 2);
        QVERIFY(paths(p, T::CopyToStaging, 2).contains(kBMo));
    }
    void phase1KeepSetExcludesDeletedFiles()
    {
        auto mics = allMics(true);
        mics["mo01"]["brass"] = false;
        const QList<FileInfo> dest = {fi(kBrassDb, 1000), fi(kBTr, 100), fi(kBMo, 200), fi(kBZz, 7),
                                      fi(kStringsDb, 2000), fi(kSTr, 300), fi(kSMo, 400)};
        const Plan p = buildPlan(input(dest, mics));
        QCOMPARE(paths(p, T::ReplaceDb, 1), QStringList{kBrassDb}); // strings has no deletes
        const PlanStep *s = dbStep(p, 1, kBrassDb);
        QVERIFY(s);
        QCOMPARE(s->keepFileNames, (QSet<QString>{"B_TR01.spitfire", "B_ZZ01.spitfire"}));
    }
    void phase2KeepSetIsFinalState()
    {
        auto mics = allMics(true);
        mics["mo01"]["brass"] = false;
        const QList<FileInfo> dest = {fi(kBrassDb, 1000), fi(kBMo, 200), fi(kBZz, 7)};
        const Plan p = buildPlan(input(dest, mics));
        const PlanStep *b = dbStep(p, 2, kBrassDb);
        const PlanStep *s = dbStep(p, 2, kStringsDb);
        QVERIFY(b && s);
        QCOMPARE(b->keepFileNames, (QSet<QString>{"B_TR01.spitfire", "B_ZZ01.spitfire"}));
        QCOMPARE(s->keepFileNames, (QSet<QString>{"S_TR01.spitfire", "S_MO01.spitfire"}));
    }
    void stepOrderIsPhase1DbDeletePhase2CopyMoveDb()
    {
        auto mics = allMics(true);
        mics["mo01"]["brass"] = false;
        mics["mo01"]["strings"] = false;
        const QList<FileInfo> dest = {fi(kBrassDb, 1000), fi(kBMo, 200), fi(kStringsDb, 2000), fi(kSMo, 400)};
        const Plan p = buildPlan(input(dest, mics));
        auto rank = [](const PlanStep &s) {
            if (s.phase == 1)
                return s.type == T::ReplaceDb ? 0 : 1;
            return s.type == T::CopyToStaging ? 2 : s.type == T::MoveIntoPlace ? 3 : 4;
        };
        QSet<int> seen;
        for (int i = 0; i < p.steps.size(); ++i) {
            seen.insert(rank(p.steps[i]));
            if (i > 0) {
                const int a = rank(p.steps[i - 1]), b = rank(p.steps[i]);
                QVERIFY(a < b || (a == b && p.steps[i - 1].relPath < p.steps[i].relPath));
            }
        }
        QCOMPARE(seen, (QSet<int>{0, 1, 2, 3, 4}));
        QCOMPARE(paths(p, T::ReplaceDb, 1), (QStringList{kBrassDb, kStringsDb}));
    }
    void refusesWhenNotEnoughSpace()
    {
        const Plan p = buildPlan(input({}, allMics(true), 100));
        // adds 100+200+7+300+400+5 = 1012 plus dbs 3000, so 3912 short
        QCOMPARE(p.bytesToAdd, 1012);
        QVERIFY(!p.canRun());
        QCOMPARE(p.refusals.size(), 1);
        QVERIFY2(p.refusals.first().contains(formatByteSize(3912)), qPrintable(p.refusals.first()));
        QVERIFY(!p.isEmpty());
    }
    void freedBytesCountTowardSpace()
    {
        auto mics = allMics(true);
        mics["mo01"]["brass"] = false;
        const QList<FileInfo> dest = {fi(kBrassDb, 1000), fi(kBMo, 500), fi(kBTr, 100), fi(kBZz, 7),
                                      fi(kStringsDb, 2000), fi(kInst, 5)};
        // adds S_TR 300 + S_MO 400 = 700 plus dbs 3000; frees 500; needs 3200
        Plan p = buildPlan(input(dest, mics, 3200));
        QCOMPARE(p.bytesToAdd, 700);
        QCOMPARE(p.bytesToFree, 500);
        QCOMPARE(p.bytesForDatabases, 3000);
        QCOMPARE(p.freeBytesAfter, 0);
        QVERIFY(p.canRun());
        p = buildPlan(input(dest, mics, 3199));
        QVERIFY(!p.canRun());
        QCOMPARE(p.freeBytesAfter, -1);
    }
    void stagedFilesCountTowardSpace()
    {
        // 300 bytes were free before an interrupted run staged 200 of the 250 to add (+ a 1-byte DB)
        const QString db = "Samples/_Brass/B.db", a = "Samples/_Brass/A_TR01.spitfire",
                      b = "Samples/_Brass/B_TR01.spitfire";
        PlanInput in = input({fi(".smt-staging/" + a, 200)}, allMics(true), 100);
        in.source = {fi(db, 1), fi(a, 200), fi(b, 50)};
        Plan p = buildPlan(in);
        QVERIFY2(p.canRun(), qPrintable(p.refusals.join(' ')));
        QCOMPARE(p.bytesToAdd, 250);
        QCOMPARE(p.bytesAlreadyStaged, 200);
        QCOMPARE(p.freeBytesAfter, 100 - 250 - 1 + 200);
        QVERIFY2(p.toText().contains("Bytes already staged: " + formatByteSize(200)), qPrintable(p.toText()));

        in.dest = {fi(".smt-staging/" + a, 199), fi(".smt-staging/" + db, 1)}; // incomplete: no credit
        p = buildPlan(in);
        QCOMPARE(p.bytesAlreadyStaged, 0);
        QVERIFY(!p.canRun());
    }
    void databaseBytesCountTowardSpace()
    {
        // nothing to copy or delete: only the two database copies need space
        const QList<FileInfo> dest = {fi(kBrassDb, 1000), fi(kBTr, 100), fi(kBMo, 200), fi(kBZz, 7),
                                      fi(kStringsDb, 2000), fi(kSTr, 300), fi(kSMo, 400), fi(kInst, 5)};
        Plan p = buildPlan(input(dest, allMics(true), 3000));
        QCOMPARE(p.bytesToAdd, 0);
        QCOMPARE(p.bytesForDatabases, 3000);
        QCOMPARE(p.freeBytesAfter, 0);
        QVERIFY(p.canRun());
        p = buildPlan(input(dest, allMics(true), 2999));
        QVERIFY(!p.canRun());
        QVERIFY2(p.refusals.join(' ').contains(formatByteSize(1)), qPrintable(p.refusals.join(' ')));
        QVERIFY2(p.toText().contains(formatByteSize(3000)), qPrintable(p.toText()));
    }
    void micShapedFileOutsideSamplesIsNeverDeleted()
    {
        const QString outside = "Backup/_Strings/S_MO01.spitfire";
        const Plan p = buildPlan(input({fi(kBrassDb, 1000), fi(outside, 50)}, allMics(false)));
        QVERIFY(!paths(p, T::DeleteFile, 1).contains(outside));
        QVERIFY(p.canRun());
    }
    void refusesDeleteInDirWithoutSourceDb()
    {
        // Under Samples/: mic files outside the profile's samplesDir are "other" and never deleted.
        const QString orphanMo = "Samples/Backup/_Strings/S_MO01.spitfire";
        PlanInput in = input({fi(kBrassDb, 1000), fi(orphanMo, 50)}, allMics(false));
        const Plan p = buildPlan(in);
        QCOMPARE(paths(p, T::DeleteFile, 1), QStringList{orphanMo});
        QVERIFY(!p.canRun());
        QVERIFY2(p.refusals.join(' ').contains("Backup/_Strings"), qPrintable(p.refusals.join(' ')));
    }
    void refusalOrderIsSorted()
    {
        QList<FileInfo> dest{fi(kBrassDb, 1000)};
        for (int i = 0; i < 20; ++i)
            dest << fi(QString("Samples/Backup%1/_Strings/S_MO01.spitfire").arg(i, 2, 10, QChar('0')), 5);
        const Plan p = buildPlan(input(dest, allMics(false)));
        QCOMPARE(p.refusals.size(), 20);
        QStringList sorted = p.refusals;
        sorted.sort();
        QCOMPARE(p.refusals, sorted);
    }
    void refusesDestOnlyDbInDirWithDeletes()
    {
        const QString oldDb = "Samples/_Brass/OLD_V001.db";
        const Plan p = buildPlan(input({fi(kBrassDb, 1000), fi(oldDb, 10), fi(kBMo, 200)}, allMics(false)));
        QCOMPARE(paths(p, T::DeleteFile, 1), QStringList{kBMo});
        QVERIFY(!p.canRun());
        QVERIFY2(p.refusals.join(' ').contains(oldDb), qPrintable(p.refusals.join(' ')));
        // without deletes there, the extra DB is harmless
        QVERIFY(buildPlan(input({fi(kBrassDb, 1000), fi(oldDb, 10)}, allMics(true))).canRun());
    }
    void copyStepsCarrySourceModificationTime()
    {
        PlanInput in = input({}, allMics(true));
        const QDateTime when(QDate(2019, 5, 6), QTime(7, 8, 9), QTimeZone::UTC);
        for (auto &f : in.source)
            f.modificationTime = when.addSecs(f.size);
        const Plan p = buildPlan(in);
        int copies = 0;
        for (const auto &s : p.steps)
            if (s.type == T::CopyToStaging) {
                ++copies;
                QCOMPARE(s.modified, when.addSecs(s.bytes));
            }
        QCOMPARE(copies, 6);
    }
    void hasFileChanges()
    {
        const QList<FileInfo> full = {fi(kBrassDb, 1000), fi(kBTr, 100), fi(kBMo, 200), fi(kBZz, 7),
                                      fi(kStringsDb, 2000), fi(kSTr, 300), fi(kSMo, 400), fi(kInst, 5)};
        const Plan same = buildPlan(input(full, allMics(true)));
        QVERIFY(!same.isEmpty()); // the DB refresh steps remain
        QVERIFY(!same.hasFileChanges());
        auto mics = allMics(true);
        mics["mo01"]["brass"] = false;
        QVERIFY(buildPlan(input(full, mics)).hasFileChanges());          // a delete
        QVERIFY(buildPlan(input({}, allMics(true))).hasFileChanges());   // copies and moves
        QVERIFY(!Plan{}.hasFileChanges());
    }
    void twoDbsInOneSectionDir()
    {
        const QString w = "Samples/_Woodwinds/";
        const QString v020 = w + "BBCSO_WOODS_V020.db", v115 = w + "BBCSO_WOODS_V115.db";
        PlanInput in = input({fi(v020, 10), fi(v115, 20), fi(w + "W_MO01.spitfire", 30), fi(w + "W_TR01.spitfire", 40)},
                             allMics(true));
        in.source = {fi(v020, 10), fi(v115, 20), fi(w + "W_MO01.spitfire", 30), fi(w + "W_TR01.spitfire", 40),
                     fi(w + "W_CL01.spitfire", 50)};
        in.mics["mo01"]["woodwinds"] = false;
        const Plan p = buildPlan(in);
        QVERIFY(p.canRun());
        QCOMPARE(paths(p, T::ReplaceDb, 1), (QStringList{v020, v115}));
        QCOMPARE(paths(p, T::ReplaceDb, 2), (QStringList{v020, v115}));
        QCOMPARE(dbStep(p, 1, v020)->keepFileNames, (QSet<QString>{"W_TR01.spitfire"}));
        QCOMPARE(dbStep(p, 1, v115)->keepFileNames, dbStep(p, 1, v020)->keepFileNames);
        QCOMPARE(dbStep(p, 2, v020)->keepFileNames, (QSet<QString>{"W_TR01.spitfire", "W_CL01.spitfire"}));
        QCOMPARE(dbStep(p, 2, v115)->keepFileNames, dbStep(p, 2, v020)->keepFileNames);
        QCOMPARE(p.bytesForDatabases, 30);
    }
    void filenameWithNewlineSanitisedInDescribe()
    {
        PlanStep s;
        s.type = T::CopyToStaging;
        s.phase = 2;
        s.relPath = "Samples/_Brass/evil\nname\r.spitfire";
        const QString d = s.describe();
        QVERIFY(!d.contains('\n'));
        QVERIFY(!d.contains('\r'));
        QVERIFY2(d.contains("evil name .spitfire"), qPrintable(d));
    }
    void idempotent()
    {
        auto mics = allMics(true);
        mics["mo01"]["brass"] = false;
        mics["tr01"]["strings"] = false;
        PlanInput in = input({fi(kBrassDb, 1000), fi(kBMo, 200), fi(kSTr, 300)}, mics);
        const Plan first = buildPlan(in);
        QVERIFY(fileStepCount(first) > 0);
        in.dest = applied(in, first);
        const Plan second = buildPlan(in);
        QCOMPARE(fileStepCount(second), 0);
        QCOMPARE(second.bytesToAdd, 0);
        QCOMPARE(second.bytesToFree, 0);
        QCOMPARE(paths(second, T::ReplaceDb, 2), (QStringList{kBrassDb, kStringsDb}));
        QVERIFY(paths(second, T::ReplaceDb, 1).isEmpty());
    }
    void wrongSizeDestFileIsReplaced()
    {
        const QString otherDb = "Instruments/presets.db";
        auto mics = allMics(true);
        mics["mo01"]["brass"] = false;
        // wrong size: inst (other), B_TR (enabled mic), B_ZZ (beside the DB), B_MO (disabled), otherDb
        // right size: S_TR, S_MO
        PlanInput in = input({fi(kBrassDb, 999), fi(kBTr, 1), fi(kBMo, 2), fi(kBZz, 3), fi(kStringsDb, 2000),
                              fi(kSTr, 300), fi(kSMo, 400), fi(kInst, 4), fi(otherDb, 41)},
                             mics);
        in.source << fi(otherDb, 40);
        const Plan p = buildPlan(in);
        const QStringList replaced{kInst, kBTr, kBZz};
        QCOMPARE(paths(p, T::CopyToStaging, 2), replaced);
        QCOMPARE(paths(p, T::MoveIntoPlace, 2), replaced);
        QCOMPARE(paths(p, T::DeleteFile, 1), QStringList{kBMo});
        QCOMPARE(p.bytesToAdd, 5 + 100 + 7); // full source sizes
        QCOMPARE(dbStep(p, 2, kBrassDb)->keepFileNames, (QSet<QString>{"B_TR01.spitfire", "B_ZZ01.spitfire"}));
        for (const auto &s : p.steps)
            if (s.type == T::CopyToStaging || s.type == T::MoveIntoPlace)
                QCOMPARE(s.bytes, qint64(s.relPath == kInst ? 5 : s.relPath == kBTr ? 100 : 7));

        in.dest = applied(in, p);
        const Plan second = buildPlan(in);
        QVERIFY(paths(second, T::CopyToStaging, 2).isEmpty());
        QCOMPARE(fileStepCount(second), 0);
    }
    void describeIsSingleLine()
    {
        auto mics = allMics(true);
        mics["mo01"]["brass"] = false;
        const Plan p = buildPlan(input({fi(kBrassDb, 1000), fi(kBMo, 200)}, mics));
        const QString text = p.toText();
        QSet<T> types;
        for (const auto &s : p.steps) {
            types.insert(s.type);
            const QString d = s.describe();
            QVERIFY(!d.isEmpty());
            QVERIFY(!d.contains('\n'));
            QVERIFY2(d.contains(s.relPath), qPrintable(d));
            QVERIFY2(text.contains(d), qPrintable(text));
        }
        QCOMPARE(types.size(), 4);
        QVERIFY(text.contains(formatByteSize(p.bytesToAdd)));
    }
    void refusesWithoutSourceDb()
    {
        PlanInput in = input({}, allMics(true));
        in.source = {fi(kBTr, 100)};
        const Plan p = buildPlan(in);
        QVERIFY(!p.canRun());
        QVERIFY2(p.refusals.join(' ').contains("Spitfire library"), qPrintable(p.refusals.join(' ')));
    }
    void stagingAndToolFilesIgnored()
    {
        PlanInput in = input({fi(kBrassDb, 1000),
                              fi(".smt-staging/" + kBMo, 200),
                              fi(".smt-staging/" + kBTr, 100),
                              fi(".smt.lock", 1),
                              fi("project.smt", 2),
                              fi("Samples/_Brass/.DS_Store", 3)},
                             allMics(false));
        in.source << fi("Samples/_Brass/.DS_Store", 4) << fi(".DS_Store", 4);
        const Plan p = buildPlan(in);
        for (const auto &s : p.steps) {
            QVERIFY2(!s.relPath.startsWith(".smt"), qPrintable(s.relPath));
            QVERIFY2(!s.relPath.endsWith(".DS_Store"), qPrintable(s.relPath));
            QVERIFY(s.relPath != "project.smt");
            QVERIFY(!s.keepFileNames.contains(".DS_Store"));
        }
        QCOMPARE(p.bytesToFree, 0);
        QCOMPARE(p.bytesToAdd, 12); // B_ZZ 7 + inst 5
    }
    void mixedCaseMicSuffixHandled()
    {
        const QString lowerMo = "Samples/_Brass/b_mo01.spitfire";
        const QString lowerTr = "Samples/_Brass/b_tr01.spitfire";
        auto mics = allMics(true);
        mics["mo01"]["brass"] = false;
        PlanInput in = input({fi(kBrassDb, 1000), fi(lowerMo, 50)}, mics);
        in.source << fi(lowerMo, 50) << fi(lowerTr, 60);
        const Plan p = buildPlan(in);
        QCOMPARE(paths(p, T::DeleteFile, 1), QStringList{lowerMo});
        QVERIFY(paths(p, T::CopyToStaging, 2).contains(lowerTr));
        QVERIFY(!paths(p, T::CopyToStaging, 2).contains(lowerMo));
    }
    void dbOutsideSamplesIsCopiedNotPatched()
    {
        const QString otherDb = "Instruments/presets.db";
        PlanInput in = input({fi(kBrassDb, 1000), fi(otherDb, 40), fi("Extra/old.db", 9), fi(kBMo, 200)},
                             allMics(false));
        in.source << fi(otherDb, 40) << fi("Patches/new.db", 8);
        const Plan p = buildPlan(in);
        QVERIFY2(p.canRun(), qPrintable(p.refusals.join(' ')));
        QCOMPARE(paths(p, T::CopyToStaging, 2), (QStringList{kInst, "Patches/new.db", kBZz}));
        QCOMPARE(paths(p, T::ReplaceDb, 2), (QStringList{kBrassDb, kStringsDb}));
        QCOMPARE(paths(p, T::DeleteFile, 1), QStringList{kBMo});
        // only Samples/ databases make a library
        in.source = {fi(otherDb, 40), fi(kBTr, 100)};
        QVERIFY(!buildPlan(in).canRun());
    }
    void destOnlyOtherFileNeverTouched()
    {
        const QString extra = "Samples/_Brass/user_notes.spitfire";
        const QString extraMicLike = "Samples/_Misc/X_MO01.spitfire";
        const QList<FileInfo> dest = {fi(kBrassDb, 1000), fi(extra, 11), fi(extraMicLike, 12), fi(kBMo, 200)};
        const Plan p = buildPlan(input(dest, allMics(false)));
        for (const auto &s : p.steps) {
            QVERIFY(s.relPath != extra);
            QVERIFY(s.relPath != extraMicLike);
        }
        QCOMPARE(paths(p, T::DeleteFile, 1), QStringList{kBMo});
        QCOMPARE(p.bytesToFree, 200);
    }

    // Repair rewrites Patches/Presets/Config in place at the same size: those are copied again on every Apply.
    void settingsFilesAreRefreshedAtTheSameSizeButNothingElseIs()
    {
        const QString patch = "Patches/BBC/a.zmulti", preset = "Presets/p.zpreset", config = "Config/c.zconfig";
        const QString other = "Samples/_Brass/IR1.spitfire", nks = "NKS/x.nks", doc = "Documents/d.pdf";
        const QStringList settings{patch, preset, config};
        PlanInput in = input({}, allMics(true));
        in.source << fi(patch, 10) << fi(preset, 20) << fi(config, 30) << fi(other, 40) << fi(nks, 50) << fi(doc, 60)
                  << fi("patches/lower.zmulti", 70) << fi("Patchesx/y.zmulti", 80) << fi("Patches", 90);
        QList<FileInfo> dest = in.source; // everything already there at its size
        in.dest = dest;
        const Plan p = buildPlan(in);
        QCOMPARE(p.canRun(), true);
        QStringList copies = paths(p, T::CopyToStaging, 2), moves = paths(p, T::MoveIntoPlace, 2);
        copies.sort();
        moves.sort();
        QStringList want = settings;
        want.sort();
        QCOMPARE(copies, want); // not the mic file, the sample, NKS, Documents, other-case or look-alike names
        QCOMPARE(moves, want);
        QCOMPARE(p.bytesToAdd, qint64(60));
        QCOMPARE(p.bytesAlreadyStaged, qint64(0));
        for (const auto &st : p.steps)
            QCOMPARE(st.refresh, st.type != T::ReplaceDb); // every file step here is a settings file
        QCOMPARE(p.freeBytesAfter, in.freeBytes - 60 - p.bytesForDatabases);
        // switched-off mics change nothing about them: they are not mic files
        const Plan off = buildPlan([&] { PlanInput i2 = in; i2.mics = allMics(false); return i2; }());
        QStringList offCopies = paths(off, T::CopyToStaging, 2);
        offCopies.sort();
        QCOMPARE(offCopies, want);
        // a copy of the same size left in staging by an interrupted run is not trusted for them
        PlanInput withStaged = in;
        withStaged.dest << fi(".smt-staging/" + patch, 10);
        const Plan staged = buildPlan(withStaged);
        QCOMPARE(staged.bytesAlreadyStaged, qint64(0));
        // a missing one is copied as always, and a destination file the source lacks is left alone
        PlanInput missing = in;
        missing.dest.removeIf([&](const FileInfo &f) { return f.path == config; });
        missing.dest << fi("Patches/old-only.zmulti", 5);
        const Plan m = buildPlan(missing);
        QVERIFY(!m.steps.isEmpty());
        for (const auto &st : m.steps)
            QVERIFY(st.relPath != "Patches/old-only.zmulti");
        QCOMPARE(m.bytesToFree, qint64(0));
        // a plan made only of refreshes still has file changes
        QVERIFY(p.hasFileChanges());
    }
};

QTEST_APPLESS_MAIN(TstPlan)
#include "tst_plan.moc"
