#include <QtTest>
#include <csignal>
#include "fakelibrary.h"
#include "unixonly.h"
#include "librarydb.h"

using namespace smt;

static const QStringList kTokens{"TR01", "MO01"};
static const QStringList kSections{"brass", "woodwinds"};

struct Result
{
    int code = -1;
    QString out, err;
};

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

static QStringList sortedPaths(const QString &root)
{
    QStringList out;
    for (const auto &f : scanTree(root))
        out << f.path;
    out.sort();
    return out;
}

class TstCli : public QObject
{
    Q_OBJECT

    QTemporaryDir mTmp;     // libraries live here; snapshots cover all of it
    QTemporaryDir mAppData; // the child's scan cache, never the user's
    QString mSrc, mDst;

    void start(QProcess &p, const QStringList &args)
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("SMT_APP_DATA_DIR", mAppData.path());
        p.setProcessEnvironment(env);
        p.start(SMT_BIN, args);
        QVERIFY2(p.waitForStarted(), qPrintable(p.errorString()));
    }

    // A timeout fails the caller's exit-code check instead of hanging the suite.
    Result run(const QStringList &args, int timeoutMs = 60000)
    {
        QProcess p;
        start(p, args);
        Result r;
        if (!p.waitForFinished(timeoutMs)) {
            p.kill();
            p.waitForFinished();
            return r;
        }
        if (p.exitStatus() != QProcess::NormalExit)
            return r;
        r.code = p.exitCode();
        r.out = QString::fromUtf8(p.readAllStandardOutput()).replace("\r\n", "\n"); // Windows text mode
        r.err = QString::fromUtf8(p.readAllStandardError()).replace("\r\n", "\n");
        return r;
    }

