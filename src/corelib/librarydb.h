#pragma once
#include <QSet>
#include <QString>
#include <QStringList>

namespace smt::librarydb {

// Every Containers.File in the DB (opened read-only).
QStringList containerNames(const QString &dbPath, QString *error = nullptr);

// Copies sourceDb to destDb and deletes Containers rows not in keepFileNames
// (Files rows are left alone). Removes destDb and returns false on any failure.
// destDb must be a temp/staging path (the caller swaps it into place); it is removed first.
// Refuses, touching nothing, if destDb is the same file as sourceDb.
bool writePatchedCopy(const QString &sourceDb, const QString &destDb,
                      const QSet<QString> &keepFileNames, QString *error);

} // namespace smt::librarydb
