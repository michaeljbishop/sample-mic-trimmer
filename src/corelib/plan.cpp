#include "plan.h"
#include <QMap>
#include "coreutil.h"
#include "projectconfig.h"

namespace smt {

namespace {

bool isToolArtifact(const QString &p)
{
    return p.startsWith(".smt-staging/") || p == ".smt.lock" || p == "project.smt"
           || p.section('/', -1) == ".DS_Store";
}

// Spitfire's Repair rewrites the patch/preset/config files in place with the same size, and they are signed per
// install, so a copy made before the Repair is unusable: these are copied again on every Apply, whatever the size.
bool isSettingsFile(const QString &relPath)
{
    static const QStringList dirs{QStringLiteral("Patches"), QStringLiteral("Presets"), QStringLiteral("Config")};
    return dirs.contains(relPath.section('/', 0, 0)) && relPath.contains('/');
}

QString dirOf(const QString &p)
{
    const int i = p.lastIndexOf('/');
    return i < 0 ? QString() : p.left(i);
}

QString nameOf(const QString &p)
{
    return p.mid(p.lastIndexOf('/') + 1);
}

QString signedSize(qint64 b)
{
    return b < 0 ? "-" + formatByteSize(size_t(-b)) : formatByteSize(size_t(b));
}

// QMap keeps paths sorted, so step order is deterministic.
QMap<QString, qint64> index(const QList<FileInfo> &files)
{
    QMap<QString, qint64> m;
    for (const auto &f : files)
        if (!isToolArtifact(f.path))
            m.insert(f.path, qint64(f.size));
    return m;
}

PlanStep step(PlanStep::Type type, int phase, const QString &relPath, qint64 bytes)
{
    PlanStep s;
    s.type = type;
    s.phase = phase;
    s.relPath = relPath;
    s.bytes = bytes;
    return s;
}

} // namespace

QString PlanStep::describe() const
{
    QString path = relPath;
    path.replace('\n', ' ').replace('\r', ' '); // keep it one line whatever the file name
    QString what;
    switch (type) {
    case Type::ReplaceDb:
        what = QString("rebuild database %1 (keeps %2 files)").arg(path).arg(keepFileNames.size());
        break;
    case Type::DeleteFile:
        what = QString("delete %1 (%2)").arg(path, formatByteSize(size_t(bytes)));
        break;
    case Type::CopyToStaging:
        what = QString("copy %1 to staging (%2)").arg(path, formatByteSize(size_t(bytes)));
        break;
    case Type::MoveIntoPlace:
        what = QString("move %1 into place").arg(path);
        break;
    }
    return QString("Phase %1: %2").arg(phase).arg(what);
}

QString Plan::toText() const
{
    QStringList lines;
    lines << QString("Plan: %1 steps").arg(steps.size())
          << QString("Bytes to add: %1").arg(formatByteSize(size_t(bytesToAdd)))
          << QString("Bytes to free: %1").arg(formatByteSize(size_t(bytesToFree)))
          << QString("Bytes for databases: %1").arg(formatByteSize(size_t(bytesForDatabases)))
          << QString("Bytes already staged: %1").arg(formatByteSize(size_t(bytesAlreadyStaged)))
          << QString("Free space after: %1").arg(signedSize(freeBytesAfter));
    for (const auto &r : refusals)
        lines << "REFUSED: " + r;
    for (int i = 0; i < steps.size(); ++i)
        lines << QString("%1. %2").arg(i + 1).arg(steps[i].describe());
    return lines.join('\n') + '\n';
}

bool Plan::hasFileChanges() const
{
    for (const auto &s : steps)
        if (s.type != PlanStep::Type::ReplaceDb)
            return true;
    return false;
}

Plan buildPlan(const PlanInput &in)
{
    using T = PlanStep::Type;
    const auto src = index(in.source);
    const auto dst = index(in.dest);

    Plan plan;
    plan.builtForSource = in.sourceRoot;
    plan.builtForDest = in.destRoot;
    QList<PlanStep> deletes, copies, moves, db1, db2;
    QHash<QString, QSet<QString>> kept;  // dir -> non-db names left in dest after phase 1
    QHash<QString, QSet<QString>> added; // dir -> names added in phase 2
    QSet<QString> dirsWithDeletes;
    QHash<QString, QDateTime> srcTime;
    for (const auto &f : in.source)
        srcTime.insert(f.path, f.modificationTime);
    const QString stagingPrefix = QStringLiteral(".smt-staging/");
    QHash<QString, qint64> staged; // relPath -> size of the copy left by an interrupted run
    for (const auto &f : in.dest)
        if (f.path.startsWith(stagingPrefix))
            staged.insert(f.path.mid(stagingPrefix.size()), qint64(f.size));

    for (auto it = dst.cbegin(); it != dst.cend(); ++it) {
        const auto c = classify(in.profile, it.key());
        if (c.kind == FileKind::Database)
            continue;
        // The only deletion rule: a mic file whose position is switched off.
        if (c.kind == FileKind::Mic && !micEnabled(in.profile, in.mics, c.key)) {
            deletes << step(T::DeleteFile, 1, it.key(), it.value());
            plan.bytesToFree += it.value();
            dirsWithDeletes.insert(dirOf(it.key()));
        } else {
            kept[dirOf(it.key())].insert(nameOf(it.key()));
        }
    }

    bool haveDb = false;
    for (auto it = src.cbegin(); it != src.cend(); ++it) {
        const auto c = classify(in.profile, it.key());
        if (c.kind == FileKind::Database) {
            haveDb = true;
            continue;
        }
        if (c.kind == FileKind::Mic && !micEnabled(in.profile, in.mics, c.key))
            continue;
        // A present file of the wrong size is replaced (copy + atomic rename); never a .db. A settings file is
        // replaced even at the same size.
        const bool refresh = c.kind == FileKind::Other && isSettingsFile(it.key()); // never a mic or database file
        const auto d = dst.constFind(it.key());
        if (d != dst.cend() && ((d.value() == it.value() && !refresh) || it.key().endsWith(".db", Qt::CaseInsensitive)))
            continue;
        copies << step(T::CopyToStaging, 2, it.key(), it.value());
        copies.last().modified = srcTime.value(it.key());
        copies.last().refresh = refresh;
        moves << step(T::MoveIntoPlace, 2, it.key(), it.value());
        moves.last().refresh = refresh;
        plan.bytesToAdd += it.value();
        if (!refresh && staged.value(it.key(), -1) == it.value()) // the copier reuses it: it already takes up space
            plan.bytesAlreadyStaged += it.value();
        added[dirOf(it.key())].insert(nameOf(it.key()));
    }

    QSet<QString> dirsWithSourceDb;
    for (auto it = src.cbegin(); it != src.cend(); ++it) {
        if (classify(in.profile, it.key()).kind != FileKind::Database)
            continue;
        const QString dir = dirOf(it.key());
        dirsWithSourceDb.insert(dir);
        if (dirsWithDeletes.contains(dir)) {
            db1 << step(T::ReplaceDb, 1, it.key(), it.value());
            db1.last().keepFileNames = kept.value(dir);
        }
        db2 << step(T::ReplaceDb, 2, it.key(), it.value());
        db2.last().keepFileNames = kept.value(dir) | added.value(dir);
        plan.bytesForDatabases += it.value();
    }

    // Deleting is only safe where every DB beside the files is rebuilt from the source.
    QStringList deleteDirs = dirsWithDeletes.values();
    deleteDirs.sort(); // QSet order varies per process; keep the refusals stable
    for (const QString &dir : std::as_const(deleteDirs))
        if (!dirsWithSourceDb.contains(dir))
            plan.refusals << QString("%1 has files to delete but no database in the source: it does not look "
                                     "like a Spitfire section folder.").arg(dir);
    for (auto it = dst.cbegin(); it != dst.cend(); ++it)
        if (classify(in.profile, it.key()).kind == FileKind::Database && dirsWithDeletes.contains(dirOf(it.key()))
            && !src.contains(it.key()))
            plan.refusals << QString("%1 is in the destination but not the source; it would still list "
                                     "deleted files.").arg(it.key());

    plan.steps << db1 << deletes << copies << moves << db2;
    plan.freeBytesAfter = in.freeBytes + plan.bytesToFree + plan.bytesAlreadyStaged - plan.bytesToAdd
                          - plan.bytesForDatabases;

    if (!haveDb)
        plan.refusals << "The source does not look like a Spitfire library: no .db file found.";
    if (plan.freeBytesAfter < 0)
        plan.refusals << QString("Not enough free space on the destination: free %1 more and try again.")
                             .arg(formatByteSize(size_t(-plan.freeBytesAfter)));
    return plan;
}

} // namespace smt
