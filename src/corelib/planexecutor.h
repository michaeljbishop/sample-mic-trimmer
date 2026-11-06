#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include "plan.h"

class QLockFile;
class QThread;

namespace smt {

// Runs a Plan against destRoot on a worker thread. Signals are emitted from the worker,
// so receivers in other threads get them queued. Never writes under sourceRoot.
class PlanExecutor : public QObject
{
    Q_OBJECT
public:
    PlanExecutor(QString sourceRoot, QString destRoot, QObject *parent = nullptr);
    ~PlanExecutor() override; // cancels and waits for the worker

    // Returns false, emitting nothing, if an apply is already running. Otherwise returns true
    // immediately and always ends with finished() (refusals included, asynchronously).
    // `prepare` runs on the worker once every check passed and the lock is held, before the
    // first step; if it returns false the run ends with its error and the steps never run.
    // A run refused before its first step removes the (empty) folders it created.
    bool start(Plan plan, std::function<bool(QString *error)> prepare = {});
    void cancel();         // honoured between steps and between copy buffers

signals:
    void stepChanged(int index, smt::PlanStep::Status status, const QString &error);
    void bytesProgress(qint64 done, qint64 total, double bytesPerSecond);
    void finished(bool success, const QString &message);

private:
    void run(QList<PlanStep> steps);
    bool refuse(const QString &reason); // reports via finished(); returns true for start()
    void removeCreatedDirs();

    QString mSource, mDest, mDestCanonical;
    QStringList mCreatedDirs; // by the current start(), deepest first
    std::atomic_bool mCancel{false}, mBusy{false};
    std::unique_ptr<QLockFile> mLock;
    QThread *mThread = nullptr;
};

} // namespace smt
