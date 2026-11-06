#pragma once
#include <QDateTime>
#include <QList>
#include <QString>
#include <atomic>
#include <functional>

namespace smt {

struct CopyJob
{
    QString source, dest;
    qint64 size = 0;     // source size from the scan; the copy must end at exactly this size
    QDateTime modified;  // set on dest when valid
    bool overwrite = false; // copy even if dest already has job.size bytes (a stale leftover could hold other content)
};

struct CopyOptions
{
    qint64 bufferBytes = 8 << 20;
    int concurrency = 3; // files in flight
};

// Blocking: call off the GUI thread. Copies each job via a reader/writer pipeline into
// "<dest>.part", checks the size, renames to dest and sets the modification time.
// Skips jobs whose dest already has job.size bytes (still reported to progress), unless job.overwrite.
// `progress` is called from worker threads, concurrently; it must be thread-safe.
// On the first error, or when `cancel` becomes true, stops the rest, removes .part files,
// sets *error ("<path>: <reason>" or "cancelled") and returns false.
bool copyFiles(const QList<CopyJob> &jobs, const CopyOptions &opts,
               const std::function<void(qint64 bytesCopiedDelta)> &progress,
               const std::atomic_bool &cancel, QString *error);

} // namespace smt
