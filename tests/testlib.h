#pragma once
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

// Creates a DB with the real Spitfire two-table schema; 3 Files rows per container.
inline void makeDb(const QString &path, const QStringList &containers)
{
    const QString conn = "makeDb_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
        db.setDatabaseName(path);
        if (!db.open()) qFatal("makeDb: cannot open %s", qPrintable(path));
        QSqlQuery q(db);
        q.exec("CREATE TABLE Containers(ID INTEGER PRIMARY KEY AUTOINCREMENT UNIQUE, File TEXT NOT NULL UNIQUE)");
        q.exec("CREATE TABLE Files(ID INTEGER NOT NULL UNIQUE PRIMARY KEY, Container INTEGER NOT NULL REFERENCES Containers(ID))");
        // One transaction: every insert on its own commit is one disk flush each, which is very slow on Windows.
        db.transaction();
        int fileId = 1;
        for (const QString &c : containers) {
            q.prepare("INSERT INTO Containers(File) VALUES(?)");
            q.addBindValue(c);
            if (!q.exec()) qFatal("makeDb: insert failed");
            const QVariant cid = q.lastInsertId();
            for (int i = 0; i < 3; ++i) {
                QSqlQuery f(db);
                f.prepare("INSERT INTO Files(ID, Container) VALUES(?, ?)");
                f.addBindValue(fileId++);
                f.addBindValue(cid);
                if (!f.exec()) qFatal("makeDb: files insert failed");
            }
        }
        if (!db.commit()) qFatal("makeDb: commit failed");
    }
    QSqlDatabase::removeDatabase(conn);
}

// Runs one statement on an existing DB (e.g. to add a trigger).
inline void execSql(const QString &path, const QString &sql)
{
    const QString conn = "execSql_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
        db.setDatabaseName(path);
        if (!db.open()) qFatal("execSql: cannot open %s", qPrintable(path));
        QSqlQuery q(db);
        if (!q.exec(sql)) qFatal("execSql: %s failed", qPrintable(sql));
    }
    QSqlDatabase::removeDatabase(conn);
}

inline void writeFile(const QString &path, qint64 size)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) qFatal("writeFile: cannot open %s", qPrintable(path));
    f.write(QByteArray(int(size), 'x'));
}
