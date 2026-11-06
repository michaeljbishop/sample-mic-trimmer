#pragma once
#include <QDialog>
#include "plan.h"

class QLabel;
class QProgressBar;
class QPushButton;
namespace smt {
class Core;
class PlanModel;
}

// Shows a Plan, runs it through Core and shows its progress. No logic of its own.
class PlanDialog : public QDialog
{
    Q_OBJECT
public:
    PlanDialog(smt::Core *core, const smt::Plan &plan, QWidget *parent = nullptr);

    void reject() override; // ignored while running (Escape, window close)

private:
    enum class State { Review, Running, Finished };
    void setState(State state);

    smt::Core *mCore;
    smt::Plan mPlan;
    smt::PlanModel *mModel;
    State mState = State::Review;
    QPushButton *mStart, *mClose;
    QProgressBar *mProgress;
    QLabel *mProgressLine, *mResult;
};
