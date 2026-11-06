#include "qfileinfogatherertask.h"
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QSaveFile>
#include <QThreadPool>
#include <algorithm>
#include <limits>

namespace {
constexpr int kBatchFiles = 2000;
constexpr int kBatchMs = 50;

constexpr quint32 kMagic = 0x434D4653; // "SFMC" little endian
constexpr quint32 kFormat = 1;
constexpr int kMaxPath = 4096;
constexpr int kMinRecord = 2 + 8 + 8 + 8 + 8 + 4;
constexpr qint64 kNoTime = std::numeric_limits<qint64>::min();

void putU(QByteArray &b, quint64 v, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        b.append(char((v >> (8 * i)) & 0xff));
}
void putStr(QByteArray &b, const QString &s)
{
    const QByteArray u = s.toUtf8().left(kMaxPath);
    putU(b, u.size(), 2);
    b.append(u);
}
qint64 timeMs(const QDateTime &t)
{
    return t.isValid() ? t.toMSecsSinceEpoch() : kNoTime;
}
QDateTime fromMs(qint64 ms)
{
    return ms == kNoTime ? QDateTime() : QDateTime::fromMSecsSinceEpoch(ms);
}

// Bounds-checked little-endian reader: any overrun flips `ok` and yields zeros.
struct Reader
{
    const QByteArray &b;
    qsizetype pos = 0;
    bool ok = true;
    quint64 u(int bytes)
    {
        if (pos + bytes > b.size()) {
            ok = false;
            return 0;
        }
        quint64 v = 0;
        for (int i = 0; i < bytes; ++i)
            v |= quint64(quint8(b[pos + i])) << (8 * i);
        pos += bytes;
        return v;
    }
    QString str()
    {
        const int n = int(u(2));
        if (!ok || n > kMaxPath || pos + n > b.size()) {
            ok = false;
            return {};
        }
        const QString s = QString::fromUtf8(b.constData() + pos, n);
        pos += n;
        return s;
    }
};

smt::FileInfo fromQFileInfo(const QDir &parent, const QFileInfo &info)
{
    smt::FileInfo out;
    out.path = parent.relativeFilePath(info.absoluteFilePath());
    out.size = info.size();
    out.modificationTime = info.fileTime(QFileDevice::FileModificationTime);
    out.creationTime = info.fileTime(QFileDevice::FileBirthTime);
    out.metadataModificationTime = info.fileTime(QFileDevice::FileMetadataChangeTime);
    out.permissions = info.permissions();
    return out;
}
} // namespace

QFileInfoGathererTask::QFileInfoGathererTask(const QString &dir, const QString &cacheFile, qint64 expectedTotal, bool skipCacheRead,
                                             const QStringList &ignoreStrings)
    : QObject()
    , mDir(dir)
    , mCacheFile(cacheFile)
    , mExpected(expectedTotal)
    , mSkipCacheRead(skipCacheRead)
    , mIgnoreStrings(ignoreStrings)
{
    setAutoDelete(true);
}

void QFileInfoGathererTask::start()
{
    QThreadPool::globalInstance()->start(this);
}

QByteArray QFileInfoGathererTask::serializeScanCache(const QString &dir, const QList<smt::FileInfo> &files)
{
    QByteArray b;
    b.reserve(64 + files.size() * 96);
    putU(b, kMagic, 4);
    putU(b, kFormat, 4);
    putStr(b, QDir::cleanPath(dir));
    putU(b, files.size(), 4);
    for (const auto &f : files) {
        putStr(b, f.path);
        putU(b, f.size, 8);
        putU(b, quint64(timeMs(f.modificationTime)), 8);
        putU(b, quint64(timeMs(f.creationTime)), 8);
        putU(b, quint64(timeMs(f.metadataModificationTime)), 8);
        putU(b, quint32(f.permissions.toInt()), 4);
    }
    return b;
}

std::optional<QList<smt::FileInfo>> QFileInfoGathererTask::parseScanCache(const QByteArray &bytes, const QString &dir)
{
    Reader r{bytes};
    if (r.u(4) != kMagic || r.u(4) != kFormat || r.str() != QDir::cleanPath(dir))
        return std::nullopt;
    const quint64 count = r.u(4);
    if (!r.ok || count > quint64(bytes.size() - r.pos) / kMinRecord) // a hostile count cannot allocate more than the file holds
        return std::nullopt;
    QList<smt::FileInfo> out;
    out.reserve(qsizetype(count));
    for (quint64 i = 0; i < count; ++i) {
        smt::FileInfo f;
        f.path = r.str();
        f.size = size_t(r.u(8));
        f.modificationTime = fromMs(qint64(r.u(8)));
        f.creationTime = fromMs(qint64(r.u(8)));
        f.metadataModificationTime = fromMs(qint64(r.u(8)));
        f.permissions = QFile::Permissions::fromInt(int(r.u(4)));
        if (!r.ok)
            return std::nullopt;
        out.append(f);
    }
    if (r.pos != bytes.size()) // trailing bytes: not our file
        return std::nullopt;
    return out;
}

