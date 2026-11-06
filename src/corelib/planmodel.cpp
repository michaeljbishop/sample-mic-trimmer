#include "planmodel.h"

namespace smt {

void PlanModel::setPlan(const Plan &plan)
{
    beginResetModel();
    mSteps = plan.steps;
    for (auto &s : mSteps) {
        s.status = PlanStep::Status::Pending;
        s.error.clear();
    }
    endResetModel();
}

void PlanModel::setStepStatus(int index, PlanStep::Status status, const QString &error)
{
    if (index < 0 || index >= mSteps.size())
        return;
    mSteps[index].status = status;
    mSteps[index].error = error;
    emit dataChanged(this->index(index), this->index(index));
}

int PlanModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(mSteps.size());
}

QVariant PlanModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.parent().isValid() || index.row() >= mSteps.size())
        return {};
    const PlanStep &s = mSteps[index.row()];
    const bool failed = s.status == PlanStep::Status::Failed;
    switch (role) {
    case Qt::DisplayRole:
        return failed ? tr("%1 (failed: %2)").arg(s.describe(), s.error) : s.describe();
    case Qt::ToolTipRole:
        return failed ? QVariant(s.error) : QVariant();
    case Qt::CheckStateRole: // ticks steps off in a plain list view (QtCore has no icons)
        return s.status == PlanStep::Status::Done      ? Qt::Checked
               : s.status == PlanStep::Status::Running ? Qt::PartiallyChecked
                                                       : Qt::Unchecked;
    case StatusRole:
        return int(s.status);
    case ErrorRole:
        return s.error;
    }
    return {};
}

} // namespace smt
