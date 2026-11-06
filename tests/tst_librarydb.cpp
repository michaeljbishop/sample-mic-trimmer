#include <QtTest>
#include "unixonly.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QCryptographicHash>
#include "librarydb.h"
#include "testlib.h"
#ifndef Q_OS_WIN
#include <unistd.h>
#endif

using namespace smt;

static QByteArray sha(const QString &p)
{
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) qFatal("sha");
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
}

class TstLibraryDb : public QObject
{
    Q_OBJECT
private slots:
    void containerNamesListsAll()
    {
        QTemporaryDir d;
        const QString p = d.filePath("a.db");
        makeDb(p, {"A.spitfire", "B.spitfire", "C.spitfire"});
        QString err;
        auto names = librarydb::containerNames(p, &err);
        names.sort();
        QCOMPARE(names, (QStringList{"A.spitfire", "B.spitfire", "C.spitfire"}));
        QVERIFY2(err.isEmpty(), qPrintable(err));
    }
    void patchRemovesOnlyUnlistedContainers()
    {
        QTemporaryDir d;
        const QString src = d.filePath("a.db"), dst = d.filePath("b.db");
        makeDb(src, {"A.spitfire", "B.spitfire", "C.spitfire"});
        QString err;
        QVERIFY2(librarydb::writePatchedCopy(src, dst, {"A.spitfire", "C.spitfire"}, &err), qPrintable(err));
        auto names = librarydb::containerNames(dst);
        names.sort();
        QCOMPARE(names, (QStringList{"A.spitfire", "C.spitfire"}));
        int files = -1;
        const QString conn = "t_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
            db.setDatabaseName(dst);
            QVERIFY(db.open());
            QSqlQuery q("SELECT COUNT(*) FROM Files", db);
            QVERIFY(q.next());
            files = q.value(0).toInt();
        }
        QSqlDatabase::removeDatabase(conn);
        QCOMPARE(files, 9);
    }
    void sourceDbUnchanged()
    {
        QTemporaryDir d;
        const QString src = d.filePath("a.db");
        makeDb(src, {"A.spitfire", "B.spitfire"});
        const QByteArray before = sha(src);
        QString err;
        QVERIFY2(librarydb::writePatchedCopy(src, d.filePath("b.db"), {"A.spitfire"}, &err), qPrintable(err));
        QCOMPARE(sha(src), before);
    }
    void badSourceReturnsFalseAndNoDest()
    {
        QTemporaryDir d;
        const QString dst = d.filePath("b.db");
        QString err;
        QVERIFY(!librarydb::writePatchedCopy(d.filePath("missing.db"), dst, {}, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!QFile::exists(dst));
        writeFile(d.filePath("junk.db"), 4096);
        QVERIFY(!librarydb::writePatchedCopy(d.filePath("junk.db"), dst, {}, &err));
        QVERIFY(!QFile::exists(dst));
    }
    void destEqualsSourceRefusedAndSourceIntact()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir d;
        const QString src = d.filePath("a.db");
        makeDb(src, {"A.spitfire", "B.spitfire"});
        const QByteArray before = sha(src);
        QVERIFY(QFile::link(src, d.filePath("sym.db")));
#ifndef Q_OS_WIN
        QVERIFY(::link(QFile::encodeName(src).constData(), QFile::encodeName(d.filePath("hard.db")).constData()) == 0);
#endif
        for (const QString &dst : {src, d.path() + "/./a.db", d.filePath("sym.db"), d.filePath("hard.db")}) {
            QString err;
            QVERIFY2(!librarydb::writePatchedCopy(src, dst, {"A.spitfire"}, &err), qPrintable(dst));
            QVERIFY2(err.contains("same file"), qPrintable(err));
            QCOMPARE(sha(src), before);
            QVERIFY(QFileInfo::exists(dst));
        }
    }
    void keepMatchIsCaseSensitive()
    {
        QTemporaryDir d;
        const QString src = d.filePath("a.db"), dst = d.filePath("b.db");
        makeDb(src, {"A.spitfire", "B.spitfire"});
        QString err;
        QVERIFY2(librarydb::writePatchedCopy(src, dst, {"a.spitfire", "B.spitfire"}, &err), qPrintable(err));
        QCOMPARE(librarydb::containerNames(dst), QStringList{"B.spitfire"});
    }
    void preExistingDestReplaced()
    {
        QTemporaryDir d;
        const QString src = d.filePath("a.db"), dst = d.filePath("b.db");
        makeDb(src, {"A.spitfire", "B.spitfire"});
        makeDb(dst, {"OLD.spitfire"});
        QString err;
        QVERIFY2(librarydb::writePatchedCopy(src, dst, {"A.spitfire"}, &err), qPrintable(err));
        QCOMPARE(librarydb::containerNames(dst), QStringList{"A.spitfire"});
    }
    void failingDeleteIsAnError()
    {
        QTemporaryDir d;
        const QString src = d.filePath("a.db"), dst = d.filePath("b.db");
        makeDb(src, {"A.spitfire", "B.spitfire"});
        execSql(src, "CREATE TRIGGER nodelete BEFORE DELETE ON Containers BEGIN SELECT RAISE(ABORT, 'blocked'); END");
        QString err;
        QVERIFY(!librarydb::writePatchedCopy(src, dst, {"A.spitfire"}, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!QFile::exists(dst));
    }
    void containerNamesErrorPath()
    {
        QTemporaryDir d;
        writeFile(d.filePath("junk.db"), 4096);
        for (const QString &p : {d.filePath("missing.db"), d.filePath("junk.db")}) {
            QString err;
            QVERIFY(librarydb::containerNames(p, &err).isEmpty());
            QVERIFY2(!err.isEmpty(), qPrintable(p));
        }
    }
    void containerNamesClearsErrorOnSuccess()
    {
        QTemporaryDir d;
        const QString p = d.filePath("a.db");
        makeDb(p, {"A.spitfire"});
        QString err = "stale";
        QCOMPARE(librarydb::containerNames(p, &err), QStringList{"A.spitfire"});
        QVERIFY2(err.isEmpty(), qPrintable(err));
    }
};

QTEST_MAIN(TstLibraryDb)
#include "tst_librarydb.moc"
