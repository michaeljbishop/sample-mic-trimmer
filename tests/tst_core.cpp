#include <QtTest>
#include "unixonly.h"
#include "pathsafety.h"
#include "qfileinfogatherertask.h"
#include <QCryptographicHash>
#include "corelib.h"
#include "fakelibrary.h"
#include "librarydb.h"
#include "pathsafety_testing.h"
#include "profileregistry.h"
#include "projectconfig.h"
#include "quickset.h"

using namespace smt;

Q_DECLARE_METATYPE(smt::Plan)
Q_DECLARE_METATYPE(PendingChange)

static const QStringList kTokens{"TR01", "MO01"};
static const QStringList kSections{"brass", "woodwinds"};
static const int kInstruments = 2;
static const qint64 kFileSize = 1000;

static bool waitFor(QSignalSpy &spy)
{
    return spy.count() > 0 || spy.wait(10000);
}

static QStringList sortedPaths(const QString &root)
{
    QStringList out;
    for (const auto &f : scanTree(root))
        out << f.path;
    out.sort();
    return out;
}

static QStringList sortedContainers(const QString &db)
{
    QStringList names = librarydb::containerNames(db);
    names.sort();
    return names;
}

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    // JSON escapes a Windows backslash as two: read them as "/" so the expectations hold on both platforms
    return f.open(QIODevice::ReadOnly) ? f.readAll().replace("\\\\", "/") : QByteArray();
}

// Every entry under root (directories too) with size and mtime.
static QStringList snapshot(const QString &root)
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

static void writeCorruptConfig(const QString &dir)
{
    QDir().mkpath(dir);
    QFile f(dir + "/project.smt");
    if (!f.open(QIODevice::WriteOnly))
        qFatal("cannot write config");
    f.write("{not json");
}

class TstCore : public QObject
{
    Q_OBJECT

