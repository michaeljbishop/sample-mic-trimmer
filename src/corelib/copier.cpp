#include "copier.h"
#include "fsutil.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThreadPool>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

namespace smt {

namespace {

constexpr int kQueueSlots = 4;
// The plan comes from a (possibly cached) scan; a source that changed since needs a new one.
const QString kRescanHint = QStringLiteral("; rescan the source library and build the plan again");

struct Buffer
{
    QByteArray data;
    qint64 len = 0;
};

// Bounded queue between one reader and one writer: buffers cycle free -> full -> free.
struct Pipe
{
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Buffer> free, full;
    bool eof = false;  // reader finished (end, error or stop)
    bool stop = false; // writer gave up; reader must exit
    QString readError;
};

QString fileError(const QString &path, const QString &reason)
{
    return QStringLiteral("%1: %2").arg(path, reason);
}

// Several copy jobs create the same new folders (staging/Samples/_Brass ...) at once. On Windows a QDir::mkpath that
// races with another thread's reports failure even though the folder gets created, so folder creation is serialised.
bool makeFolder(const QString &path)
{
    static std::mutex mutex;
    const std::lock_guard<std::mutex> lock(mutex);
    return QDir().mkpath(path) || QFileInfo(path).isDir();
}

bool copyOne(const CopyJob &job, const CopyOptions &opts, const std::function<void(qint64)> &progress,
             const std::function<bool()> &stopping, QString *error)
{
    const QFileInfo destInfo(job.dest);
    if (!job.overwrite && destInfo.exists() && destInfo.size() == job.size) {
        if (progress)
            progress(job.size);
        return true;
    }
    if (!makeFolder(destInfo.absolutePath())) {
        *error = fileError(destInfo.absolutePath(), QStringLiteral("cannot create directory"));
        return false;
    }
    QFile src(job.source);
    if (!src.open(QIODevice::ReadOnly)) {
        *error = fileError(job.source, QStringLiteral("cannot open source (%1)%2").arg(src.errorString(), kRescanHint));
        return false;
    }
    const QString partPath = job.dest + QStringLiteral(".part");
    QFile out(partPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = fileError(partPath, out.errorString());
        return false;
    }

    Pipe pipe;
    for (int i = 0; i < kQueueSlots; ++i)
        pipe.free.push_back({QByteArray(opts.bufferBytes, Qt::Uninitialized), 0});

    std::thread reader([&] {
        try {
            for (;;) {
                Buffer buf;
                {
                    std::unique_lock lock(pipe.mutex);
                    while (pipe.free.empty() && !pipe.stop)
                        pipe.changed.wait(lock);
                    if (pipe.stop || stopping()) {
                        pipe.eof = true;
                        pipe.changed.notify_all();
                        return;
                    }
                    buf = std::move(pipe.free.front());
                    pipe.free.pop_front();
                }
                buf.len = src.read(buf.data.data(), buf.data.size());
                std::unique_lock lock(pipe.mutex);
                if (buf.len <= 0) {
                    if (buf.len < 0)
                        pipe.readError = src.errorString();
                    pipe.eof = true;
                } else {
                    pipe.full.push_back(std::move(buf));
                }
                pipe.changed.notify_all();
                if (pipe.eof)
                    return;
            }
        } catch (...) { // an escaping exception would terminate the process
            std::unique_lock lock(pipe.mutex);
            pipe.readError = QStringLiteral("unexpected exception while reading");
            pipe.eof = true;
            pipe.changed.notify_all();
        }
    });
    // Stops and joins the reader on every exit, including an exception below:
    // destroying a joinable std::thread terminates the process.
    struct ReaderGuard
    {
        Pipe &pipe;
        std::thread &reader;
        ~ReaderGuard()
        {
            {
                std::unique_lock lock(pipe.mutex);
                pipe.stop = true;
                pipe.changed.notify_all();
            }
            if (reader.joinable())
                reader.join();
        }
    } readerGuard{pipe, reader};

    QString writeError;
    qint64 written = 0;
    for (;;) {
        if (stopping())
            break;
        Buffer buf;
        {
            std::unique_lock lock(pipe.mutex);
            while (pipe.full.empty() && !pipe.eof)
                pipe.changed.wait(lock);
            if (pipe.full.empty())
                break; // eof
            buf = std::move(pipe.full.front());
            pipe.full.pop_front();
        }
        if (out.write(buf.data.constData(), buf.len) != buf.len) {
            writeError = fileError(partPath, out.errorString());
            break;
        }
        written += buf.len;
        if (progress)
            progress(buf.len);
        std::unique_lock lock(pipe.mutex);
        pipe.free.push_back(std::move(buf));
        pipe.changed.notify_all();
    }
    {
        std::unique_lock lock(pipe.mutex);
        pipe.stop = true;
        pipe.changed.notify_all();
    }
    reader.join();

    auto fail = [&](const QString &msg) {
        out.close();
        out.remove();
        *error = msg;
        return false;
    };
    if (!writeError.isEmpty())
        return fail(writeError);
    if (!pipe.readError.isEmpty())
        return fail(fileError(job.source, pipe.readError));
    if (stopping())
        return fail(QStringLiteral("cancelled"));
    if (written != job.size)
        return fail(fileError(job.source, QStringLiteral("size changed during copy (expected %1 bytes, read %2)%3")
                                              .arg(job.size)
                                              .arg(written)
                                              .arg(kRescanHint)));
    if (!out.flush())
        return fail(fileError(partPath, out.errorString()));
    if (QString syncError; !fs::syncDescriptor(out.handle(), &syncError))
        return fail(fileError(partPath, syncError));
    // set before close: the data is already flushed, and rename keeps the time
    if (job.modified.isValid() && !out.setFileTime(job.modified, QFileDevice::FileModificationTime))
        return fail(fileError(partPath, out.errorString()));
    out.close();
    if (out.error() != QFileDevice::NoError)
        return fail(fileError(partPath, out.errorString()));
    // replaceFile (rename(2) on Unix) replaces a stale dest of another size atomically; QFile::rename won't overwrite.
    if (QString renameError; !fs::replaceFile(partPath, job.dest, &renameError))
        return fail(fileError(job.dest, renameError));
    return true;
}

} // namespace

bool copyFiles(const QList<CopyJob> &jobs, const CopyOptions &opts,
               const std::function<void(qint64 bytesCopiedDelta)> &progress, const std::atomic_bool &cancel,
               QString *error)
{
    CopyOptions o = opts;
    o.bufferBytes = qMax<qint64>(o.bufferBytes, 4096);
    std::atomic_bool failed{false};
    std::mutex errorMutex;
    QString firstError;
    const std::function<bool()> stopping = [&] { return cancel.load() || failed.load(); };

    QThreadPool pool; // dedicated: each worker also blocks on a reader thread
    pool.setMaxThreadCount(qMax(1, o.concurrency));
    for (const CopyJob &job : jobs) {
        pool.start([&, job] {
            if (stopping())
                return;
            QString err;
            try {
                if (copyOne(job, o, progress, stopping, &err))
                    return;
            } catch (...) { // copyOne has joined its reader; the .part may be left behind
                QFile::remove(job.dest + QStringLiteral(".part"));
                // "<source>: ..." so callers can tell which job failed
                err = fileError(job.source, QStringLiteral("unexpected exception while copying"));
            }
            std::lock_guard lock(errorMutex);
            if (!failed.exchange(true) && !cancel.load())
                firstError = err;
        });
    }
    pool.waitForDone();

    if (!failed.load() && !cancel.load())
        return true;
    if (error)
        *error = firstError.isEmpty() ? QStringLiteral("cancelled") : firstError;
    return false;
}

} // namespace smt
