#include <QtTest>
#include "planmodel.h"

using namespace smt;

static Plan threeSteps()
{
    Plan p;
    for (int i = 0; i < 3; ++i) {
        PlanStep s;
        s.type = PlanStep::Type::DeleteFile;
        s.relPath = QString("Samples/_Brass/F%1.spitfire").arg(i);
        s.bytes = 1000;
        p.steps << s;
    }
    return p;
}

class TstPlanModel : public QObject
{
    Q_OBJECT
private slots:
    void rowCountMatchesSteps()
    {
        PlanModel m;
        QCOMPARE(m.rowCount(), 0);
        const Plan p = threeSteps();
        m.setPlan(p);
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(m.data(m.index(1)).toString(), p.steps[1].describe());
        QCOMPARE(m.data(m.index(1), PlanModel::StatusRole).toInt(), int(PlanStep::Status::Pending));
    }

    void statusUpdateEmitsDataChanged()
    {
        PlanModel m;
        m.setPlan(threeSteps());
        QSignalSpy spy(&m, &QAbstractItemModel::dataChanged);
        m.setStepStatus(2, PlanStep::Status::Done, {});
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy[0][0].toModelIndex().row(), 2);
        QCOMPARE(m.data(m.index(2), PlanModel::StatusRole).toInt(), int(PlanStep::Status::Done));
        QCOMPARE(m.data(m.index(2), Qt::CheckStateRole).toInt(), int(Qt::Checked));
    }

    void failedStepShowsError()
    {
        PlanModel m;
        const Plan p = threeSteps();
        m.setPlan(p);
        m.setStepStatus(0, PlanStep::Status::Failed, "disk full");
        const QString text = m.data(m.index(0)).toString();
        QVERIFY2(text.startsWith(p.steps[0].describe()), qPrintable(text));
        QVERIFY2(text.contains("disk full"), qPrintable(text));
        QCOMPARE(m.data(m.index(0), Qt::ToolTipRole).toString(), QString("disk full"));
        QCOMPARE(m.data(m.index(0), PlanModel::ErrorRole).toString(), QString("disk full"));
    }

    void setPlanResetsStatuses()
    {
        PlanModel m;
        m.setPlan(threeSteps());
        m.setStepStatus(1, PlanStep::Status::Failed, "x");
        QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
        m.setPlan(threeSteps());
        QCOMPARE(reset.count(), 1);
        QCOMPARE(m.data(m.index(1), PlanModel::StatusRole).toInt(), int(PlanStep::Status::Pending));
        QVERIFY(m.data(m.index(1), PlanModel::ErrorRole).toString().isEmpty());
    }

    void outOfRangeIndexIgnored()
    {
        PlanModel m;
        m.setPlan(threeSteps());
        QSignalSpy spy(&m, &QAbstractItemModel::dataChanged);
        m.setStepStatus(-1, PlanStep::Status::Done, {});
        m.setStepStatus(3, PlanStep::Status::Done, {});
        QCOMPARE(spy.count(), 0);
        QVERIFY(!m.data(m.index(3)).isValid());
    }
};

QTEST_GUILESS_MAIN(TstPlanModel)
#include "tst_planmodel.moc"
