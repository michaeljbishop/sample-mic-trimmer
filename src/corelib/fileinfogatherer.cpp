#include "fileinfogatherer.h"
#include <QDir>
#include <QMetaType>
#include <algorithm>
#include "qfileinfogatherertask.h"
namespace smt {

FileInfoGatherer::FileInfoGatherer(const QString &dir, const QString &cacheFilenamePath, QObject *parent)
    : QObject(parent)
    , mDirPath(dir)
    , mCacheFileName(cacheFilenamePath)
{
    qRegisterMetaType<QList<smt::FileInfo>>("QList<smt::FileInfo>"); // queued from the pool thread
}

FileInfoGatherer::FileInfoGatherer(const QString &dir, QObject *parent)
    : FileInfoGatherer(dir, QString(), parent)
{}

FileInfoGatherer::~FileInfoGatherer()
{
    cancel(); // connections die with this (context object); the task is pool-owned
}

void FileInfoGatherer::setPreloaded(const QList<FileInfo> &files)
{
    mPreloaded = files;
}

void FileInfoGatherer::finish(bool success)
{
    mIsRunning = false;
    mFinished = true;
    emit finished(success && !mCancelled);
}

qint64 FileInfoGatherer::cachedFileCount(const QString &cacheFile)
{
    return QFileInfoGathererTask::cachedFileCount(cacheFile);
}

void FileInfoGatherer::deliverPreloaded(qsizetype from)
{
    constexpr qsizetype kSlice = 2000;
    if (mCancelled) {
        finish(true);
        return;
    }
    const QList<FileInfo> slice = mPreloaded->mid(from, kSlice);
    if (!slice.isEmpty()) {
        mInfos += slice;
        emit foundFileInfos(slice);
        emit progress(mInfos.size(), mPreloaded->size());
    }
    if (from + kSlice < mPreloaded->size())
        QMetaObject::invokeMethod(this, [this, from] { deliverPreloaded(from + kSlice); }, Qt::QueuedConnection);
    else
        finish(true);
}

void FileInfoGatherer::run()
{
    if (mIsRunning)
        return;
    mInfos.clear();
    mCancelFlag.reset();
    mFinished = false;
    mCancelled = false;
    mIsRunning = true;

    if (mPreloaded) { // always async, in slices like a scan, so a big library does not stall the GUI thread
        QMetaObject::invokeMethod(this, [this] { deliverPreloaded(0); }, Qt::QueuedConnection);
        return;
    }

    auto *task = new QFileInfoGathererTask(mDirPath, mCacheFileName, mExpected, mSkipCacheRead, {".DS_Store"});
    mCancelFlag = task->cancelFlag();
    connect(task, &QFileInfoGathererTask::batch, this, [this](const QList<FileInfo> &files) {
        if (mCancelled)
            return; // a batch that was already on its way
        mInfos += files;
        emit foundFileInfos(files);
    });
    connect(task, &QFileInfoGathererTask::progress, this, [this](qint64 scanned, qint64 expected) {
        if (mCancelled)
            return;
        emit progress(scanned, expected);
    });
    connect(task, &QFileInfoGathererTask::finished, this, [this](bool success) { finish(success); });
    task->start();
}

void FileInfoGatherer::cancel()
{
    if (!mIsRunning)
        return;
    mCancelled = true;
    if (mCancelFlag)
        mCancelFlag->storeRelaxed(1); // the task then emits finished(false); a preloaded run finishes in its queued step
}

QList<FileInfo> FileInfoGatherer::infos() const
{
    return mInfos;
}

bool FileInfoGatherer::isFinished() const
{
    return mFinished;
}

bool FileInfoGatherer::isRunning() const
{
    return mIsRunning;
}

QString FileInfoGatherer::absoluteFilePath(const FileInfo &info)
{
    return QDir(mDirPath).absoluteFilePath(info.path);
}
} // namespace smt
