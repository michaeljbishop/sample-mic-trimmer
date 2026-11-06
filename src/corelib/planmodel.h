#pragma once
#include <QAbstractListModel>
#include "plan.h"

namespace smt {

// The steps of a Plan for a list view; Core's applyStepChanged feeds setStepStatus().
class PlanModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role { StatusRole = Qt::UserRole + 1, ErrorRole };
    using QAbstractListModel::QAbstractListModel;

    void setPlan(const Plan &plan);
    void setStepStatus(int index, PlanStep::Status status, const QString &error);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

private:
    QList<PlanStep> mSteps;
};

} // namespace smt
