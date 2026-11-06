#include <QtTest>
#include "unixonly.h"
#include <QRandomGenerator>
#include "copier.h"

using namespace smt;

static const qint64 kBuf = 64 * 1024;

static QByteArray randomBytes(qint64 size, quint32 seed)
{
    QByteArray data(size, Qt::Uninitialized);
    QRandomGenerator rng(seed);
    for (qint64 i = 0; i < size; ++i)
        data[i] = char(rng.generate() & 0xff);
    return data;
}

static bool writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static CopyOptions smallOpts(int concurrency = 2)
{
    CopyOptions o;
    o.bufferBytes = kBuf;
    o.concurrency = concurrency;
    return o;
}

class TstCopier : public QObject
{
    Q_OBJECT

private slots:
    void copiesBytesExactly()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir tmp;
        const QByteArray data = randomBytes(5 * kBuf + 123, 1);
        const QString src = tmp.filePath(QStringLiteral("src/a.bin"));
        const QString dst = tmp.filePath(QStringLiteral("dst/sub/a.bin"));
        QVERIFY(writeFile(src, data));
        // read-only source: the copier must never open it for writing
        QVERIFY(QFile::setPermissions(src, QFileDevice::ReadOwner));
        std::atomic_bool cancel{false};
        QString err;
        QVERIFY2(copyFiles({{src, dst, data.size(), {}}}, smallOpts(), {}, cancel, &err), qPrintable(err));
        QCOMPARE(readFile(dst), data);
        QVERIFY(!QFile::exists(dst + QStringLiteral(".part")));
    }

    void progressDeltasSumToTotal()
    {
        QTemporaryDir tmp;
        QList<CopyJob> jobs;
        qint64 total = 0;
        for (int i = 0; i < 3; ++i) {
            const QByteArray data = randomBytes(3 * kBuf + i * 1000, 10 + i);
            const QString src = tmp.filePath(QStringLiteral("src/%1.bin").arg(i));
            QVERIFY(writeFile(src, data));
            jobs.append({src, tmp.filePath(QStringLiteral("dst/%1.bin").arg(i)), data.size(), {}});
            total += data.size();
        }
        std::atomic<qint64> sum{0};
        std::atomic_bool cancel{false};
        QString err;
        QVERIFY2(copyFiles(jobs, smallOpts(), [&](qint64 d) { sum += d; }, cancel, &err), qPrintable(err));
        QCOMPARE(sum.load(), total);
    }

    void skipsExistingSameSize()
    {
        QTemporaryDir tmp;
        const QByteArray data = randomBytes(2 * kBuf, 2);
        const QByteArray existing(data.size(), 'x');
        const QString src = tmp.filePath(QStringLiteral("a.bin"));
        const QString dst = tmp.filePath(QStringLiteral("b.bin"));
        QVERIFY(writeFile(src, data));
        QVERIFY(writeFile(dst, existing));
        std::atomic<qint64> sum{0};
        std::atomic_bool cancel{false};
        QString err;
        QVERIFY2(copyFiles({{src, dst, data.size(), {}}}, smallOpts(), [&](qint64 d) { sum += d; }, cancel, &err),
                 qPrintable(err));
        QCOMPARE(readFile(dst), existing);
        QCOMPARE(sum.load(), qint64(data.size()));
    }

    void cancelLeavesNoPartFile()
    {
        QTemporaryDir tmp;
        const QByteArray data = randomBytes(20 * kBuf, 3);
        const QString src = tmp.filePath(QStringLiteral("a.bin"));
        const QString dst = tmp.filePath(QStringLiteral("b.bin"));
        QVERIFY(writeFile(src, data));
        std::atomic_bool cancel{false};
        QString err;
        QVERIFY(!copyFiles({{src, dst, data.size(), {}}}, smallOpts(), [&](qint64) { cancel = true; }, cancel, &err));
        QCOMPARE(err, QStringLiteral("cancelled"));
        QVERIFY(!QFile::exists(dst + QStringLiteral(".part")));
        QVERIFY(!QFile::exists(dst));
    }

    void missingSourceFailsWithError()
    {
        QTemporaryDir tmp;
        const QString src = tmp.filePath(QStringLiteral("missing.bin"));
        const QString dst = tmp.filePath(QStringLiteral("b.bin"));
        std::atomic_bool cancel{false};
        QString err;
        QVERIFY(!copyFiles({{src, dst, 10, {}}}, smallOpts(), {}, cancel, &err));
        QVERIFY2(err.startsWith(src + ':'), qPrintable(err));
        QVERIFY2(err.contains(QStringLiteral("rescan the source library")), qPrintable(err));
        QVERIFY(!QFile::exists(dst));
        QVERIFY(!QFile::exists(dst + QStringLiteral(".part")));
    }

    void modificationTimePreserved()
    {
        QTemporaryDir tmp;
        const QByteArray data = randomBytes(kBuf + 7, 4);
        const QString src = tmp.filePath(QStringLiteral("a.bin"));
        const QString dst = tmp.filePath(QStringLiteral("b.bin"));
        QVERIFY(writeFile(src, data));
        const QDateTime when(QDate(2020, 1, 2), QTime(3, 4, 5), QTimeZone::UTC);
        std::atomic_bool cancel{false};
        QString err;
        QVERIFY2(copyFiles({{src, dst, data.size(), when}}, smallOpts(), {}, cancel, &err), qPrintable(err));
        QCOMPARE(QFileInfo(dst).lastModified().toSecsSinceEpoch(), when.toSecsSinceEpoch());
    }

    void differentSizeDestReplaced()
    {
        QTemporaryDir tmp;
        const QByteArray data = randomBytes(2 * kBuf + 5, 5);
        const QString src = tmp.filePath(QStringLiteral("a.bin"));
        const QString dst = tmp.filePath(QStringLiteral("b.bin"));
        QVERIFY(writeFile(src, data));
        QVERIFY(writeFile(dst, QByteArray(17, 'x')));
        std::atomic_bool cancel{false};
        QString err;
        QVERIFY2(copyFiles({{src, dst, data.size(), {}}}, smallOpts(), {}, cancel, &err), qPrintable(err));
        QCOMPARE(readFile(dst), data);
        QVERIFY(!QFile::exists(dst + QStringLiteral(".part")));
    }

    void sizeMismatchFailsWithNoPartAndNoDest()
    {
        QTemporaryDir tmp;
        const QByteArray data = randomBytes(3 * kBuf, 6);
        const QString src = tmp.filePath(QStringLiteral("a.bin"));
        const QString dst = tmp.filePath(QStringLiteral("b.bin"));
        QVERIFY(writeFile(src, data));
        for (const qint64 wrong : {data.size() + 5, data.size() - 5}) {
            std::atomic_bool cancel{false};
            QString err;
            QVERIFY(!copyFiles({{src, dst, wrong, {}}}, smallOpts(), {}, cancel, &err));
            QVERIFY2(err.startsWith(src + QStringLiteral(": size changed")), qPrintable(err));
            QVERIFY2(err.contains(QStringLiteral("rescan the source library")), qPrintable(err));
            QVERIFY(!QFile::exists(dst));
            QVERIFY(!QFile::exists(dst + QStringLiteral(".part")));
        }
    }

    void firstErrorWinsAndOthersStop()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir tmp;
        QList<CopyJob> jobs;
        for (int i = 0; i < 6; ++i) {
            const QString src = tmp.filePath(QStringLiteral("src/%1.bin").arg(i));
            QVERIFY(writeFile(src, randomBytes(kBuf, 200 + i)));
            jobs.append({src, tmp.filePath(QStringLiteral("dst/%1.bin").arg(i)), kBuf, {}});
        }
        for (int i = 0; i < 2; ++i) // two unreadable sources first
            QVERIFY(QFile::setPermissions(jobs[i].source, {}));
        std::atomic_bool cancel{false};
        QString err;
        QVERIFY(!copyFiles(jobs, smallOpts(1), {}, cancel, &err));
        QVERIFY2(err.startsWith(jobs[0].source + ':'), qPrintable(err));
        for (const auto &j : jobs) {
            QVERIFY2(!QFile::exists(j.dest), qPrintable(j.dest));
            QVERIFY(!QFile::exists(j.dest + QStringLiteral(".part")));
        }
    }

    void exceptionInWorkerIsAnErrorNotACrash()
    {
        QTemporaryDir tmp;
        QList<CopyJob> jobs;
        for (int i = 0; i < 3; ++i) {
            const QString src = tmp.filePath(QStringLiteral("src/%1.bin").arg(i));
            QVERIFY(writeFile(src, randomBytes(8 * kBuf, 300 + i)));
            jobs.append({src, tmp.filePath(QStringLiteral("dst/%1.bin").arg(i)), 8 * kBuf, {}});
        }
        std::atomic_bool cancel{false};
        QString err;
        // throws on the first buffer, while that file's reader is still running
        QVERIFY(!copyFiles(jobs, smallOpts(1), [](qint64) { throw std::runtime_error("boom"); }, cancel, &err));
        QVERIFY2(err == jobs[0].source + QStringLiteral(": unexpected exception while copying"), qPrintable(err));
        for (const auto &j : jobs) {
            QVERIFY(!QFile::exists(j.dest));
            QVERIFY(!QFile::exists(j.dest + QStringLiteral(".part")));
        }
    }

    void manyFilesConcurrently()
    {
        QTemporaryDir tmp;
        QList<CopyJob> jobs;
        QList<QByteArray> datas;
        for (int i = 0; i < 20; ++i) {
            datas.append(randomBytes(kBuf * (i % 4) + 17 * i, 100 + i));
            const QString src = tmp.filePath(QStringLiteral("src/%1.bin").arg(i));
            QVERIFY(writeFile(src, datas.last()));
            jobs.append({src, tmp.filePath(QStringLiteral("dst/%1.bin").arg(i)), datas.last().size(), {}});
        }
        std::atomic_bool cancel{false};
        QString err;
        QVERIFY2(copyFiles(jobs, smallOpts(4), {}, cancel, &err), qPrintable(err));
        for (int i = 0; i < 20; ++i)
            QCOMPARE(readFile(jobs[i].dest), datas[i]);
    }
};

QTEST_GUILESS_MAIN(TstCopier)
#include "tst_copier.moc"
