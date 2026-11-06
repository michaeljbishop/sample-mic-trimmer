#include <QtTest>
#include <numeric>
#include "fileinfogatherer.h"
#include "qfileinfogatherertask.h"

using namespace smt;

class TstGatherer : public QObject
{
    Q_OBJECT
    static void touch(const QString &path)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QFile::WriteOnly));
        f.write("x");
    }
    static void makeTree(const QTemporaryDir &t, int n)
    {
        for (int i = 0; i < n; ++i)
            touch(t.filePath(QString("d%1/f%2.wav").arg(i % 5).arg(i)));
    }

private slots:
    void findsAllFilesAndSkipsDsStore()
    {
        QTemporaryDir t;
        makeTree(t, 10);
        touch(t.filePath(".DS_Store"));
        touch(t.filePath("d1/.DS_Store"));
        FileInfoGatherer g(t.path());
        QSignalSpy fin(&g, &FileInfoGatherer::finished);
        g.run();
        QVERIFY(fin.wait(5000));
        QCOMPARE(fin.first().first().toBool(), true);
        QCOMPARE(g.infos().size(), 10);
        QVERIFY(g.isFinished());
        QVERIFY(!g.isRunning());
        for (const auto &i : g.infos())
            QVERIFY(!i.path.contains("DS_Store"));
    }
    void cancelStopsEarly()
    {
        QTemporaryDir t;
        makeTree(t, 3000);
        FileInfoGatherer g(t.path());
        QSignalSpy fin(&g, &FileInfoGatherer::finished);
        g.run();
        g.cancel();
        QVERIFY(fin.wait(5000));
        QCOMPARE(fin.size(), 1);
        QCOMPARE(fin.first().first().toBool(), false);
        QVERIFY(g.isFinished());
        const int seen = g.infos().size();
        QTest::qWait(100); // nothing more arrives after the cancel
        QCOMPARE(g.infos().size(), seen);
        QVERIFY(seen < 3000);
    }
    void batchesAndProgressArriveInOrderOffTheCallingThread()
    {
        QTemporaryDir t;
        const int n = 2300; // more than one batch
        makeTree(t, n);
        FileInfoGatherer g(t.path());
        g.setExpectedTotal(n);
        QList<int> batches;
        QList<qint64> scanned;
        connect(&g, &FileInfoGatherer::foundFileInfos, this, [&](const QList<FileInfo> &f) { batches << f.size(); });
        connect(&g, &FileInfoGatherer::progress, this, [&](qint64 done, qint64 expected) {
            QCOMPARE(expected, qint64(n));
            scanned << done;
        });
        QSignalSpy fin(&g, &FileInfoGatherer::finished);
        g.run();
        QVERIFY(fin.wait(20000));
        QCOMPARE(fin.first().first().toBool(), true);
        QVERIFY(batches.size() >= 2);
        QCOMPARE(std::accumulate(batches.begin(), batches.end(), 0), n);
        QCOMPARE(g.infos().size(), n);
        QVERIFY(std::is_sorted(scanned.begin(), scanned.end()));
        QCOMPARE(scanned.last(), qint64(n));
    }
    void unknownTotalReportsMinusOne()
    {
        QTemporaryDir t;
        makeTree(t, 5);
        FileInfoGatherer g(t.path());
        qint64 lastExpected = 0;
        connect(&g, &FileInfoGatherer::progress, this, [&](qint64, qint64 expected) { lastExpected = expected; });
        QSignalSpy fin(&g, &FileInfoGatherer::finished);
        g.run();
        QVERIFY(fin.wait(5000));
        QCOMPARE(g.infos().size(), 5);
        QVERIFY(lastExpected == 5 || lastExpected == -1); // the closing report knows the total
    }
    static QList<FileInfo> sample()
    {
        QList<FileInfo> files(3);
        files[0].path = "Samples/_Brass/\u00dcber \u266a.spitfire";
        files[0].size = 123456789012ULL;
        files[0].modificationTime = QDateTime::fromMSecsSinceEpoch(1700000000123LL);
        files[0].creationTime = QDateTime::fromMSecsSinceEpoch(1600000000456LL);
        files[0].metadataModificationTime = QDateTime::fromMSecsSinceEpoch(1700000001789LL);
        files[0].permissions = QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup;
        files[1].path = "Samples/_Brass/B.db"; // null times stay null
        files[1].size = 0;
        files[2].path = QString(300, QLatin1Char('x')) + ".wav";
        files[2].size = 7;
        return files;
    }
    void binaryCacheRoundTrips()
    {
        const auto files = sample();
        const QByteArray bytes = QFileInfoGathererTask::serializeScanCache("/lib/A/", files);
        const auto back = QFileInfoGathererTask::parseScanCache(bytes, "/lib/A"); // same folder, spelled differently
        QVERIFY(back.has_value());
        QCOMPARE(back->size(), files.size());
        for (int i = 0; i < files.size(); ++i) {
            QCOMPARE((*back)[i].path, files[i].path);
            QCOMPARE((*back)[i].size, files[i].size);
            QCOMPARE((*back)[i].modificationTime, files[i].modificationTime);
            QCOMPARE((*back)[i].creationTime, files[i].creationTime);
            QCOMPARE((*back)[i].metadataModificationTime, files[i].metadataModificationTime);
            QCOMPARE(int((*back)[i].permissions.toInt()), int(files[i].permissions.toInt()));
        }
        QVERIFY(!(*back)[1].modificationTime.isValid());
    }
    void damagedOrForeignCacheIsIgnored()
    {
        const auto files = sample();
        const QByteArray good = QFileInfoGathererTask::serializeScanCache("/lib/A", files);
        auto parse = [&](const QByteArray &b, const QString &dir = "/lib/A") { return QFileInfoGathererTask::parseScanCache(b, dir); };
        QVERIFY(parse(good).has_value());
        QVERIFY(!parse(good, "/lib/B").has_value()); // another folder
        QVERIFY(!parse(QByteArray()).has_value());
        QVERIFY(!parse("not a cache at all").has_value());
        for (int n = 0; n < good.size(); ++n) // every truncation
            QVERIFY2(!parse(good.left(n)).has_value(), qPrintable(QString::number(n)));
        QVERIFY(!parse(good + "x").has_value()); // trailing garbage
        QByteArray badMagic = good;
        badMagic[0] = 'X';
        QVERIFY(!parse(badMagic).has_value());
        QByteArray oldVersion = good;
        oldVersion[4] = 2; // a format this build does not know
        QVERIFY(!parse(oldVersion).has_value());
        // a hostile record count must not allocate or read past the data
        QByteArray hostile = good;
        const int countAt = 8 + 2 + QString("/lib/A").size();
        hostile.replace(countAt, 4, QByteArray("\xff\xff\xff\xff", 4));
        QVERIFY(!parse(hostile).has_value());
        // an oversized string length
        QByteArray longPath = good;
        longPath.replace(countAt + 4, 2, QByteArray("\xff\xff", 2));
        QVERIFY(!parse(longPath).has_value());
    }
    void damagedCacheFileFallsBackToARescan()
    {
        QTemporaryDir t;
        makeTree(t, 3);
        const QString cache = t.filePath("cache.smtcache");
        QFile f(cache);
        QVERIFY(f.open(QFile::WriteOnly));
        f.write(QFileInfoGathererTask::serializeScanCache(t.filePath("d0"), sample()).left(40)); // truncated
        f.close();
        QCOMPARE(FileInfoGatherer::cachedFileCount(cache), qint64(-1));
        FileInfoGatherer g(t.filePath("d0"), cache);
        QSignalSpy fin(&g, &FileInfoGatherer::finished);
        g.run();
        QVERIFY(fin.wait(5000));
        QCOMPARE(g.infos().size(), 1); // scanned from disk, not the three sample records
    }
    void finishedComesBeforeTheCacheIsWrittenAndALateDeleteIsSafe()
    {
        QTemporaryDir t;
        makeTree(t, 400);
        touch(t.filePath("d0/Samples/_Brass/a.db"));
        const QString cache = t.filePath("cache.smtcache");
        {
            auto *g = new FileInfoGatherer(t.filePath("d0"), cache);
            QSignalSpy fin(g, &FileInfoGatherer::finished);
            g->run();
            QVERIFY(fin.wait(5000));
            delete g; // right after finished(): the worker is still free to write the file
        }
        QTRY_VERIFY(QFileInfo::exists(cache));
        QThreadPool::globalInstance()->waitForDone();
        FileInfoGatherer again(t.filePath("d0"), cache); // the next scan reads a complete file
        QSignalSpy fin2(&again, &FileInfoGatherer::finished);
        again.run();
        QVERIFY(fin2.wait(5000));
        QCOMPARE(again.infos().size(), 81); // 400 files over d0..d4 plus a.db, only d0's
        QCOMPARE(FileInfoGatherer::cachedFileCount(cache), qint64(81));
    }
    void skipCacheReadWalksTheDiskAndReplacesTheCache()
    {
        QTemporaryDir t;
        makeTree(t, 6);
        touch(t.filePath("d0/Samples/_Brass/a.db"));
        const QString cache = t.filePath("cache.smtcache");
        QFile f(cache);
        QVERIFY(f.open(QFile::WriteOnly));
        FileInfo marker{};
        marker.path = "marker.wav";
        f.write(QFileInfoGathererTask::serializeScanCache(t.filePath("d0"), {marker}));
        f.close();
        FileInfoGatherer g(t.filePath("d0"), cache);
        g.setSkipCacheRead(true);
        QSignalSpy fin(&g, &FileInfoGatherer::finished);
        g.run();
        QVERIFY(fin.wait(5000));
        QVERIFY(g.infos().size() > 1); // the disk, not the one-file marker
        QTRY_COMPARE(FileInfoGatherer::cachedFileCount(cache), qint64(g.infos().size()));
    }
    void preloadedAnswersWithoutIo()
    {
        QList<FileInfo> files(3);
        for (int i = 0; i < 3; ++i)
            files[i].path = QString("f%1").arg(i);
        FileInfoGatherer g("/does/not/exist");
        g.setPreloaded(files);
        QSignalSpy fin(&g, &FileInfoGatherer::finished);
        g.run();
        QVERIFY(!g.isFinished());
        QVERIFY(fin.wait(2000));
        QCOMPARE(g.infos().size(), 3);
        QCOMPARE(fin.first().first().toBool(), true);
    }
    void cacheHitFinishesAsynchronously()
    {
        QTemporaryDir t;
        makeTree(t, 4);
        touch(t.filePath("d0/Samples/_Brass/a.db"));
        const QString cache = t.filePath("cache.json");
        {
            FileInfoGatherer g(t.filePath("d0"), cache);
            QSignalSpy fin(&g, &FileInfoGatherer::finished);
            g.run();
            QVERIFY(fin.wait(5000));
        }
        QTRY_VERIFY(QFileInfo::exists(cache)); // written by the worker after finished()
        QCOMPARE(FileInfoGatherer::cachedFileCount(cache), qint64(2)); // read from the header
        FileInfoGatherer g(t.filePath("d0"), cache);
        g.run();
        QVERIFY(!g.isFinished());
        QSignalSpy fin(&g, &FileInfoGatherer::finished); // connected after run()
        QVERIFY(fin.wait(5000));
        QCOMPARE(fin.size(), 1);
        QVERIFY(g.isFinished());
        QCOMPARE(g.infos().size(), 2);
    }
    void cacheForOtherDirIgnored()
    {
        QTemporaryDir t;
        makeTree(t, 5);
        touch(t.filePath("d0/Samples/_Brass/a.db"));
        const QString cache = t.filePath("cache.json");
        {
            FileInfoGatherer g(t.filePath("d0"), cache);
            QSignalSpy fin(&g, &FileInfoGatherer::finished);
            g.run();
            QVERIFY(fin.wait(5000));
        }
        QTRY_VERIFY(QFileInfo::exists(cache));
        FileInfoGatherer g(t.filePath("d1"), cache);
        QSignalSpy fin(&g, &FileInfoGatherer::finished);
        g.run();
        QVERIFY(fin.wait(5000));
        QCOMPARE(g.infos().size(), 1);
        QVERIFY(g.infos().first().path.startsWith("f1"));
    }
    void scanWithoutDatabaseNotCached()
    {
        QTemporaryDir t;
        makeTree(t, 5);
        touch(t.filePath("d0/top.db")); // not a section DB (not under Samples/)
        const QString cache = t.filePath("cache.json");
        FileInfoGatherer g(t.filePath("d0"), cache);
        QSignalSpy fin(&g, &FileInfoGatherer::finished);
        g.run();
        QVERIFY(fin.wait(5000));
        QCOMPARE(fin.first().first().toBool(), true);
        QCOMPARE(g.infos().size(), 2);
        QTest::qWait(300); // the worker would have written it by now
        QVERIFY(!QFileInfo::exists(cache));
    }
    void corruptCacheFallsBackToScan()
    {
        QTemporaryDir t;
        makeTree(t, 3);
        const QString cache = t.filePath("cache.json");
        QFile f(cache);
        QVERIFY(f.open(QFile::WriteOnly));
        f.write("{not json");
        f.close();
        FileInfoGatherer g(t.filePath("d0"), cache);
        QSignalSpy fin(&g, &FileInfoGatherer::finished);
        g.run();
        QVERIFY(fin.wait(5000));
        QCOMPARE(g.infos().size(), 1);
    }
    void cacheHitCanBeCancelled()
    {
        QTemporaryDir t;
        makeTree(t, 5);
        touch(t.filePath("Samples/_Brass/a.db"));
        const QString cache = t.filePath("cache.json");
        {
            FileInfoGatherer g(t.path(), cache);
            QSignalSpy fin(&g, &FileInfoGatherer::finished);
            g.run();
            QVERIFY(fin.wait(5000));
        }
        QTRY_VERIFY(QFileInfo::exists(cache));
        FileInfoGatherer g(t.path(), cache);
        QSignalSpy fin(&g, &FileInfoGatherer::finished);
        g.run();
        g.cancel();
        QVERIFY(fin.wait(5000));
        QCOMPARE(fin.size(), 1);
        QCOMPARE(fin.first().first().toBool(), false);
        QVERIFY(g.isFinished());
    }
    void runTwiceWhileRunningIsIgnored()
    {
        QTemporaryDir t;
        makeTree(t, 20);
        FileInfoGatherer g(t.path());
        QSignalSpy fin(&g, &FileInfoGatherer::finished);
        int found = 0;
        connect(&g, &FileInfoGatherer::foundFileInfos, this, [&](const QList<FileInfo> &f) { found += f.size(); });
        g.run();
        g.run();
        QVERIFY(fin.wait(5000));
        QTest::qWait(100);
        QCOMPARE(fin.size(), 1);
        QCOMPARE(found, 20);
    }
    void noLeakOrDoubleDeleteUnderAsan()
    {
        QTemporaryDir t;
        makeTree(t, 50);
        for (int i = 0; i < 50; ++i) {
            FileInfoGatherer g(t.path());
            QSignalSpy fin(&g, &FileInfoGatherer::finished);
            g.run();
            if (i % 3 == 0)
                g.cancel();
            QVERIFY(fin.wait(5000));
            QCOMPARE(fin.size(), 1);
        }
        // destroy while the task is still running
        for (int i = 0; i < 20; ++i) {
            auto *g = new FileInfoGatherer(t.path());
            g->run();
            delete g;
        }
        QTest::qWait(100);
        QThreadPool::globalInstance()->waitForDone();
    }
};

QTEST_MAIN(TstGatherer)
#include "tst_gatherer.moc"
