#ifndef QFILEINFOGATHERERTASK_H
#define QFILEINFOGATHERERTASK_H

#include <QList>
#include <QObject>
#include <QRunnable>
#include <memory>
#include <optional>
#include "smt.h"


// The whole scan on a pool thread: reading and parsing the cache or walking the folder (every stat), and writing the
// cache afterwards. The owner only receives batches of finished FileInfos, so the GUI thread never does file IO or
// bulk parsing.
class QFileInfoGathererTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    // No QObject parent: the thread pool (autoDelete) is the only owner. `cacheFile` null = no cache;
    // `expectedTotal` -1 = unknown: the task then reads it from the cache header (the progress denominator while walking). `skipCacheRead` walks the disk whatever the cache holds.
    QFileInfoGathererTask(const QString &dir, const QString &cacheFile, qint64 expectedTotal, bool skipCacheRead,
                          const QStringList &ignoreStrings = {});

    void start();
    // Shared with the owner so it can cancel without touching this (pool-deleted) object.
    std::shared_ptr<QAtomicInt> cancelFlag() const { return m_cancelRequested; }

    // The file count stored in a cache file's header (a small read), -1 if there is none or it is not valid.
    static qint64 cachedFileCount(const QString &cacheFile);

    // The binary scan cache (".smtcache"): magic "SFMC", format version, folder, count, then one record per file.
    // `readScanCache` validates strictly (magic, version, folder, count against the size, string lengths, no trailing
    // bytes); anything else yields nullopt, so a damaged or foreign file just means a rescan.
    static QByteArray serializeScanCache(const QString &dir, const QList<smt::FileInfo> &files);
    static std::optional<QList<smt::FileInfo>> parseScanCache(const QByteArray &bytes, const QString &dir);

signals:
    void batch(const QList<smt::FileInfo> &files); // up to ~2000 files or 50 ms of them
    void progress(qint64 scanned, qint64 expected); // expected -1: unknown
    void finished(bool success);

protected:
    void run() override;

private:
    bool readCache(); // true: the cache answered (even when cancelled midway)
    void writeCache(const QList<smt::FileInfo> &all) const;

    QString mDir, mCacheFile;
    qint64 mExpected;
    bool mSkipCacheRead;
    QStringList mIgnoreStrings;
    std::shared_ptr<QAtomicInt> m_cancelRequested = std::make_shared<QAtomicInt>();
};

#endif // QFILEINFOGATHERERTASK_H