private slots:
    void init()
    {
        QVERIFY(mTmp.isValid());
        QVERIFY(mAppData.isValid());
        mSrc = makeFakeLibrary(mTmp.filePath("src"), kTokens, kSections, 2, 1000);
        mDst = mTmp.filePath("dst/lib"); // not created yet
    }

    void cleanup()
    {
        QDir(mTmp.path()).removeRecursively();
        QDir().mkpath(mTmp.path());
    }

    void analyzeListsMics()
    {
        const Result r = run({"analyze", "-s", mSrc});
        QCOMPARE(r.code, 0);
        QVERIFY2(r.out.contains("Databases: 3"), qPrintable(r.out));
        QVERIFY2(r.out.contains("Brass / Tree (TR01): 2 files"), qPrintable(r.out));
        QVERIFY2(r.out.contains("Woodwinds / Mono (MO01): 2 files"), qPrintable(r.out));
        QVERIFY2(r.err.isEmpty(), qPrintable(r.err));
        // The scan cache went to the isolated app data dir.
        const QStringList caches = QDir(mAppData.filePath("ScanCache")).entryList({"*.smtcache"});
        QCOMPARE(caches.size(), 1); // one cache file per scanned folder
        // A trailing '/' names the same folder, so the cached scan is reused under the same key.
        QCOMPARE(run({"analyze", "-s", mSrc + "/"}).out, r.out);
        QCOMPARE(QDir(mAppData.filePath("ScanCache")).entryList({"*.smtcache"}), caches);
        QFile cache(mAppData.filePath("ScanCache/" + caches.first()));
        QVERIFY(cache.open(QIODevice::ReadOnly));
        QCOMPARE(cache.read(4), QByteArray("SFMC")); // the binary cache
    }

    void analyzePrintsProfileLine()
    {
        const QString line = "Profile: BBC Symphony Orchestra (name.mbishop.smtprofile.bbcso)\n";
        Result r = run({"analyze", "-s", mSrc});
        QVERIFY2(r.out.startsWith(line), qPrintable(r.out));
        r = run({"plan", "-s", mSrc, "-p", mDst});
        QVERIFY2(r.out.startsWith(line), qPrintable(r.out));
        r = run({"analyze", "--profile", "ewc", "-s", mSrc});
        QVERIFY2(r.out.startsWith("Profile: Eric Whitacre Choir"), qPrintable(r.out));
    }

    void unknownProfileExitsTwo()
    {
        const Result r = run({"analyze", "--profile", "nope", "-s", mSrc});
        QCOMPARE(r.code, 2);
        QVERIFY2(r.err.contains("Unknown profile: nope"), qPrintable(r.err));
        QVERIFY2(r.err.contains("name.mbishop.smtprofile.bbcso"), qPrintable(r.err));
    }

    void profileFlagAcceptsABundlePath()
    {
        const QString dir = SMT_PROFILES_DIR;
        Result r = run({"analyze", "--profile", dir + "/EricWhitacre.smtprofile", "-s", mSrc});
        QVERIFY2(r.out.startsWith("Profile: Eric Whitacre Choir"), qPrintable(r.out));
        r = run({"analyze", "--profile", dir + "/EricWhitacre.smtprofile/", "-s", mSrc});
        QVERIFY2(r.out.startsWith("Profile: Eric Whitacre Choir"), qPrintable(r.out));
        r = run({"analyze", "--profile", mTmp.filePath("missing.smtprofile"), "-s", mSrc});
        QCOMPARE(r.code, 2);
        QVERIFY2(!r.err.isEmpty(), qPrintable(r.err));
    }

    void analyzeWithoutDatabasesFails()
    {
        QDir().mkpath(mTmp.filePath("empty"));
        const Result r = run({"analyze", "-s", mTmp.filePath("empty")});
        QCOMPARE(r.code, 1);
        QVERIFY(!r.err.isEmpty());
    }

    void planPrintsStepsAndChangesNothing()
    {
        const Result r = run({"plan", "-s", mSrc, "-p", mDst});
        QCOMPARE(r.code, 0);
        QVERIFY2(r.out.contains(mDst + "/project.smt"), qPrintable(r.out));
        QVERIFY2(r.out.contains("Plan: "), qPrintable(r.out));
        QVERIFY2(r.out.contains("copy Samples/_Brass/INSTR1_TR01_01.spitfire to staging"), qPrintable(r.out));
        QVERIFY2(r.err.isEmpty(), qPrintable(r.err));
        QVERIFY(!QFileInfo::exists(mTmp.filePath("dst")));
    }

    void planSaysWhenOnlyDatabasesAreRefreshed()
    {
        const QString line = "No file changes; databases will be refreshed only.";
        Result r = run({"plan", "-s", mSrc, "-p", mDst});
        QCOMPARE(r.code, 0);
        QVERIFY2(!r.out.contains(line), qPrintable(r.out));
        QCOMPARE(run({"apply", "-y", "-s", mSrc, "-p", mDst}).code, 0);
        for (const QString &cmd : {"plan", "apply"}) {
            r = run({cmd, "-s", mSrc, "-p", mDst});
            QCOMPARE(r.code, 0);
            QVERIFY2(r.out.contains(line), qPrintable(r.out));
        }
    }

    void planChangesNothingOnDisk()
    {
        // An existing, partly filled destination with a config: nothing may change, not even mtimes.
        const QString dst = makeFakeLibrary(mTmp.filePath("dst2"), {"TR01"}, {"brass"}, 1, 1000);
        QFile cfg(dst + "/project.smt");
        QVERIFY(cfg.open(QIODevice::WriteOnly));
        cfg.write("{\"micSignals\":{\"tr01\":{\"brass\":false}}}");
        cfg.close();
        QTest::qWait(20); // so a rewrite would show as a new mtime
        const QStringList before = snapshot(mTmp.path());
        const Result r = run({"plan", "-s", mSrc, "-p", dst});
        QCOMPARE(r.code, 0);
        QVERIFY2(r.out.contains("delete Samples/_Brass/INSTR1_TR01_01.spitfire"), qPrintable(r.out));
        QCOMPARE(snapshot(mTmp.path()), before);
    }

    void applyWithoutYesChangesNothing()
    {
        const QStringList before = snapshot(mTmp.path());
        const Result r = run({"apply", "-s", mSrc, "-p", mDst});
        QCOMPARE(r.code, 0);
        QVERIFY2(r.out.contains("Plan: "), qPrintable(r.out));
        QVERIFY2(r.out.contains("Re-run with -y to apply"), qPrintable(r.out));
        QVERIFY(!r.out.contains("Quit the Spitfire plugin/host"));
        QCOMPARE(snapshot(mTmp.path()), before);
    }

    void applyWithYesInstalls()
    {
        const QStringList srcBefore = snapshot(mSrc);
        const Result r = run({"apply", "-s", mSrc, "-p", mDst, "-y"});
        QVERIFY2(r.code == 0, qPrintable(r.out + r.err));
        QVERIFY2(r.err.isEmpty(), qPrintable(r.err));
        const int quit = r.out.indexOf("Quit the Spitfire plugin/host before applying.");
        QVERIFY(quit >= 0);
        QVERIFY(quit < r.out.indexOf("Running"));
        QVERIFY2(r.out.contains("Done"), qPrintable(r.out));
        QVERIFY2(r.out.contains(" / "), qPrintable(r.out)); // a progress line
        QStringList expected = sortedPaths(mSrc);
        expected << "project.smt";
        expected.sort();
        QCOMPARE(sortedPaths(mDst), expected);
        // All mics kept: each destination DB lists exactly what the source DB lists, and all of it exists.
        for (const auto &f : scanTree(mDst)) {
            if (!f.path.endsWith(".db"))
                continue;
            QStringList got = librarydb::containerNames(mDst + "/" + f.path);
            QStringList want = librarydb::containerNames(mSrc + "/" + f.path);
            got.sort();
            want.sort();
            QVERIFY(!got.isEmpty());
            QCOMPARE(got, want);
            const QString dir = QFileInfo(mDst + "/" + f.path).path();
            for (const QString &name : std::as_const(got))
                QVERIFY2(QFile::exists(dir + "/" + name), qPrintable(f.path + ": " + name));
        }
        QCOMPARE(snapshot(mSrc), srcBefore);
    }

    void refusedPlanExitsNonZero()
    {
        const QString inside = mSrc + "/trimmed";
        const QStringList before = snapshot(mTmp.path());
        for (const QString &cmd : {QString("plan"), QString("apply")}) {
            const Result r = run({cmd, "-s", mSrc, "-p", inside, "-y"});
            QCOMPARE(r.code, 1);
            QVERIFY2(r.out.contains("REFUSED: The destination is inside the source folder."), qPrintable(r.out));
        }
        // ".." reaches Core unchanged and is refused there.
        const Result dots = run({"plan", "-s", mSrc, "-p", mTmp.filePath("dst/../out")});
        QCOMPARE(dots.code, 1);
        QVERIFY2(dots.out.contains("must not contain '..'"), qPrintable(dots.out));
        QCOMPARE(snapshot(mTmp.path()), before);
    }

    void configErrorExitsOne()
    {
        QDir().mkpath(mDst);
        QFile cfg(mDst + "/project.smt");
        QVERIFY(cfg.open(QIODevice::WriteOnly));
        cfg.write("{not json");
        cfg.close();
        const Result r = run({"apply", "-s", mSrc, "-p", mDst, "-y"});
        QCOMPARE(r.code, 1);
        QVERIFY2(r.err.contains("project.smt"), qPrintable(r.err));
        QCOMPARE(sortedPaths(mDst), QStringList{"project.smt"});
    }

    void useRepointsOnlyTheGivenPropertiesFile()
    {
        const QString name = "Spitfire Audio - BBC Symphony Orchestra"; // the library folders carry the product's name
        const QString src = makeFakeLibrary(mTmp.filePath("u/src/" + name), kTokens, kSections, 1, 1000);
        const QString dst = mTmp.filePath("u/dst/" + name);
        QDir().mkpath(dst);
        const QString props = mTmp.filePath("Spitfire.properties");
        QFile f(props);
        QVERIFY(f.open(QIODevice::WriteOnly));
        // BBCSO's entry pointing somewhere unrelated, next to another product's
        const QByteArray original = "{\"BBC Symphony Orchestra\":{\"config\":[\"/old/Config\"],\"patches\":[\"/old/Patches\"],"
                                    "\"presets\":[\"/old/Presets\"],\"samples\":[\"/old/Samples\"]},\"P\":{\"samples\":[\"/p/Samples\"]}}";
        f.write(original);
        f.close();
        const QStringList before = snapshot(src);
        Result r = run({"use", "trimmed", "-s", src, "-p", dst, "--properties", props});
        QVERIFY2(r.code == 0, qPrintable(r.err));
        QVERIFY2(r.out.contains("trimmed") && r.out.contains("now uses"), qPrintable(r.out));
        QVERIFY(QFile::exists(props + ".smt-backup"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray trimmed = f.readAll().replace("\\\\", "/"); // JSON-escaped Windows backslashes
        QVERIFY(trimmed.contains(dst.toUtf8() + "/Samples"));
        QVERIFY(trimmed.contains(dst.toUtf8() + "/Config"));
        QVERIFY(!trimmed.contains("/old/")); // wherever it pointed before no longer matters
        QVERIFY(trimmed.contains("/p/Samples")); // another product is untouched
        f.close();
        r = run({"use", "trimmed", "-s", src, "-p", dst, "--properties", props}); // again: nothing to do
        QCOMPARE(r.code, 0);
        QVERIFY2(r.out.contains("already") || r.out.contains("up to date"), qPrintable(r.out));
        r = run({"use", "source", "-s", src, "-p", dst, "--properties", props});
        QCOMPARE(r.code, 0);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().replace("\\\\", "/").contains(src.toUtf8() + "/Samples"));
        f.close();
        QFile backup(props + ".smt-backup");
        QVERIFY(backup.open(QIODevice::ReadOnly));
        QCOMPARE(backup.readAll(), original); // the very first settings
        QCOMPARE(snapshot(src), before); // the source library is never written
        QCOMPARE(run({"use", "nowhere", "-s", src, "-p", dst, "--properties", props}).code, 2);
    }

    void useRefusesAnotherProductsFolder()
    {
        const QString labs = mTmp.filePath("labs/Spitfire Originals - Labs");
        QDir().mkpath(labs);
        const QString props = mTmp.filePath("Spitfire.properties");
        const QByteArray original = "{\"BBC Symphony Orchestra\":{\"samples\":[\"/x\"]}}";
        QFile f(props);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(original);
        f.close();
        const Result r = run({"use", "trimmed", "--profile", "bbcso", "-s", labs, "-p", labs, "--properties", props});
        QCOMPARE(r.code, 1);
        QVERIFY2(r.err.contains("Labs") && r.err.contains("BBC Symphony Orchestra"), qPrintable(r.err));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), original);
        QVERIFY(!QFile::exists(props + ".smt-backup"));
    }

    void unknownSubcommandExitsTwo()
    {
        const Result r = run({"frobnicate", "-s", mSrc, "-p", mDst});
        QCOMPARE(r.code, 2);
        QVERIFY(r.err.contains("Usage:"));
        QVERIFY(r.out.isEmpty());
    }

    void generateNoLongerExists()
    {
        const Result r = run({"generate", "-s", mSrc, "-p", mDst});
        QCOMPARE(r.code, 2);
        QVERIFY(r.err.contains("Usage:"));
        QVERIFY(!QFileInfo::exists(mTmp.filePath("dst")));
    }

    void missingOptionsExitsTwo()
    {
        for (const QStringList &args : {QStringList{}, QStringList{"analyze"}, QStringList{"plan", "-s", mSrc},
                                        QStringList{"apply", "-p", mDst}, QStringList{"plan", "--bogus"},
                                        QStringList{"analyze", "-s", ""}, QStringList{"plan", "-s", "", "-p", mDst},
                                        QStringList{"plan", "-s", mSrc, "-p", " "}}) {
            const Result r = run(args, 10000);
            QVERIFY2(r.code == 2, qPrintable(args.join(' ')));
            QVERIFY(r.err.contains("Usage:"));
        }
    }

    void nonexistentSourceExitsNonZeroWithoutHanging()
    {
        const QString missing = mTmp.filePath("no-such-library");
        const Result a = run({"analyze", "-s", missing}, 10000);
        QCOMPARE(a.code, 1);
        QVERIFY2(a.err.contains("No Spitfire databases"), qPrintable(a.err));
        const Result p = run({"plan", "-s", missing, "-p", mDst}, 10000);
        QCOMPARE(p.code, 1);
        QVERIFY2(p.out.contains("REFUSED: The source library folder does not exist"), qPrintable(p.out));
        QVERIFY(!QFileInfo::exists(missing));
        QVERIFY(!QFileInfo::exists(mTmp.filePath("dst")));
    }

    void sigintCancelsCleanly()
    {
        SMT_UNIX_ONLY(); // POSIX kill(); the Windows Ctrl-C path is not testable this way
        // Enough files that the copy outlasts the signal: cancel is checked between files.
        const QString src = makeFakeLibrary(mTmp.filePath("big"), kTokens,
                                            {"brass", "percussion", "strings", "woodwinds"},
                                            100, 256 * 1024);
        const QStringList srcBefore = snapshot(src);
        QProcess p;
        start(p, {"apply", "-s", src, "-p", mDst, "-y"});
        QByteArray out;
        bool signalled = false;
        QElapsedTimer t;
        t.start();
        while (!signalled && t.elapsed() < 30000 && p.state() == QProcess::Running) {
            p.waitForReadyRead(1000);
            out += p.readAllStandardOutput();
            if (out.contains("Running") && out.contains("copy ")) {
#ifndef Q_OS_WIN
                QCOMPARE(::kill(pid_t(p.processId()), SIGINT), 0);
                QCOMPARE(::kill(pid_t(p.processId()), SIGINT), 0); // a second Ctrl-C is ignored
#endif
                signalled = true;
            }
        }
        QVERIFY2(signalled, out.constData());
        QVERIFY(p.waitForFinished(60000));
        out += p.readAllStandardOutput();
        QCOMPARE(p.exitStatus(), QProcess::NormalExit);
        QVERIFY2(p.exitCode() == 1, out.constData());
        const QByteArray err = p.readAllStandardError();
        QVERIFY2(err.contains("Cancelled. The library is still consistent"), (out + err).constData());
        QCOMPARE(err.count("Cancelling after the current file"), 1);
        QCOMPARE(snapshot(src), srcBefore);
        // Every destination DB lists only files that are present.
        for (const auto &f : scanTree(mDst)) {
            if (!f.path.endsWith(".db"))
                continue;
            const QString dir = QFileInfo(mDst + "/" + f.path).path();
            for (const QString &name : librarydb::containerNames(mDst + "/" + f.path))
                QVERIFY2(QFile::exists(dir + "/" + name), qPrintable(f.path + ": " + name));
        }
    }
};

QTEST_GUILESS_MAIN(TstCli)
#include "tst_cli.moc"
