#ifndef WORKRELAY_H
#define WORKRELAY_H

#include <QMetaType>
#include <QObject>
#include <functional>

Q_DECLARE_METATYPE(std::function<void()>)

// Carries a finished background job's completion (a callable) from a pool thread to the GUI thread: the job emits
// post() from its own thread, the connection (with the Core as context) delivers it queued and drops it when the Core
// is gone, so a late job can never touch a destroyed Core.
class WorkRelay : public QObject
{
    Q_OBJECT
signals:
    void post(std::function<void()> completion);
};

#endif // WORKRELAY_H
