#include "librarydb.h"
#include "fsutil.h"
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <filesystem>

namespace smt::librarydb {

namespace {
// Owns a uniquely named connection and removes it after the db/queries are gone.
struct Conn {
    QString name = "librarydb_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    ~Conn() { QSqlDatabase::removeDatabase(name); }
};

void setError(QString *e, const QString &m) { if (e) *e = m; }
}

QStringList containerNames(const QString &dbPath, QString *error)
{
    QStringList out;
    if (!QFile::exists(dbPath)) { setError(error, "Database not found: " + dbPath); return out; }
    Conn c;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", c.name);
        db.setDatabaseName(dbPath);
        db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (!db.open()) { setError(error, db.lastError().text()); return out; }
        QSqlQuery q(db);
        if (!q.exec("SELECT File FROM Containers")) { setError(error, q.lastError().text()); return out; }
        while (q.next()) out << q.value(0).toString();
    }
    setError(error, {});
    return out;
}

bool writePatchedCopy(const QString &sourceDb, const QString &destDb,
                      const QSet<QString> &keepFileNames, QString *error)
{
    // Before anything can remove destDb: it may be the source itself (same path, link, hard link).
    std::error_code ec;
    if (std::filesystem::equivalent(fs::path(sourceDb), fs::path(destDb), ec)) {
        setError(error, "Refusing to patch: " + destDb + " is the same file as " + sourceDb);
        return false;
    }
    auto fail = [&](const QString &m) { setError(error, m); QFile::remove(destDb); return false; };
    if (!QFile::exists(sourceDb)) return fail("Database not found: " + sourceDb);
    QFile::remove(destDb);
    if (!QFile::copy(sourceDb, destDb)) return fail("Cannot copy " + sourceDb + " to " + destDb);
    QFile::setPermissions(destDb, QFile::permissions(destDb) | QFile::WriteOwner);

    bool failed = false;
    QString problem;
    // Every failure goes through here, so success never depends on an error text being non-empty.
    auto bad = [&](const QString &text, const QString &fallback) {
        failed = true;
        problem = text.trimmed().isEmpty() ? fallback : text;
    };
    {
        Conn c; // destroyed after db/queries, before the file may be removed
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", c.name);
        db.setDatabaseName(destDb);
        if (!db.open()) bad(db.lastError().text(), "cannot open " + destDb);
        else {
            QSqlQuery q(db);
            QStringList doomed;
            if (!q.exec("SELECT File FROM Containers")) bad(q.lastError().text(), "cannot read Containers");
            else while (q.next())
                if (!keepFileNames.contains(q.value(0).toString())) doomed << q.value(0).toString();
            if (!failed && !db.transaction()) bad(db.lastError().text(), "cannot start a transaction");
            QSqlQuery del(db);
            if (!failed && !del.prepare("DELETE FROM Containers WHERE File = ?"))
                bad(del.lastError().text(), "cannot prepare the DELETE");
            for (int i = 0; !failed && i < doomed.size(); ++i) {
                del.bindValue(0, doomed.at(i));
                if (!del.exec()) bad(del.lastError().text(), "cannot delete the row for " + doomed.at(i));
            }
            if (!failed && !db.commit()) bad(db.lastError().text(), "cannot commit");
            if (!failed) {
                if (!q.exec("PRAGMA integrity_check")) bad(q.lastError().text(), "integrity_check failed");
                else if (!q.next()) bad({}, "integrity_check returned no rows");
                else if (q.value(0).toString() != "ok") bad("integrity_check: " + q.value(0).toString(), "integrity_check failed");
            }
        }
    }
    return failed ? fail(problem) : true;
}

} // namespace smt::librarydb