qint64 QFileInfoGathererTask::cachedFileCount(const QString &cacheFile)
{
    QFile f(cacheFile);
    if (cacheFile.isNull() || !f.open(QFile::ReadOnly))
        return -1;
    const QByteArray head = f.read(8 + 2 + kMaxPath + 4);
    Reader r{head};
    if (r.u(4) != kMagic || r.u(4) != kFormat)
        return -1;
    r.str();
    const quint64 count = r.u(4);
    // plausible against the file size (the header read does not validate the records)
    return r.ok && count <= quint64(f.size()) / kMinRecord ? qint64(count) : -1;
}

// The cache answered when it holds this directory and is valid: hand its files over in batches.
bool QFileInfoGathererTask::readCache()
{
    QFile f(mCacheFile);
    if (mCacheFile.isNull() || !f.open(QFile::ReadOnly))
        return false;
    const auto files = parseScanCache(f.readAll(), mDir);
    if (!files)
        return false;
    f.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime); // "used" (90-day cleanup)
    f.close();
    const qint64 total = files->size();
    QList<smt::FileInfo> out;
    qint64 done = 0;
    for (const auto &info : *files) {
        if (m_cancelRequested->loadRelaxed())
            return true;
        out.append(info);
        if (out.size() >= kBatchFiles) {
            done += out.size();
            emit batch(out);
            emit progress(done, total);
            out.clear();
        }
    }
    if (!out.isEmpty()) {
        done += out.size();
        emit batch(out);
    }
    emit progress(done, total);
    return true;
}

// A scan without a section DB (e.g. a share that was not mounted) is not cached, so it cannot hide the library on
// the next run. Written here, off the GUI thread, after finished() was emitted; QSaveFile keeps the old file (or none)
// readable until the new one is complete, and a task that is still writing at exit is waited for by the thread pool.
void QFileInfoGathererTask::writeCache(const QList<smt::FileInfo> &all) const
{
    const bool hasDb = std::any_of(all.cbegin(), all.cend(), [](const smt::FileInfo &i) { return smt::isDatabasePath(i.path); });
    if (mCacheFile.isNull() || !hasDb)
        return;
    QDir().mkpath(QFileInfo(mCacheFile).absolutePath());
    QSaveFile cache(mCacheFile);
    if (cache.open(QFile::WriteOnly) && cache.write(serializeScanCache(mDir, all)) >= 0)
        cache.commit();
}

void QFileInfoGathererTask::run()
{
    if (!mSkipCacheRead && readCache()) {
        emit finished(!m_cancelRequested->loadRelaxed());
        return;
    }
    if (mExpected < 0 && !mSkipCacheRead) // the file count of an earlier scan: the percentage's denominator
        mExpected = cachedFileCount(mCacheFile);
    QDirIterator it(mDir, QDir::Files | QDir::NoDotAndDotDot | QDir::Readable | QDir::Hidden, QDirIterator::Subdirectories);
    const QDir root(mDir);
    QList<smt::FileInfo> all, pending;
    QElapsedTimer sinceBatch;
    sinceBatch.start();
    while (it.hasNext()) {
        if (m_cancelRequested->loadRelaxed()) {
            emit finished(false);
            return;
        }
        const QFileInfo next = it.nextFileInfo();
        if (mIgnoreStrings.contains(next.fileName()))
            continue;
        pending.append(fromQFileInfo(root, next));
        if (pending.size() >= kBatchFiles || sinceBatch.elapsed() >= kBatchMs) {
            all += pending;
            emit batch(pending);
            emit progress(all.size(), mExpected);
            pending.clear();
            sinceBatch.restart();
        }
    }
    if (m_cancelRequested->loadRelaxed()) {
        emit finished(false);
        return;
    }
    all += pending;
    if (!pending.isEmpty())
        emit batch(pending);
    emit progress(all.size(), all.size()); // done: 100%
    emit finished(true); // the owner is not kept waiting for the cache file
    writeCache(all);
}
