#pragma once
#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include "smt.h"

namespace smt {

struct PlanStep
{
    enum class Type { ReplaceDb, DeleteFile, CopyToStaging, MoveIntoPlace };
    enum class Status { Pending, Running, Done, Failed };
    Type type = Type::DeleteFile;
    int phase = 1;               // 1 or 2
    QString relPath;             // relative to source/dest root, '/' separated
    qint64 bytes = 0;            // Delete: dest size; Copy/Move/ReplaceDb: source size
    QSet<QString> keepFileNames; // ReplaceDb only: bare file names the DB may list
    QDateTime modified;          // CopyToStaging only: source modification time, kept on the copy
    bool refresh = false;        // CopyToStaging and MoveIntoPlace: a settings file, copied again and moved over whatever the destination or staging holds
    Status status = Status::Pending;
    QString error;
    QString describe() const; // one line, shared by GUI and CLI
};

struct PlanInput
{
    ProductProfile profile;
    QString sourceRoot, destRoot;
    QList<FileInfo> source, dest;
    MicStatuses mics;
    qint64 freeBytes = 0;
};

struct Plan
{
    QList<PlanStep> steps;
    qint64 bytesToAdd = 0, bytesToFree = 0;
    qint64 bytesForDatabases = 0; // source sizes of the phase-2 database copies
    qint64 bytesAlreadyStaged = 0; // part of bytesToAdd already complete in staging (needs no new space)
    qint64 freeBytesAfter = 0;
    QStringList refusals;
    // What the plan was built from; Core refuses to run a plan whose stamp no longer matches.
    QString builtForSource, builtForDest;
    quint64 generation = 0;
    bool isEmpty() const { return steps.isEmpty(); }
    bool hasFileChanges() const; // any delete, copy or move (DB refreshes alone don't count)
    bool canRun() const { return refusals.isEmpty(); }
    QString toText() const;
};

Plan buildPlan(const PlanInput &);

} // namespace smt
