#ifndef FILEINFOGATHERER_H
#define FILEINFOGATHERER_H

#include <QList>
#include <QObject>
#include <QString>
#include <memory>
#include <optional>
#include "smt.h"

class QFileInfoGathererTask;

namespace smt {
// Scans a folder into FileInfos on a pool thread (cache read/parse, every stat and the cache write included) and
// delivers them to this object in batches, so the GUI thread never waits for IO.
class FileInfoGatherer : public QObject
{
    Q_OBJECT
public:
    FileInfoGatherer(const QString &dir, const QString &cacheFilename, QObject *parent = nullptr);
    FileInfoGatherer(const QString &dir, QObject *parent = nullptr);
    ~FileInfoGatherer() override;

    // The percentage's denominator while walking a folder (a previous scan's file count); -1 = unknown.
    void setExpectedTotal(qint64 files) { mExpected = files; }
    // Walk the folder even if a cache file exists (Rescan source); the fresh result still replaces the cache.
    void setSkipCacheRead(bool skip) { mSkipCacheRead = skip; }
    // Answer run() with these files, no IO at all (a scan this session already did).
    void setPreloaded(const QList<FileInfo> &files);

    // Starts the scan; ignored while a previous run is active. Everything arrives queued: connect before or after.
    // finished() is emitted exactly once per run.
    void run();
    void cancel();

    QList<FileInfo> infos() const; // what has arrived so far
    bool isFinished() const;
    bool isRunning() const;

    // The file count a cache file remembers (cheap, -1 if none): the percentage's denominator for a rescan.
    static qint64 cachedFileCount(const QString &cacheFile);

    QString absoluteFilePath(const FileInfo &);

signals:
    void foundFileInfos(const QList<smt::FileInfo> &files); // one batch, in order
    void progress(qint64 scanned, qint64 expected); // expected -1: unknown (callers cap the percentage below 100 until finished())
    void finished(bool success);

private:
    void finish(bool success);
    void deliverPreloaded(qsizetype from);

    QList<FileInfo> mInfos;
    QString mDirPath;
    QString mCacheFileName;
    qint64 mExpected = -1;
    bool mSkipCacheRead = false;
    std::optional<QList<FileInfo>> mPreloaded;

    std::shared_ptr<QAtomicInt> mCancelFlag; // shared with the pool-owned task
    bool mCancelled = false;
    bool mFinished = false;
    bool mIsRunning = false;
};
} // namespace smt

#endif // FILEINFOGATHERER_H