    QTemporaryDir mTmp;
    QString mSrc, mDst;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true); // never touch the real source cache
    }

    void init()
    {
        QVERIFY(mTmp.isValid());
        mSrc = makeFakeLibrary(mTmp.filePath("src"), kTokens, kSections, kInstruments, kFileSize);
        mDst = mTmp.filePath("dst/lib"); // not created yet
        // Same source path in every test: drop the previous test's scan cache.
        // Core uses $SMT_APP_DATA_DIR when set, else the (test-mode) standard location.
        const QString dataDir = qEnvironmentVariable("SMT_APP_DATA_DIR",
                QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
        QDir(dataDir + "/ScanCache").removeRecursively();
    }

    void cleanup()
    {
        testing::setMountIdProvider({});
        QDir(mTmp.path()).removeRecursively();
        QDir().mkpath(mTmp.path());
    }

    void validUserOverrideOfBbcsoIsTheDefaultProfile()
    {
        QTemporaryDir data;
        const QByteArray old = qgetenv("SMT_APP_DATA_DIR");
        const bool had = qEnvironmentVariableIsSet("SMT_APP_DATA_DIR");
        qputenv("SMT_APP_DATA_DIR", data.path().toUtf8());
        const QString root = data.path() + "/Profiles/Mine.smtprofile/Contents";
        QVERIFY(QDir().mkpath(root + "/Resources"));
        auto put = [&](const QString &rel, const QByteArray &bytes) {
            QFile f(root + "/" + rel);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(bytes);
        };
        put("Info.plist", "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
                          "<key>CFBundleIdentifier</key><string>" + kBbcsoProfileId.toUtf8() + "</string>"
                          "<key>CFBundleName</key><string>My BBCSO</string>"
                          "<key>SMTProfileFormat</key><string>1</string>"
                          "<key>SMTProfileLibrary</key><string>library.json</string>"
                          "<key>SMTProfileMics</key><string>mics.json</string>"
                          "<key>SMTProfileGroups</key><string>groups.json</string></dict></plist>");
        put("Resources/library.json", "{\"folderPattern\":\"^User\",\"samplesDir\":\"Samples\","
                                      "\"sampleFile\":\"_(?<mic>[a-z]{2}\\\\d{2})\\\\.spitfire$\"}");
        put("Resources/mics.json", "{\"mics\":[],\"families\":[]}");
        put("Resources/groups.json", "{\"groups\":[]}");
        {
            Core core;
            QCOMPARE(core.profile().name, "My BBCSO");
        }
        had ? (void)qputenv("SMT_APP_DATA_DIR", old) : (void)qunsetenv("SMT_APP_DATA_DIR");
    }

    void scanFinishedEmittedOnce()
    {
        Core core;
        QSignalSpy spy(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        QTest::qWait(200); // source done, no destination yet
        QCOMPARE(spy.count(), 0);
        core.setDestinationDirectoryPath(mTmp.filePath("other"));
        core.setDestinationDirectoryPath(mDst); // the first destination scan is abandoned
        QVERIFY(waitFor(spy));
        QTest::qWait(200);
        QCOMPARE(spy.count(), 1);

        core.rescanSource();
        QVERIFY(spy.wait(10000));
        QTest::qWait(200);
        QCOMPARE(spy.count(), 2);
    }

    void rescanSourcePicksUpNewFiles()
    {
        const QString added = "Samples/_Brass/NEW1_TR01_01.spitfire";
        auto copies = [&](const Plan &p) {
            QStringList out;
            for (const auto &s : p.steps)
                if (s.type == PlanStep::Type::CopyToStaging)
                    out << s.relPath;
            return out;
        };
        {
            Core core; // fills the scan cache
            QSignalSpy scan(&core, &Core::scanFinished);
            core.setSourceDir(mSrc);
            core.setDestinationDirectoryPath(mDst);
            QVERIFY(waitFor(scan));
        }
        writeFile(mSrc + "/" + added, 10);
        Core core;
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
        QVERIFY(!copies(core.buildPlan()).contains(added)); // the cached scan
        core.rescanSource();
        QVERIFY(scan.wait(10000));
        QVERIFY(copies(core.buildPlan()).contains(added));
    }

    void buildPlanRefusesBeforeScanFinishes()
    {
        Core core;
        QSignalSpy spy(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        const Plan early = core.buildPlan();
        QVERIFY(!early.canRun());
        QVERIFY(early.isEmpty());
        QVERIFY2(early.refusals.join('\n').contains("scan not finished"), qPrintable(early.refusals.join('\n')));
        QVERIFY(waitFor(spy));
        const Plan plan = core.buildPlan();
        QVERIFY2(plan.canRun(), qPrintable(plan.refusals.join('\n')));
        QVERIFY(plan.hasFileChanges());
    }

    void changingDestRescans()
    {
        const QString full = makeFakeLibrary(mTmp.filePath("full"), kTokens, kSections, kInstruments, kFileSize);
        Core core;
        QSignalSpy spy(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(full);
        QVERIFY(waitFor(spy));
        QVERIFY(!core.buildPlan().hasFileChanges());

        spy.clear();
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(!core.buildPlan().canRun()); // new destination scan is running
        QVERIFY(waitFor(spy));
        const Plan plan = core.buildPlan();
        QVERIFY2(plan.canRun(), qPrintable(plan.refusals.join('\n')));
        QVERIFY(plan.hasFileChanges());
    }

    void profileMismatchEmitsConfigurationErrorAndKeepsFile()
    {
        QDir().mkpath(mDst);
        {
            Core c;
            c.setDestinationDirectoryPath(mDst);
            QVERIFY(c.saveConfiguration()); // a BBCSO file
        }
        const QByteArray before = readAll(mDst + "/project.smt");
        Core core;
        QSignalSpy error(&core, &Core::configurationError);
        core.setDestinationDirectoryPath(mDst);
        QCOMPARE(error.count(), 0);
        QVERIFY(core.setProfileById(kEwcProfileId));
        QCOMPARE(error.count(), 1);
        QVERIFY(!core.saveConfiguration());
        QCOMPARE(readAll(mDst + "/project.smt"), before);
    }

    void clearingDestinationBeforeSwitchRaisesNoConfigurationError()
    {
        QDir().mkpath(mDst);
        {
            Core c;
            c.setDestinationDirectoryPath(mDst);
            QVERIFY(c.saveConfiguration()); // a BBCSO file
        }
        Core core;
        core.setDestinationDirectoryPath(mDst);
        QSignalSpy error(&core, &Core::configurationError);
        core.setDestinationDirectoryPath({});
        QVERIFY(core.setProfileById(kEwcProfileId));
        core.setDestinationDirectoryPath(mTmp.filePath("whitDst"));
        QCOMPARE(error.count(), 0);
    }

    void setProfileByIdRejectsUnknown()
    {
        Core core;
        QSignalSpy changed(&core, &Core::profileChanged);
        QVERIFY(!core.setProfileById("no.such.profile"));
        QCOMPARE(changed.count(), 0);
        QCOMPARE(core.profile().id, kBbcsoProfileId);
        QVERIFY(core.setProfileById(kEwcProfileId));
        QCOMPARE(core.profile().id, kEwcProfileId);
    }

    void coreSuggestsProfileFromSourceFolderName()
    {
        const QString whit = mTmp.filePath("Spitfire Audio - Eric Whitacre Choir 2");
        QVERIFY(QDir().mkpath(whit));
        Core core;
        QVERIFY(core.availableProfiles().size() >= 2);
        QVERIFY(core.profileSuggestion().isEmpty());
        core.setSourceDir(whit);
        QCOMPARE(core.profileSuggestion(), kEwcProfileId);
        QCOMPARE(core.profile().id, kEwcProfileId);
    }

    void coreKeepsExplicitChoice()
    {
        const QString whit = mTmp.filePath("Spitfire Audio - Eric Whitacre Choir 2");
        QVERIFY(QDir().mkpath(whit));
        Core core;
        QVERIFY(core.setProfileById(kBbcsoProfileId));
        core.setSourceDir(whit);
        QCOMPARE(core.profileSuggestion(), kEwcProfileId);
        QCOMPARE(core.profile().id, kBbcsoProfileId);
    }

    void switchingProfileReloadsMatchingConfig_data()
    {
        QTest::addColumn<bool>("viaSourceFolder");
        QTest::newRow("setProfileById") << false;
        QTest::newRow("suggestion") << true;
    }

    void switchingProfileReloadsMatchingConfig()
    {
        QFETCH(bool, viaSourceFolder);
        const ProductProfile w = builtInProfile(kEwcProfileId);
        QDir().mkpath(mDst);
        MicStatuses mics;
        mics["cl01"]["tutti"] = false;
        mics["mi01"]["tutti"] = false;
        QString err;
        QVERIFY2(saveProjectConfig(mDst, w, mics, &err), qPrintable(err));
        const QString whit = mTmp.filePath("Spitfire Audio - Eric Whitacre Choir");
        QVERIFY(QDir().mkpath(whit));

        Core core; // BBCSO until switched
        QSignalSpy error(&core, &Core::configurationError);
        core.setDestinationDirectoryPath(mDst);
        QCOMPARE(error.count(), 1); // a Whitacre file under BBCSO
        QSignalSpy loaded(&core, &Core::configurationLoaded);
        if (viaSourceFolder)
            core.setSourceDir(whit);
        else
            QVERIFY(core.setProfileById(kEwcProfileId));
        QCOMPARE(core.profile().id, kEwcProfileId);
        QCOMPARE(error.count(), 1);
        QCOMPARE(loaded.count(), 1);
        QVERIFY(!core.isMicEnabled({"cl01", "tutti"}));
        QVERIFY(!core.isMicEnabled({"mi01", "tutti"})); // nothing is locked in Whitacre
    }

    void quickSetSelectsFamiliesPlusLocked()
    {
        QTemporaryDir bundle;
        Core core;
        QVERIFY(core.profile().quickSets.isEmpty()); // none ships
        core.setProfile(profileWithQuickSet(kBbcsoProfileId, bundle));
        QSignalSpy changed(&core, &Core::micEnabledChanged);
        core.applyQuickSet(99);
        core.applyQuickSet(-1);
        QCOMPARE(changed.count(), 0);
        core.applyQuickSet(0);
        for (const QString &group : {"brass", "strings"}) {
            for (const QString &mic : {"cl01", "tr01", "am01", "ou01", "mi01"})
                QVERIFY2(core.isMicEnabled({mic, group}), qPrintable(mic + "/" + group));
            QVERIFY(!core.isMicEnabled({"cw01", group}));
        }
    }

    void setProfileRebuildsAccounting()
    {
        QDir().mkpath(mDst);
        QFile f(mDst + "/project.smt");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"micSignals\":{\"cl01\":{\"tutti\":false},\"tr01\":{\"brass\":false}}}");
        f.close();
        Core core;
        QCOMPARE(core.profile().id, kBbcsoProfileId);
        QSignalSpy scan(&core, &Core::sourceScanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
        // everything, the switched-off mic included: only tr01/brass is on disk and off (cl01/tutti is an unknown key)
        const size_t total = core.totalSpaceForSelectedItems() + core.spaceRequired({"tr01", "brass"});
        QCOMPARE(core.spaceRequired({"tr01", "brass"}), size_t(kInstruments * kFileSize));
        QVERIFY(!core.isMicEnabled({"tr01", "brass"}));
        QVERIFY(!core.isMicEnabled({"cl01", "tutti"})); // no tutti group in BBCSO: unknown key

        QSignalSpy changed(&core, &Core::profileChanged);
        QSignalSpy mismatch(&core, &Core::configurationError);
        scan.clear();
        core.setProfile(builtInProfile(kEwcProfileId));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(core.profile().id, kEwcProfileId);
        QVERIFY(waitFor(scan));
        QCOMPARE(core.spaceRequired({"tr01", "brass"}), size_t(0)); // BBCSO files are all "other" now
        QCOMPARE(core.totalSpaceForSelectedItems(), total);
        // The BBCSO file does not fit the Whitacre profile: refused, defaults in memory.
        QCOMPARE(mismatch.count(), 1);
        QVERIFY(core.isMicEnabled({"cl01", "tutti"}));
        QVERIFY(core.isMicEnabled({"cl01", "altos"}));
        QVERIFY(core.isMicEnabled({"tr01", "tutti"}));
    }

    void lockedMicCannotBeSwitchedOff()
    {
        Core core;
        QSignalSpy changed(&core, &Core::micEnabledChanged);
        core.setMicEnabled({"mi01", "brass"}, false);
        QVERIFY(core.isMicEnabled({"mi01", "brass"}));
        core.setMicEnabled({"zz01", "brass"}, false); // unknown to the profile
        QCOMPARE(changed.count(), 0);
    }

    void anyMicEnabledNeedsOneMic()
    {
        Core core;
        QVERIFY(core.anyMicEnabled()); // BBCSO: Full Mix 1 is locked on
        disableAllMics(core);
        QVERIFY(core.anyMicEnabled());
        QVERIFY(core.setProfileById(kEwcProfileId));
        disableAllMics(core);
        QVERIFY(!core.anyMicEnabled());
        core.setMicEnabled({"gal01", "tutti"}, true); // unknown mic: ignored
        QVERIFY(!core.anyMicEnabled());
        core.setMicEnabled({"ga01", "tutti"}, true);
        QVERIFY(core.anyMicEnabled());
    }

    void followBuildPlanRefusesWithoutAMic()
    {
        const QString none = QStringLiteral("Select at least one microphone.");
        Core core;
        core.setSelectionFollowsDestination(true);
        QVERIFY(core.setProfileById(kEwcProfileId));
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
        QVERIFY(!core.anyMicEnabled());
        QVERIFY(core.buildPlan().refusals.contains(none));
        core.setMicEnabled({"cl01", "tutti"}, true);
        QVERIFY(!core.buildPlan().refusals.contains(none));
    }

    void defaultModeBuildPlanDoesNotNeedAMic()
    {
        Core core; // CLI mode
        QVERIFY(core.setProfileById(kEwcProfileId));
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
        disableAllMics(core);
        QVERIFY(!core.anyMicEnabled());
        QVERIFY(!core.buildPlan().refusals.contains(QStringLiteral("Select at least one microphone.")));
    }

    void configLivesInDestination()
    {
        const QString a = mTmp.filePath("a"), b = mTmp.filePath("b");
        QDir().mkpath(a);
        QDir().mkpath(b);
        const MicSignalGroupKey key{"mo01", "brass"};
        Core core;
        core.setDestinationDirectoryPath(a);
        QVERIFY(core.isMicEnabled(key));
        core.setMicEnabled(key, false);
        QVERIFY(core.saveConfiguration());
        QVERIFY(QFileInfo::exists(a + "/project.smt"));

        QSignalSpy changed(&core, &Core::micEnabledChanged);
        core.setDestinationDirectoryPath(b);
        QVERIFY(core.isMicEnabled(key)); // b has no config: all enabled
        QCOMPARE(changed.count(), 1);
        QVERIFY(!QFileInfo::exists(b + "/project.smt"));

        core.setDestinationDirectoryPath(a);
        QVERIFY(!core.isMicEnabled(key));

        // Saving never creates the destination root.
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(!core.saveConfiguration());
        QVERIFY(!QFileInfo::exists(mDst));
    }

    void shortfallIsNegativeNotWrapped()
    {
        // Sparse: a petabyte "mic" file that takes no real space.
        QFile huge(mSrc + "/Samples/_Brass/INSTR9_TR01_01.spitfire");
        QVERIFY(huge.open(QIODevice::WriteOnly));
        if (!huge.resize(qint64(1) << 50))
            QSKIP("filesystem does not support sparse files");
        huge.close();

        Core core;
        qint64 remaining = 0;
        connect(&core, &Core::remainingSpaceChanged, this, [&](qint64 r) { remaining = r; });
        QSignalSpy spy(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst); // missing: free space comes from its parent
        QVERIFY(waitFor(spy));
        QVERIFY2(remaining < 0, qPrintable(QString::number(remaining)));
        const Plan plan = core.buildPlan();
        QVERIFY(!plan.canRun());
        QVERIFY(plan.freeBytesAfter < 0);
        QVERIFY(plan.refusals.join('\n').contains("Not enough free space"));
    }

    void startApplyEndToEnd()
    {
        Core core;
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        for (const auto &s : kSections)
            core.setMicEnabled({"mo01", s}, false);
        QVERIFY(waitFor(scan));

        const Plan plan = core.buildPlan();
        QVERIFY2(plan.canRun(), qPrintable(plan.refusals.join('\n')));
        int done = 0, progress = 0;
        connect(&core, &Core::applyStepChanged, this, [&](int, PlanStep::Status s, const QString &) {
            done += s == PlanStep::Status::Done;
        });
        connect(&core, &Core::applyProgress, this, [&](qint64, qint64, double) { ++progress; });
        QSignalSpy fin(&core, &Core::applyFinished);
        core.startApply(plan);
        QVERIFY(waitFor(fin));
        QVERIFY2(fin.first().first().toBool(), qPrintable(fin.first().at(1).toString()));
        QCOMPARE(done, plan.steps.size());
        QVERIFY(progress > 0);

        // Tree: every source file except MO01 mics, plus the config.
        QStringList expected{"project.smt"};
        for (const auto &f : scanTree(mSrc))
            if (classify(core.profile(), f.path).kind != FileKind::Mic || !f.path.contains("_MO01_"))
                expected << f.path;
        expected.sort();
        QCOMPARE(sortedPaths(mDst), expected);

        // DBs: the source rows minus MO01.
        int dbs = 0;
        for (const auto &f : scanTree(mSrc)) {
            if (!f.path.endsWith(".db"))
                continue;
            ++dbs;
            QStringList want;
            for (const auto &name : sortedContainers(mSrc + "/" + f.path))
                if (!name.contains("_MO01_"))
                    want << name;
            QCOMPARE(sortedContainers(mDst + "/" + f.path), want);
        }
        QCOMPARE(dbs, 3);

        const auto saved = loadProjectConfig(mDst, core.profile(), nullptr);
        QVERIFY(saved);
        QVERIFY(!micEnabled(core.profile(), *saved, {"mo01", "brass"}));
        QVERIFY(micEnabled(core.profile(), *saved, {"tr01", "brass"}));
    }

    void corruptConfigReportedNotOverwritten()
    {
        QDir().mkpath(mDst);
        const QString config = mDst + "/project.smt";
        {
            QFile f(config);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{not json");
        }
        Core core;
        QSignalSpy error(&core, &Core::configurationError);
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QCOMPARE(error.count(), 1);
        const MicSignalGroupKey key{"mo01", "brass"};
        QVERIFY(core.isMicEnabled(key)); // defaults in memory
        core.setMicEnabled(key, false);
        QVERIFY(!core.saveConfiguration());
        QCOMPARE(readAll(config), QByteArray("{not json"));

        QVERIFY(waitFor(scan));
        const Plan plan = core.buildPlan();
        QVERIFY(!plan.canRun());
        QSignalSpy fin(&core, &Core::applyFinished);
        QVERIFY(core.startApply(plan));
        QVERIFY(waitFor(fin));
        QVERIFY(!fin.first().first().toBool());
        QCOMPARE(readAll(config), QByteArray("{not json"));
        QCOMPARE(sortedPaths(mDst), QStringList{"project.smt"});
    }

    void configurationLoadedOnlyWhenClean()
    {
        writeCorruptConfig(mDst);
        Core core;
        QSignalSpy loaded(&core, &Core::configurationLoaded);
        QSignalSpy error(&core, &Core::configurationError);
        core.setDestinationDirectoryPath(mDst);
        QCOMPARE(error.count(), 1);
        QCOMPARE(loaded.count(), 0);

        QVERIFY(QFile::remove(mDst + "/project.smt")); // fixed: missing means all enabled
        core.setDestinationDirectoryPath(mDst);         // re-chosen: reloads
        QCOMPARE(loaded.count(), 1);
        QCOMPARE(error.count(), 1);
        core.setDestinationDirectoryPath(mTmp.filePath("other"));
        QCOMPARE(loaded.count(), 2);
    }

    void emptySourceDoesNotScanCwd()
    {
        const QString oldCwd = QDir::currentPath();
        Core core;
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
        QVERIFY(core.totalSpaceForSelectedItems() > 0);

        QVERIFY(QDir::setCurrent(mSrc)); // a library in the cwd would be found by QDir("")
        const auto restoreCwd = qScopeGuard([&] { QDir::setCurrent(oldCwd); });
        scan.clear();
        core.setSourceDir("");
        QCOMPARE(core.totalSpaceForSelectedItems(), size_t(0));
        QTest::qWait(200);
        QCOMPARE(core.totalSpaceForSelectedItems(), size_t(0));
        QCOMPARE(scan.count(), 0); // no source scan, so the scans never finish

        core.setSourceDir(mSrc);
        QVERIFY(waitFor(scan));
        scan.clear();
        core.setDestinationDirectoryPath("");
        QTest::qWait(200);
        QCOMPARE(scan.count(), 0);
        core.setSourceDir("");
        const Plan plan = core.buildPlan();
        QVERIFY(!plan.canRun());
        QVERIFY(plan.isEmpty());
    }

    void afterApplyDestIsRescanned()
    {
        Core core;
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
        const Plan plan = core.buildPlan();
        QVERIFY(plan.hasFileChanges());

        scan.clear();
        QSignalSpy fin(&core, &Core::applyFinished);
        bool rescanUnderWay = false;
        connect(&core, &Core::applyFinished, this, [&] { rescanUnderWay = !core.buildPlan().canRun(); });
        core.startApply(plan);
        QVERIFY(waitFor(fin));
        QVERIFY2(fin.first().first().toBool(), qPrintable(fin.first().at(1).toString()));
        QVERIFY(rescanUnderWay);
        QVERIFY(waitFor(scan));
        const Plan after = core.buildPlan();
        QVERIFY2(after.canRun(), qPrintable(after.refusals.join('\n')));
        QVERIFY(!after.hasFileChanges());
    }

    void refusalWhenDestInsideSource()
    {
        const QString inside = mSrc + "/out";
        Core core;
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(inside);
        QVERIFY(waitFor(scan));
        const Plan plan = core.buildPlan();
        QVERIFY(!plan.canRun());
        QVERIFY(plan.isEmpty());

        QSignalSpy fin(&core, &Core::applyFinished);
        core.startApply(plan);
        QCOMPARE(fin.count(), 0); // reported asynchronously
        QVERIFY(waitFor(fin));
        QVERIFY(!fin.first().first().toBool());
        QVERIFY(!QFileInfo::exists(inside));
    }
    void planRefusedWhenSourceHasLockFile()
    {
        SMT_UNIX_ONLY();
        QDir().mkpath(mDst);
        writeFile(mDst + "/.smt.lock", 1);
        QVERIFY(QFile::link(mDst + "/.smt.lock", mSrc + "/.smt.lock")); // the destination's lock seen from the source
        Core core;
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
        const Plan plan = core.buildPlan();
        QVERIFY(!plan.canRun());
        QVERIFY2(plan.refusals.join(' ').contains(".smt.lock"), qPrintable(plan.refusals.join(' ')));
    }
    void staleDestinationPlanRefusedAndSourceUntouched()
    {
        for (const QString &bad : {mSrc + "/x", mSrc}) {
            Core core;
            QSignalSpy scan(&core, &Core::scanFinished);
            core.setSourceDir(mSrc);
            core.setDestinationDirectoryPath(mDst);
            QVERIFY(waitFor(scan));
            const Plan plan = core.buildPlan();
            QVERIFY(plan.canRun());

            const QStringList before = snapshot(mSrc);
            scan.clear();
            core.setDestinationDirectoryPath(bad);
            QVERIFY(waitFor(scan));
            QSignalSpy fin(&core, &Core::applyFinished);
            QVERIFY(core.startApply(plan));
            QVERIFY(waitFor(fin));
            QVERIFY(!fin.first().first().toBool());
            QCOMPARE(snapshot(mSrc), before);
            QVERIFY(!QFileInfo::exists(mSrc + "/project.smt"));
            QVERIFY(!QFileInfo::exists(mDst));
        }
    }

    void saveConfigurationRefusesInsideSource()
    {
        Core core;
        core.setSourceDir(mSrc);
        for (const QString &dest : {mSrc, mSrc + "/Samples"}) {
            core.setDestinationDirectoryPath(dest);
            QString error;
            QVERIFY(!core.saveConfiguration(&error));
            QVERIFY(!error.isEmpty());
            QVERIFY(!QFileInfo::exists(dest + "/project.smt"));
        }
    }

    void planRefusedAfterSourceChange()
    {
        const QString src2 = makeFakeLibrary(mTmp.filePath("src2"), kTokens, kSections, kInstruments, kFileSize);
        Core core;
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
        Plan plan = core.buildPlan();
        QVERIFY(plan.canRun());

        scan.clear();
        core.setSourceDir(src2);
        QVERIFY(waitFor(scan));
        QSignalSpy fin(&core, &Core::applyFinished);
        QVERIFY(core.startApply(plan));
        QVERIFY(waitFor(fin));
        QVERIFY(!fin.first().first().toBool());
        QVERIFY2(fin.first().at(1).toString().contains("out of date"), qPrintable(fin.first().at(1).toString()));

        plan = core.buildPlan();
        QVERIFY(plan.canRun());
        scan.clear();
        core.rescanSource();
        QVERIFY(waitFor(scan));
        fin.clear();
        QVERIFY(core.startApply(plan));
        QVERIFY(waitFor(fin));
        QVERIFY(!fin.first().first().toBool());
        QVERIFY(!QFileInfo::exists(mDst));
    }

    void planRefusedAfterMicToggle()
    {
        Core core;
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
        const Plan plan = core.buildPlan();
        QVERIFY(plan.canRun());
        core.setMicEnabled({"mo01", "brass"}, false);
        QSignalSpy fin(&core, &Core::applyFinished);
        QVERIFY(core.startApply(plan));
        QVERIFY(waitFor(fin));
        QVERIFY(!fin.first().first().toBool());
        QVERIFY(fin.first().at(1).toString().contains("out of date"));
        QVERIFY(!QFileInfo::exists(mDst));
    }

    void buildPlanRefusesWhileApplying()
    {
        Core core;
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
        QSignalSpy fin(&core, &Core::applyFinished);
        QVERIFY(core.startApply(core.buildPlan()));
        const Plan during = core.buildPlan();
        QVERIFY(!during.canRun());
        QVERIFY(during.isEmpty());
        QVERIFY2(during.refusals.join('\n').contains("apply is running", Qt::CaseInsensitive),
                 qPrintable(during.refusals.join('\n')));
        QVERIFY(waitFor(fin));
        QVERIFY(fin.first().first().toBool());
    }

    void startApplyWhileRunningReturnsFalseWithoutSignal()
    {
        Core core;
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
        const Plan plan = core.buildPlan();
        QSignalSpy fin(&core, &Core::applyFinished);
        QVERIFY(core.startApply(plan));
        QVERIFY(!core.startApply(plan));
        QVERIFY(waitFor(fin));
        QTest::qWait(100);
        QCOMPARE(fin.count(), 1);
        QVERIFY(fin.first().first().toBool());
    }

    void corruptConfigIsPlanRefusal()
    {
        writeCorruptConfig(mDst);
        Core core;
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
        const Plan plan = core.buildPlan();
        QVERIFY(!plan.canRun());
        QVERIFY(plan.isEmpty());
        QVERIFY2(plan.refusals.join('\n').contains("project.smt"), qPrintable(plan.refusals.join('\n')));
    }

    void sameDestReloadClearsCorruptFlag()
    {
        writeCorruptConfig(mDst);
        Core core;
        QSignalSpy error(&core, &Core::configurationError);
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
        QCOMPARE(error.count(), 1);
        QVERIFY(!core.saveConfiguration());

        const MicSignalGroupKey key{"mo01", "brass"};
        MicStatuses fixed;
        fixed[key.mic][key.group] = false;
        QVERIFY(saveProjectConfig(mDst, core.profile(), fixed, nullptr)); // the user fixes the file
        scan.clear();
        core.setDestinationDirectoryPath(mDst); // and picks the same folder again
        QVERIFY(!core.isMicEnabled(key));
        QCOMPARE(error.count(), 1);
        QVERIFY(core.saveConfiguration());
        QVERIFY(core.buildPlan().canRun()); // no rescan was needed
        QTest::qWait(200);
        QCOMPARE(scan.count(), 0);
    }
    void startApplyDoesNotWriteConfigBeforeExecutorAccepts()
    {
        SMT_UNIX_ONLY();
        Core core;
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        core.setMicEnabled({"mo01", "brass"}, false);
        QVERIFY(waitFor(scan));
        const Plan plan = core.buildPlan();
        QVERIFY2(plan.canRun(), qPrintable(plan.refusals.join('\n')));
        // After the scan, so it is not a file to copy: the source's lock resolves to the lock
        // the executor takes in the destination, as with a second mount of the source.
        const QString link = mSrc + "/.smt.lock";
        QVERIFY(QFile::link(mTmp.filePath("dst/lib/.smt.lock"), link));

        QSignalSpy fin(&core, &Core::applyFinished);
        QVERIFY(core.startApply(plan));
        QVERIFY(waitFor(fin));
        QVERIFY(!fin.first().first().toBool());
        QVERIFY2(fin.first().at(1).toString().contains(".smt.lock"), qPrintable(fin.first().at(1).toString()));
        QVERIFY(!QFileInfo::exists(mDst + "/project.smt"));
        QVERIFY(!QFileInfo::exists(mTmp.filePath("dst"))); // created for the lock, removed again

        QVERIFY(QFile::remove(link));
        QVERIFY(scan.count() > 1 || scan.wait(10000)); // the destination is rescanned after an apply
        const Plan again = core.buildPlan();
        QVERIFY2(again.canRun(), qPrintable(again.refusals.join('\n')));
        fin.clear();
        QVERIFY(core.startApply(again));
        QVERIFY(waitFor(fin));
        QVERIFY2(fin.first().first().toBool(), qPrintable(fin.first().at(1).toString()));
        const auto saved = loadProjectConfig(mDst, core.profile(), nullptr);
        QVERIFY(saved);
        QVERIFY(QFileInfo::exists(mDst + "/project.smt"));
        QVERIFY(!micEnabled(core.profile(), *saved, {"mo01", "brass"}));
    }
    // mTmp/alias is the source's share by a second mount (as /Volumes/Share-1 would be).
    void secondMountAliasRefusedByBuildPlanAndStartApply()
    {
        SMT_UNIX_ONLY();
        const QString alias = mTmp.filePath("alias");
        QVERIFY(QDir().mkpath(alias));
        const QString dst = alias + "/new/out";
        const QString share = QFileInfo(mTmp.path()).canonicalFilePath();
        const QString aliasCanon = QFileInfo(alias).canonicalFilePath();
        const auto fake = [share, aliasCanon](const QString &path) -> std::optional<testing::MountId> {
            if (path == aliasCanon || path.startsWith(aliasCanon + '/'))
                return testing::MountId{"//u@srv/Share", "src" + path.mid(aliasCanon.size())};
            if (path == share || path.startsWith(share + '/'))
                return testing::MountId{"//u@srv/Share", path.mid(share.size() + 1)};
            return std::nullopt;
        };
        const QStringList srcBefore = snapshot(mSrc), aliasBefore = snapshot(alias);

        Core core;
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(dst);
        QVERIFY(waitFor(scan));
        const Plan accepted = core.buildPlan(); // before the alias is known: a runnable plan
        QVERIFY2(accepted.canRun(), qPrintable(accepted.refusals.join('\n')));

        testing::setMountIdProvider(fake);
        const Plan refused = core.buildPlan();
        QVERIFY(!refused.canRun());
        QVERIFY2(refused.refusals.join('\n').contains("mounted twice"), qPrintable(refused.refusals.join('\n')));
        QSignalSpy fin(&core, &Core::applyFinished);
        QVERIFY(core.startApply(accepted));
        QVERIFY(waitFor(fin));
        QVERIFY(!fin.first().first().toBool());
        QVERIFY2(fin.first().at(1).toString().contains("mounted twice"), qPrintable(fin.first().at(1).toString()));

        QCOMPARE(snapshot(mSrc), srcBefore);
        QCOMPARE(snapshot(alias), aliasBefore);
        QVERIFY(!QFileInfo::exists(alias + "/new"));
        QVERIFY(!QFileInfo::exists(mSrc + "/project.smt"));
        QVERIFY(!QFileInfo::exists(dst + "/project.smt"));
    }

    // --- selection follows the destination (GUI mode) ---

    static void copySectionMic(const QString &src, const QString &dst, const QString &token, const QString &section)
    {
        const QString rel = fakeSectionDir(section);
        QDir().mkpath(dst + "/" + rel);
        for (const auto &f : scanTree(src))
            if (f.path.startsWith(rel + "/") && f.path.contains("_" + token + "_"))
                QVERIFY(QFile::copy(src + "/" + f.path, dst + "/" + f.path));
    }

    static QStringList enabledKeys(const Core &core)
    {
        QStringList out;
        forAllMicSignalGroupKeys(core.profile(), [&](const MicSignalGroupKey &k) {
            if (core.isMicEnabled(k))
                out << k.mic + "/" + k.group;
        });
        out.sort();
        return out;
    }

    static QStringList lockedKeys(const Core &core)
    {
        QStringList out;
        forAllMicSignalGroupKeys(core.profile(), [&](const MicSignalGroupKey &k) {
            if (core.profile().mic(k.mic)->locked)
                out << k.mic + "/" + k.group;
        });
        out.sort();
        return out;
    }

    void followDefaultOffKeepsAllEnabled()
    {
        Core core;
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
        QVERIFY(core.isMicEnabled({"tr01", "brass"}));
        QVERIFY(core.isMicEnabled({"mo01", "woodwinds"}));
    }

    void followEmptyDestinationChecksOnlyLockedMics()
    {
        Core core;
        core.setSelectionFollowsDestination(true);
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst); // missing folder
        QVERIFY(waitFor(scan));
        QCOMPARE(enabledKeys(core), lockedKeys(core));
        QVERIFY(!lockedKeys(core).isEmpty());
        QVERIFY(core.isMicEnabled({"mi01", "brass"}));
        core.setDestinationDirectoryPath(QString());
        QCOMPARE(enabledKeys(core), lockedKeys(core));
    }

    // Starts a follow-mode Core on mSrc and dst and waits for both scans.
    void startFollow(Core &core, const QString &dst)
    {
        core.setSelectionFollowsDestination(true);
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(dst);
        QVERIFY(waitFor(scan));
    }

    static int countSteps(const Plan &plan, PlanStep::Type type, const QString &needle)
    {
        int n = 0;
        for (const auto &st : plan.steps)
            n += st.type == type && st.relPath.contains(needle);
        return n;
    }

    void followCompleteMicIsEnabledAndComplete()
    {
        copySectionMic(mSrc, mDst, "TR01", "brass");
        Core core;
        startFollow(core, mDst);
        QStringList expected = lockedKeys(core);
        expected << "tr01/brass";
        expected.sort();
        QCOMPARE(enabledKeys(core), expected);
        QVERIFY(core.micCompleteness({"tr01", "brass"}) == Completeness::Complete);
        QVERIFY(core.micCompleteness({"tr01", "woodwinds"}) == Completeness::Empty);
        QVERIFY(core.micCompleteness({"mo01", "brass"}) == Completeness::Empty);
    }

    void followPartialMicStaysEnabledAndPlanDeletesNothing()
    {
        copySectionMic(mSrc, mDst, "TR01", "brass");
        QVERIFY(QFile::remove(mDst + "/" + fakeSectionDir("brass") + "/INSTR1_TR01_01.spitfire")); // interrupted copy
        Core core;
        startFollow(core, mDst);
        const MicSignalGroupKey key{"tr01", "brass"};
        QVERIFY(core.isMicEnabled(key));
        QVERIFY(core.micCompleteness(key) == Completeness::Partial);

        const Plan plan = core.buildPlan();
        QVERIFY2(plan.canRun(), qPrintable(plan.refusals.join('\n')));
        QCOMPARE(countSteps(plan, PlanStep::Type::DeleteFile, "_TR01_"), 0);
        QCOMPARE(countSteps(plan, PlanStep::Type::CopyToStaging, "INSTR1_TR01_01"), 1);

        QSignalSpy fin(&core, &Core::applyFinished);
        QVERIFY(core.startApply(plan));
        QVERIFY(waitFor(fin));
        QVERIFY2(fin.first().first().toBool(), qPrintable(fin.first().at(1).toString()));
        QTRY_VERIFY(core.micCompleteness(key) == Completeness::Complete);
        QVERIFY(core.isMicEnabled(key));
    }

    void followUntickingPartialMicPlansItsDeletion()
    {
        copySectionMic(mSrc, mDst, "TR01", "brass");
        QVERIFY(QFile::remove(mDst + "/" + fakeSectionDir("brass") + "/INSTR1_TR01_01.spitfire"));
        Core core;
        startFollow(core, mDst);
        const MicSignalGroupKey key{"tr01", "brass"};
        QSignalSpy changed(&core, &Core::micCompletenessChanged);
        core.setMicEnabled(key, false);
        QCOMPARE(changed.count(), 1);
        QVERIFY(core.micCompleteness(key) == Completeness::Empty);
        const Plan plan = core.buildPlan();
        QVERIFY2(plan.canRun(), qPrintable(plan.refusals.join('\n')));
        QCOMPARE(countSteps(plan, PlanStep::Type::DeleteFile, "INSTR2_TR01_01"), 1);
        QCOMPARE(countSteps(plan, PlanStep::Type::CopyToStaging, "_TR01_"), 0);
    }

    void followIgnoresStoredSelectionAndSwitchesDestination()
    {
        QVERIFY(QDir().mkpath(mDst));
        {
            Core writer; // project.smt says everything is enabled
            writer.setDestinationDirectoryPath(mDst);
            QVERIFY(writer.saveConfiguration());
        }
        Core core;
        core.setSelectionFollowsDestination(true);
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
        QCOMPARE(enabledKeys(core), lockedKeys(core));

        const QString other = mTmp.filePath("other/lib");
        copySectionMic(mSrc, other, "MO01", "woodwinds");
        scan.clear();
        core.setDestinationDirectoryPath(other);
        QCOMPARE(enabledKeys(core), lockedKeys(core)); // nothing stale while scanning
        QVERIFY(waitFor(scan));
        QVERIFY(core.isMicEnabled({"mo01", "woodwinds"}));
        QVERIFY(!core.isMicEnabled({"tr01", "brass"}));
    }

    void followRederivesAfterApplyAndNeverDuringIt()
    {
        Core core;
        startFollow(core, mDst);
        const MicSignalGroupKey other{"mo01", "woodwinds"};
        core.setMicEnabled({"tr01", "brass"}, true);
        const Plan plan = core.buildPlan();
        QVERIFY2(plan.canRun(), qPrintable(plan.refusals.join('\n')));
        QSignalSpy fin(&core, &Core::applyFinished);
        QVERIFY(core.startApply(plan));
        core.setMicEnabled(other, true); // mid-apply: not copied, and no derive may undo it yet
        core.setSelectionFollowsDestination(true); // derives at once, unless an apply runs
        QVERIFY(core.isMicEnabled(other));
        QVERIFY(waitFor(fin));
        QVERIFY2(fin.first().first().toBool(), qPrintable(fin.first().at(1).toString()));
        QTRY_VERIFY(!core.isMicEnabled(other)); // re-derived from disk after the apply's rescan
        QVERIFY(core.micCompleteness({"tr01", "brass"}) == Completeness::Complete);
        QStringList expected = lockedKeys(core);
        expected << "tr01/brass";
        expected.sort();
        QCOMPARE(enabledKeys(core), expected);
    }

    void followCancelledApplyLeavesPartialMic()
    {
        copySectionMic(mSrc, mDst, "TR01", "brass");
        QVERIFY(QFile::remove(mDst + "/" + fakeSectionDir("brass") + "/INSTR2_TR01_01.spitfire"));
        Core core;
        startFollow(core, mDst);
        const MicSignalGroupKey key{"tr01", "brass"};
        QVERIFY(core.micCompleteness(key) == Completeness::Partial);
        const Plan plan = core.buildPlan();
        QVERIFY2(plan.canRun(), qPrintable(plan.refusals.join('\n')));
        bool cancelled = false;
        connect(&core, &Core::applyStepChanged, this, [&](int i, PlanStep::Status st, const QString &) {
            if (!cancelled && st == PlanStep::Status::Done && plan.steps.at(i).type == PlanStep::Type::CopyToStaging) {
                cancelled = true;
                core.cancelApply();
            }
        });
        QSignalSpy fin(&core, &Core::applyFinished);
        QVERIFY(core.startApply(plan));
        QVERIFY(waitFor(fin));
        QVERIFY(cancelled);
        QVERIFY(!fin.first().first().toBool());
        QTRY_VERIFY2(core.micCompleteness(key) == Completeness::Partial, qPrintable(sortedPaths(mDst).join(' ')));
        QVERIFY(core.isMicEnabled(key));
    }

    void useLibraryPointsTheProfilesEntriesAtTheChosenFolder()
    {
        const QString props = mTmp.filePath("Spitfire.properties");
        const QByteArray original = "{\"BBC Symphony Orchestra\":{\"config\":[\"/old/Config\"],\"patches\":[\"/old/Patches\"],"
                                    "\"presets\":[\"/old/Presets\"],\"samples\":[\"/old/Samples\"]},\"Labs\":{\"samples\":[\"/labs\"]}}";
        QFile f(props);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(original);
        f.close();
        // folders named like a BBCSO library (a different name is refused)
        const QString name = "Spitfire Audio - BBC Symphony Orchestra";
        const QString src = makeFakeLibrary(mTmp.filePath("s/" + name), kTokens, kSections, 1, kFileSize);
        const QString dst = mTmp.filePath("d/" + name);
        QDir().mkpath(dst);
        Core core;
        QVERIFY(!core.useLibrary(true, props).ok()); // no folders chosen yet
        core.setSourceDir(src);
        core.setDestinationDirectoryPath(dst);
        auto r = core.useLibrary(true, props);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.products, QStringList{"BBC Symphony Orchestra"});
        QVERIFY(r.written);
        QVERIFY(readAll(props).contains(dst.toUtf8() + "/Samples"));
        QVERIFY(!readAll(props).contains("/old/"));
        QVERIFY(readAll(props).contains("/labs"));
        QCOMPARE(readAll(props + ".smt-backup"), original);
        r = core.useLibrary(false, props);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY(readAll(props).contains(src.toUtf8() + "/Config"));
        QCOMPARE(readAll(props + ".smt-backup"), original); // still the first original
        // a folder of another product is refused with the profile named, the file unchanged
        const QString labs = mTmp.filePath("l/Spitfire Originals - Labs");
        QDir().mkpath(labs);
        core.setDestinationDirectoryPath(labs);
        const QByteArray before = readAll(props);
        r = core.useLibrary(true, props); // the destination must be named like the source
        QVERIFY(!r.ok());
        QCOMPARE(readAll(props), before);
        core.setDestinationDirectoryPath(dst);
        core.setSourceDir(labs); // "use source" with another product's folder: the profile guard
        r = core.useLibrary(false, props);
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains("Labs") && r.error.contains("BBC Symphony Orchestra"), qPrintable(r.error));
        QCOMPARE(readAll(props), before);
        // a profile without Spitfire entries is refused, nothing written
        core.setDestinationDirectoryPath(dst);
        ProductProfile bare = core.profile();
        bare.spitfireEntries.clear();
        core.setProfile(bare);
        r = core.useLibrary(true, props);
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains("does not describe"), qPrintable(r.error));
        QCOMPARE(readAll(props), before);
    }

    // The data dir Core itself uses ($SMT_APP_DATA_DIR, else the test-mode location), never the user's real one.
    static QString dataDir()
    {
        if (qEnvironmentVariableIsEmpty("SMT_APP_DATA_DIR") && !QStandardPaths::isTestModeEnabled())
            qFatal("refusing to touch the real app data dir");
        return Core().applicationDataDir();
    }
    static QString cacheOf(const QString &dir)
    {
        const QByteArray key = QCryptographicHash::hash(QDir::cleanPath(dir).toUtf8(), QCryptographicHash::Sha1).toHex();
        return dataDir() + "/ScanCache/" + QString::fromLatin1(key) + ".smtcache";
    }
    static void scanSource(Core &core, const QString &dir)
    {
        QSignalSpy done(&core, &Core::sourceScanFinished);
        core.setSourceDir(dir);
        QVERIFY(waitFor(done));
    }

    void everyFolderHasItsOwnScanCacheAndSwitchingHitsItBothWays()
    {
        const QString a = makeFakeLibrary(mTmp.filePath("A"), kTokens, kSections, 2, kFileSize);
        const QString b = makeFakeLibrary(mTmp.filePath("B"), {"TR01"}, {"brass"}, 1, kFileSize);
        QDir(dataDir() + "/ScanCache").removeRecursively();
        {
            Core core;
            scanSource(core, a);
            scanSource(core, b);
        }
        QTRY_VERIFY(QFile::exists(cacheOf(a)) && QFile::exists(cacheOf(b))); // one file each, written after finished()
        QVERIFY(cacheOf(a) != cacheOf(b));
        // Replace both with recognisable one-file scans: only a cache hit can show them.
        auto marker = [](const QString &name) {
            FileInfo f{};
            f.path = name;
            f.size = 1;
            return QList<FileInfo>{f};
        };
        auto put = [&](const QString &dir, const QString &name) {
            QFile f(cacheOf(dir));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QFileInfoGathererTask::serializeScanCache(dir, marker(name)));
        };
        put(a, "marker-A.wav");
        put(b, "marker-B.wav");
        Core fresh; // a new session: only the disk caches can answer
        scanSource(fresh, a);
        QCOMPARE(fresh.sourceFileInfos().first().path, QString("marker-A.wav"));
        scanSource(fresh, b);
        QCOMPARE(fresh.sourceFileInfos().first().path, QString("marker-B.wav"));
        scanSource(fresh, a); // and back
        QCOMPARE(fresh.sourceFileInfos().first().path, QString("marker-A.wav"));
        QCOMPARE(fresh.sourceFileInfos().size(), 1);
    }

    void aFinishedSourceScanIsReusedInTheSessionAndRescanDropsOnlyItsOwn()
    {
        const QString a = makeFakeLibrary(mTmp.filePath("A"), kTokens, kSections, 2, kFileSize);
        const QString b = makeFakeLibrary(mTmp.filePath("B"), {"TR01"}, {"brass"}, 1, kFileSize);
        QDir(dataDir() + "/ScanCache").removeRecursively();
        Core core;
        scanSource(core, a);
        scanSource(core, b);
        QTRY_VERIFY(QFile::exists(cacheOf(a)) && QFile::exists(cacheOf(b)));
        const QDateTime tb = QFileInfo(cacheOf(b)).lastModified();
        QFile::remove(cacheOf(a)); // only the session memory can answer for A now
        scanSource(core, a);
        QVERIFY(!QFile::exists(cacheOf(a))); // reused as it was, no walk, no cache rewrite
        QCOMPARE(core.sourceFileInfos().size(), scanTree(a).size());
        QCOMPARE(core.spaceRequired({"tr01", "brass"}), size_t(kInstruments * kFileSize));
        // Rescan source: this folder's cache and memory are dropped and rebuilt, the other folder's stay
        QTest::qWait(30);
        QSignalSpy done(&core, &Core::sourceScanFinished);
        core.rescanSource();
        QVERIFY(waitFor(done));
        QTRY_VERIFY(QFile::exists(cacheOf(a))); // rescanned from disk and cached again
        QCOMPARE(QFileInfo(cacheOf(b)).lastModified(), tb);
    }

    void theSessionKeepsAtMostThreeFinishedSourceScans()
    {
        QStringList dirs;
        for (int i = 1; i <= 4; ++i)
            dirs << makeFakeLibrary(mTmp.filePath(QString("F%1").arg(i)), {"TR01"}, {"brass"}, 1, kFileSize);
        QDir(dataDir() + "/ScanCache").removeRecursively();
        Core core;
        for (const QString &d : dirs)
            scanSource(core, d);
        for (const QString &d : dirs)
            QTRY_VERIFY(QFile::exists(cacheOf(d)));
        for (const QString &d : dirs)
            QFile::remove(cacheOf(d)); // from here only the session memory (or a rescan) can answer
        for (int i : {2, 1, 3}) { // the three most recent folders are still remembered (the current one is F4)
            scanSource(core, dirs[i]);
            QTest::qWait(150);
            QVERIFY2(!QFile::exists(cacheOf(dirs[i])), qPrintable(dirs[i]));
        }
        scanSource(core, dirs[0]); // the oldest was dropped: scanned again and cached again
        QTRY_VERIFY(QFile::exists(cacheOf(dirs[0])));
    }

    void differentSpellingsOfAFolderShareOneScanAndRescanFindsIt()
    {
        const QString a = makeFakeLibrary(mTmp.filePath("A"), kTokens, kSections, 1, kFileSize);
        const QString b = makeFakeLibrary(mTmp.filePath("B"), {"TR01"}, {"brass"}, 1, kFileSize);
        QDir(dataDir() + "/ScanCache").removeRecursively();
        Core core;
        scanSource(core, a);
        scanSource(core, b);
        QTRY_VERIFY(QFile::exists(cacheOf(a)));
        QFile::remove(cacheOf(a));
        scanSource(core, a + "/"); // another spelling: the same remembered scan
        QTest::qWait(150);
        QVERIFY(!QFile::exists(cacheOf(a)));
        QVERIFY(QFile::exists(cacheOf(a + "/../A")) == false); // and the cache file name is the same for every spelling
        QCOMPARE(cacheOf(a + "/"), cacheOf(a));
        QCOMPARE(cacheOf(b + "/./"), cacheOf(b));
        QSignalSpy done(&core, &Core::sourceScanFinished);
        core.rescanSource(); // the current folder is spelled with a slash: its normalised cache is the one dropped
        QVERIFY(waitFor(done));
        QTRY_VERIFY(QFile::exists(cacheOf(a)));
    }

    // Background mode (the GUI's): free space, plans, the derivation and the config read run on pool threads.
    void backgroundModeDerivesTheSameSelectionAndSettlesBeforeScanFinished()
    {
        const QString name = "Spitfire Audio - BBC Symphony Orchestra";
        const QString src = makeFakeLibrary(mTmp.filePath("bs/" + name), kTokens, kSections, 2, kFileSize);
        const QString dst = mTmp.filePath("bd/" + name);
        QDir().mkpath(dst + "/Samples/_Brass");
        QVERIFY(QFile::copy(src + "/Samples/_Brass/INSTR1_TR01_01.spitfire", dst + "/Samples/_Brass/INSTR1_TR01_01.spitfire"));
        auto run = [&](Core &core) {
            core.setSelectionFollowsDestination(true);
            QSignalSpy scan(&core, &Core::scanFinished);
            core.setSourceDir(src);
            core.setDestinationDirectoryPath(dst);
            QVERIFY(waitFor(scan));
            QVERIFY(!core.scanning()); // scanFinished means settled
        };
        Core sync, bg;
        bg.setBackgroundWork(true);
        run(sync);
        run(bg);
        QCOMPARE(bg.micCompleteness({"tr01", "brass"}), Completeness::Partial);
        forAllMicSignalGroupKeys(sync.profile(), [&](const MicSignalGroupKey &key) {
            QCOMPARE(bg.isMicEnabled(key), sync.isMicEnabled(key));
            QCOMPARE(bg.micCompleteness(key), sync.micCompleteness(key));
        });
    }

    void backgroundPlansMatchTheSynchronousOnesAndStaleOnesAreDropped()
    {
        const QString name = "Spitfire Audio - BBC Symphony Orchestra";
        const QString src = makeFakeLibrary(mTmp.filePath("ps/" + name), kTokens, kSections, 2, kFileSize);
        const QString dst = mTmp.filePath("pd/" + name);
        Core core;
        core.setBackgroundWork(true);
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(src);
        core.setDestinationDirectoryPath(dst);
        QVERIFY(waitFor(scan));
        const Plan sync = core.buildPlan();
        QVERIFY(sync.canRun());
        QSignalSpy ready(&core, &Core::planReady);
        const quint64 token = core.requestPlan();
        QCOMPARE(ready.count(), 0); // not synchronously: it is built on a worker
        QTRY_COMPARE(ready.count(), 1);
        const Plan async = ready.first().first().value<Plan>();
        QCOMPARE(ready.first().at(1).toULongLong(), token);
        QCOMPARE(async.steps.size(), sync.steps.size());
        QCOMPARE(async.bytesToAdd, sync.bytesToAdd);
        // two requests: only the latest answers
        ready.clear();
        core.requestPlan();
        const quint64 second = core.requestPlan();
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(ready.first().at(1).toULongLong(), second);
        // an input changes while a request is out: its answer is dropped
        ready.clear();
        core.requestPlan();
        core.setMicEnabled({"tr01", "brass"}, !core.isMicEnabled({"tr01", "brass"}));
        QTest::qWait(500);
        QCOMPARE(ready.count(), 0);
    }

    void backgroundPendingChangeAndFreeSpaceComeFromWorkers()
    {
        const QString name = "Spitfire Audio - BBC Symphony Orchestra";
        const QString src = makeFakeLibrary(mTmp.filePath("qs/" + name), kTokens, kSections, 2, kFileSize);
        const QString dst = mTmp.filePath("qd/" + name);
        Core core;
        core.setBackgroundWork(true);
        QSignalSpy remaining(&core, &Core::remainingSpaceChanged);
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(src);
        core.setDestinationDirectoryPath(dst);
        QCOMPARE(remaining.count(), 0); // the volume is asked on a worker
        QVERIFY(core.scanning());
        QVERIFY(waitFor(scan));
        QTRY_VERIFY(remaining.count() > 0);
        QSignalSpy change(&core, &Core::pendingChangeReady);
        core.requestPendingChange();
        QTRY_COMPARE(change.count(), 1);
        const auto c = change.first().first().value<PendingChange>();
        QVERIFY(c.known && c.hasSteps && c.any && !c.refused);
    }

    // scanFinished means settled: it fires once, and not while the configuration read or the derivation is pending.
    void scanFinishedFiresOnceAndSettledEvenWhenTheDestinationIsRepickedDuringTheDerivation()
    {
        const QString name = "Spitfire Audio - BBC Symphony Orchestra";
        const QString src = makeFakeLibrary(mTmp.filePath("rs/" + name), kTokens, kSections, 2, kFileSize);
        const QString dst = mTmp.filePath("rd/" + name);
        Core core;
        core.setBackgroundWork(true);
        core.setSelectionFollowsDestination(true);
        int finished = 0;
        bool scanningWhenFinished = true;
        connect(&core, &Core::scanFinished, this, [&] {
            ++finished;
            scanningWhenFinished = core.scanning();
        });
        bool repicked = false;
        connect(&core, &Core::scanStatusChanged, this, [&] {
            if (!repicked && core.scanStatus().preparing && !core.scanStatus().any()) { // the derivation just started
                repicked = true;
                core.setDestinationDirectoryPath(dst); // the same folder again: its configuration is read again
            }
        });
        core.setSourceDir(src);
        core.setDestinationDirectoryPath(dst);
        QTRY_COMPARE_WITH_TIMEOUT(finished, 1, 10000);
        QTest::qWait(300); // nothing more follows
        QCOMPARE(finished, 1);
        QVERIFY(!scanningWhenFinished);
        QVERIFY(repicked);
        QVERIFY(!core.scanning());
        // a second destination pick while the first read is still out: one scanFinished, and not early
        finished = 0;
        QSignalSpy states(&core, &Core::scanStatusChanged);
        core.setDestinationDirectoryPath(mTmp.filePath("rd2/" + name)); // starts a scan and a configuration read
        core.setDestinationDirectoryPath(mTmp.filePath("rd2/" + name)); // the same again at once
        QTRY_COMPARE_WITH_TIMEOUT(finished, 1, 10000);
        QTest::qWait(300);
        QCOMPARE(finished, 1);
        QVERIFY(!scanningWhenFinished);
    }

    void aClickDuringTheDerivationSticks()
    {
        const QString name = "Spitfire Audio - BBC Symphony Orchestra";
        const QString src = makeFakeLibrary(mTmp.filePath("cs/" + name), kTokens, kSections, 2, kFileSize);
        const QString dst = mTmp.filePath("cd/" + name);
        Core core;
        core.setBackgroundWork(true);
        core.setSelectionFollowsDestination(true);
        const MicSignalGroupKey key{"tr01", "brass"};
        bool clicked = false;
        connect(&core, &Core::scanStatusChanged, this, [&] {
            if (!clicked && core.scanStatus().preparing && !core.scanStatus().any()) { // the derivation is out
                clicked = true;
                QVERIFY(!core.isMicEnabled(key)); // an empty destination: derived as off
                core.setMicEnabled(key, true); // the user ticks it meanwhile
            }
        });
        QSignalSpy done(&core, &Core::scanFinished);
        core.setSourceDir(src);
        core.setDestinationDirectoryPath(dst);
        QVERIFY(waitFor(done));
        QVERIFY(clicked);
        QVERIFY(core.isMicEnabled(key)); // the derived "off" did not overwrite the click
        QVERIFY(!core.isMicEnabled({"cl01", "brass"})); // other keys were still derived
    }

    void aClickWhileTheConfigurationIsBeingReadSticks()
    {
        const QString name = "Spitfire Audio - BBC Symphony Orchestra";
        const QString src = makeFakeLibrary(mTmp.filePath("ks/" + name), kTokens, kSections, 2, kFileSize);
        const QString dst = mTmp.filePath("kd/" + name);
        Core core;
        core.setBackgroundWork(true);
        core.setSelectionFollowsDestination(true);
        const MicSignalGroupKey key{"tr01", "brass"};
        bool clicked = false;
        connect(&core, &Core::scanStatusChanged, this, [&] {
            if (!clicked && core.scanStatus().preparing) { // project.smt is being read on a worker
                clicked = true;
                core.setMicEnabled(key, false); // everything starts ticked: off, then on again
                core.setMicEnabled(key, true);
            }
        });
        QSignalSpy done(&core, &Core::scanFinished);
        core.setSourceDir(src);
        core.setDestinationDirectoryPath(dst);
        QVERIFY(waitFor(done));
        QVERIFY(clicked);
        QVERIFY(core.isMicEnabled(key)); // neither the loaded configuration nor the derivation overwrote the click
    }

    void rescanWithoutASourceDoesNotLeakItsSkipFlagIntoALaterScan()
    {
        const QString a = makeFakeLibrary(mTmp.filePath("A"), kTokens, kSections, 2, kFileSize);
        QDir(dataDir() + "/ScanCache").removeRecursively();
        {
            Core first;
            scanSource(first, a);
        }
        QTRY_VERIFY(QFile::exists(cacheOf(a)));
        FileInfo marker{};
        marker.path = "marker.wav";
        QFile f(cacheOf(a));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QFileInfoGathererTask::serializeScanCache(a, {marker}));
        f.close();
        Core core;
        core.rescanSource(); // no source yet: nothing to scan
        scanSource(core, a); // a normal scan must still use its cache
        QCOMPARE(core.sourceFileInfos().size(), 1);
        QCOMPARE(core.sourceFileInfos().first().path, QString("marker.wav"));
    }

    void rescanWalksTheDiskEvenWhenACacheFileAppearsAfterTheDeletion()
    {
        const QString a = makeFakeLibrary(mTmp.filePath("A"), kTokens, kSections, 2, kFileSize);
        QDir(dataDir() + "/ScanCache").removeRecursively();
        Core core;
        scanSource(core, a);
        QTRY_VERIFY(QFile::exists(cacheOf(a)));
        const qsizetype real = core.sourceFileInfos().size();
        QSignalSpy done(&core, &Core::sourceScanFinished);
        core.rescanSource();
        // as a write that was still finishing would: a (valid) cache file is there when the scan starts
        QDir().mkpath(QFileInfo(cacheOf(a)).absolutePath());
        QFile f(cacheOf(a));
        QVERIFY(f.open(QIODevice::WriteOnly));
        FileInfo marker{};
        marker.path = "marker.wav";
        f.write(QFileInfoGathererTask::serializeScanCache(a, {marker}));
        f.close();
        QVERIFY(waitFor(done));
        QCOMPARE(core.sourceFileInfos().size(), real); // from the disk, not the marker
    }

    void anOverlapThatAppearsAfterThePlanIsRefusedByTheExecutorAndNothingIsWritten_data()
    {
        QTest::addColumn<bool>("background");
        QTest::newRow("synchronous") << false;
        QTest::newRow("background") << true;
    }
    void anOverlapThatAppearsAfterThePlanIsRefusedByTheExecutorAndNothingIsWritten()
    {
        SMT_UNIX_ONLY();
        QFETCH(bool, background);
        const QString name = "Spitfire Audio - BBC Symphony Orchestra";
        const QString src = makeFakeLibrary(mTmp.filePath(QString("os%1/").arg(background) + name), kTokens, kSections, 2, kFileSize);
        const QString dst = mTmp.filePath(QString("od%1/").arg(background) + name);
        Core core;
        core.setBackgroundWork(background);
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(src);
        core.setDestinationDirectoryPath(dst);
        QVERIFY(waitFor(scan));
        const Plan plan = core.buildPlan();
        QVERIFY(plan.canRun() && !plan.isEmpty());
        // the destination path now leads into the source (a link made after the plan was built)
        QDir().mkpath(QFileInfo(dst).absolutePath());
        QVERIFY(QFile::link(src, dst));
        const QStringList before = snapshot(src);
        QSignalSpy fin(&core, &Core::applyFinished);
        QVERIFY(core.startApply(plan));
        QVERIFY(waitFor(fin));
        QVERIFY(!fin.first().first().toBool());
        QVERIFY2(!fin.first().at(1).toString().isEmpty(), "the refusal says why");
        QCOMPARE(snapshot(src), before); // nothing was written, not even a lock or project.smt
    }

    // The destination the app derives from a pick: a folder that does not exist yet scans as an empty one, and
    // picking the source's own parent resolves to the source and is refused clearly.
    void aNotYetExistingDestinationLeafScansAsEmptyAndTheSourcesParentIsAnOverlap()
    {
        const QString name = "Spitfire Audio - BBC Symphony Orchestra";
        const QString src = makeFakeLibrary(mTmp.filePath("ls/" + name), kTokens, kSections, 2, kFileSize);
        const QString chosen = mTmp.filePath("lc");
        QDir().mkpath(chosen);
        for (bool background : {false, true}) {
            Core core;
            core.setBackgroundWork(background);
            QSignalSpy scan(&core, &Core::scanFinished);
            core.setSourceDir(src);
            core.setDestinationDirectoryPath(resolveDestinationFolder(src, chosen)); // chosen/<name>, not created
            QVERIFY(waitFor(scan));
            QVERIFY(!QFileInfo::exists(chosen + "/" + name)); // choosing wrote nothing
            const Plan plan = core.buildPlan();
            QVERIFY2(plan.canRun(), qPrintable(plan.refusals.join('\n')));
            QVERIFY(plan.bytesToAdd > 0 && plan.bytesToFree == 0); // everything is to be copied
        }
        Core core;
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(src);
        core.setDestinationDirectoryPath(resolveDestinationFolder(src, QFileInfo(src).absolutePath())); // = the source
        QVERIFY(waitFor(scan));
        const Plan refused = core.buildPlan();
        QVERIFY(!refused.canRun());
        QVERIFY2(refused.refusals.join('\n').contains("same folder as the source"), qPrintable(refused.refusals.join('\n')));
    }

    void scanProgressCountsFilesAndKnowsTheTotalOfARescan()
    {
        const QString a = makeFakeLibrary(mTmp.filePath("A"), kTokens, kSections, 2, kFileSize);
        QDir(dataDir() + "/ScanCache").removeRecursively();
        Core core;
        QList<ScanStatus> seen;
        connect(&core, &Core::scanStatusChanged, this, [&] { seen << core.scanStatus(); });
        scanSource(core, a);
        const qint64 files = scanTree(a).size();
        QVERIFY(!core.scanning());
        QVERIFY(!core.scanStatus().any());
        QVERIFY(!seen.isEmpty());
        QVERIFY(seen.first().source); // started
        QCOMPARE(seen.first().sourceExpected, qint64(-1)); // the first scan of a folder: no total yet
        QCOMPARE(seen.last().sourceFiles, files);
        // a rescan knows the previous file count and starts counting from zero
        seen.clear();
        QSignalSpy done(&core, &Core::sourceScanFinished);
        core.rescanSource();
        QVERIFY(waitFor(done));
        QVERIFY(seen.first().source);
        QCOMPARE(seen.first().sourceExpected, files);
        QVERIFY(core.scanStatus().sourceFiles <= files);
    }

    // A library with Patches/Presets/Config files is never "up to date": Apply refreshes them (Repair may have
    // rewritten them at the same size), and says so as an ordinary copy.
    void anUpToDateLibraryWithSettingsFilesStillOffersTheirRefresh()
    {
        const QString name = "Spitfire Audio - BBC Symphony Orchestra";
        const QString src = makeFakeLibrary(mTmp.filePath("ts/" + name), kTokens, kSections, 1, kFileSize);
        const QString dst = mTmp.filePath("td/" + name);
        for (const char *rel : {"Patches/a.zmulti", "Presets/b.zpreset", "Config/c.zconfig"}) {
            QDir().mkpath(QFileInfo(src + "/" + rel).absolutePath());
            QFile f(src + "/" + rel);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(10, 'x'));
        }
        Core core;
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(src);
        core.setDestinationDirectoryPath(dst);
        QVERIFY(waitFor(scan));
        QSignalSpy fin(&core, &Core::applyFinished);
        QVERIFY(core.startApply(core.buildPlan()));
        QVERIFY(waitFor(fin));
        QVERIFY(fin.first().first().toBool());
        QTRY_VERIFY(!core.scanning());
        const PendingChange c = core.pendingChange();
        QVERIFY(c.known && c.hasSteps && c.any && !c.refused);
        QCOMPARE(c.add, qint64(30)); // exactly the three settings files
        QCOMPARE(c.remove, qint64(0));
        QVERIFY(applyEnabled(c));
        QVERIFY(applyButtonText(c).startsWith("Apply (+30 bytes / \u2212"));
    }

    void pendingChangeAndSourceContent()
    {
        Core core;
        QVERIFY(!core.hasSourceContent());
        PendingChange nothing = core.pendingChange(); // nothing chosen: refused, not "unknown"
        QVERIFY(nothing.known && nothing.refused && !nothing.hasSteps && !nothing.firstRefusal.isEmpty());
        QVERIFY(!applyEnabled(nothing));
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
        QVERIFY(core.hasSourceContent());
        PendingChange c = core.pendingChange();
        QVERIFY(c.known && c.any && c.hasSteps && !c.refused);
        QVERIFY(applyEnabled(c));
        QCOMPARE(c.remove, qint64(0));
        QCOMPARE(c.add, qint64(core.buildPlan().bytesToAdd));
        QVERIFY(c.add > 0);
        // Nothing left to do after the plan ran: "No changes".
        QSignalSpy fin(&core, &Core::applyFinished);
        QVERIFY(core.startApply(core.buildPlan()));
        QVERIFY(waitFor(fin));
        // Nothing to copy or delete any more; the database rebuilds are still steps (they resume an interrupted apply).
        QTRY_VERIFY(core.pendingChange().known);
        QTRY_VERIFY(!core.pendingChange().any);
        c = core.pendingChange();
        QVERIFY(c.hasSteps && !c.refused && applyEnabled(c));
        QCOMPARE(applyButtonText(c), QString("Apply (update databases)"));
        // A source folder with no file at all counts as no content once scanned.
        const QString empty = mTmp.filePath("emptysrc");
        QVERIFY(QDir().mkpath(empty));
        QSignalSpy sc2(&core, &Core::sourceScanFinished);
        core.setSourceDir(empty);
        QVERIFY(waitFor(sc2));
        QVERIFY(!core.hasSourceContent());
    }
};

QTEST_GUILESS_MAIN(TstCore)
#include "tst_core.moc"
