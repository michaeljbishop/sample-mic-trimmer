#include <QtTest>
#include <QLabel>
#include <QListView>
#include <QProgressBar>
#include <QPushButton>
#include "corelib.h"
#include "unixonly.h"
#include "fakelibrary.h"
#include "librarydb.h"
#include "plandialog.h"
#include "palette.h"
#include "planmodel.h"

using namespace smt;

static const QStringList kTokens{"TR01", "MO01"};
static const QStringList kSections{"brass", "woodwinds"};

// Every entry under root with size and mtime, sorted.
static QStringList snapshot(const QString &root)
{
    QStringList out;
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo fi = it.nextFileInfo();
        out << QString("%1 %2 %3").arg(QDir(root).relativeFilePath(fi.filePath()))
                   .arg(fi.isDir() ? -1 : fi.size())
                   .arg(snapshotMtime(fi));
    }
    out.sort();
    return out;
}

template<typename T>
static T *child(QWidget *w, const char *name)
{
    T *c = w->findChild<T *>(name);
    if (!c)
        qFatal("no child %s", name);
    return c;
}

static QtMessageHandler gPreviousHandler = nullptr;

// Drops the offscreen platform's noise so a real warning stands out.
static void filterPlatformNoise(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (qstrcmp(context.category, "qt.qpa.fonts") == 0
        || msg == QLatin1String("This plugin does not support propagateSizeHints()"))
        return;
    gPreviousHandler(type, context, msg);
}

class TstPlanDialog : public QObject
{
    Q_OBJECT

    QTemporaryDir mTmp;
    QString mSrc, mDst;

    // A Core with both scans finished on mSrc -> dest.
    void scan(Core &core, const QString &dest)
    {
        QSignalSpy spy(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(dest);
        QVERIFY(spy.count() > 0 || spy.wait(10000));
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true); // never touch the real source cache
        gPreviousHandler = qInstallMessageHandler(filterPlatformNoise);
    }

    void init()
    {
        QVERIFY(mTmp.isValid());
        mSrc = makeFakeLibrary(mTmp.filePath("src"), kTokens, kSections, 2, 1000);
        mDst = mTmp.filePath("dst");
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/ScanCache").removeRecursively();
    }

    void cleanup()
    {
        QDir(mTmp.path()).removeRecursively();
        QDir().mkpath(mTmp.path());
    }

    void reviewStateShowsWholePlanAndRefusals()
    {
        Core core;
        scan(core, mSrc + "/inside");
        const Plan plan = core.buildPlan();
        QVERIFY(!plan.canRun());
        PlanDialog dlg(&core, plan);
        dlg.show();
        QVERIFY(!child<QPushButton>(&dlg, "startButton")->isEnabled());
        QLabel *refusals = child<QLabel>(&dlg, "refusalsLabel");
        QVERIFY(refusals->isVisible());
        QVERIFY2(refusals->text().contains(plan.refusals.first()), qPrintable(refusals->text()));
        QVERIFY(child<QLabel>(&dlg, "warningLabel")->text().contains("Quit the Spitfire plugin/host (DAW)"));
        QVERIFY(!child<QProgressBar>(&dlg, "progressBar")->isVisible());
        QCOMPARE(child<QPushButton>(&dlg, "closeButton")->text(), QString("Close"));
        QVERIFY(child<QPushButton>(&dlg, "closeButton")->isDefault()); // Start is disabled
    }

    void dbOnlyPlanSaysSo()
    {
        Core core;
        PlanStep db;
        db.type = PlanStep::Type::ReplaceDb;
        db.phase = 2;
        db.relPath = "Samples/_Brass/X.db";
        PlanStep copy = db;
        copy.type = PlanStep::Type::CopyToStaging;
        copy.relPath = "Samples/_Brass/X_TR01.spitfire";
        Plan dbOnly;
        dbOnly.steps << db;
        Plan withCopy = dbOnly;
        withCopy.steps.prepend(copy);
        Plan refused = dbOnly;
        refused.refusals << "no";
        for (const Plan &p : {dbOnly, withCopy, refused, Plan{}}) {
            PlanDialog dlg(&core, p);
            dlg.show();
            QLabel *label = child<QLabel>(&dlg, "noFileChangesLabel");
            QCOMPARE(label->isVisible(), p.steps.size() == 1 && p.canRun());
            QCOMPARE(label->text(), QString("No file changes; databases will be refreshed only."));
        }
    }

    void startRunsToCompletionAndShowsGreenResult()
    {
        Core core;
        scan(core, mDst);
        const Plan plan = core.buildPlan();
        QVERIFY2(plan.canRun(), qPrintable(plan.refusals.join('\n')));
        PlanDialog dlg(&core, plan);
        dlg.show();
        QAbstractItemModel *model = child<QListView>(&dlg, "stepList")->model();
        QCOMPARE(model->rowCount(), plan.steps.size());
        QPushButton *start = child<QPushButton>(&dlg, "startButton");
        QVERIFY(start->isEnabled());
        QVERIFY(start->isDefault());

        QSignalSpy finished(&core, &Core::applyFinished);
        start->click();
        QVERIFY(!start->isEnabled());
        QCOMPARE(child<QPushButton>(&dlg, "closeButton")->text(), QString("Cancel"));
        QVERIFY(child<QProgressBar>(&dlg, "progressBar")->isVisible());
        QVERIFY(finished.count() > 0 || finished.wait(30000)); // the dialog's slot ran first
        QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));

        QLabel *result = child<QLabel>(&dlg, "resultLabel");
        QVERIFY(result->isVisible());
        QCOMPARE(result->text(), finished[0][1].toString());
        QVERIFY(result->styleSheet().contains(Palette::success.name()));
        QCOMPARE(child<QPushButton>(&dlg, "closeButton")->text(), QString("Close"));
        QVERIFY(child<QPushButton>(&dlg, "closeButton")->isDefault());
        QVERIFY(!start->isEnabled());
        for (int i = 0; i < model->rowCount(); ++i)
            QCOMPARE(model->index(i, 0).data(PlanModel::StatusRole).toInt(), int(PlanStep::Status::Done));
        QCOMPARE(child<QProgressBar>(&dlg, "progressBar")->value(), 1000);
    }

    void cancelDuringRunLeavesWorkingLibrary()
    {
        // A full destination with one mic switched off: DB patches and many deletes to interrupt.
        makeFakeLibrary(mDst, kTokens, kSections, 20, 1000);
        Core core;
        scan(core, mDst);
        core.setMicEnabled({"mo01", "brass"}, false);
        const Plan plan = core.buildPlan();
        QVERIFY(plan.canRun());
        QVERIFY(plan.hasFileChanges());
        QCOMPARE(plan.steps.first().type, PlanStep::Type::ReplaceDb);
        QVERIFY(plan.steps.size() > 20);
        PlanDialog dlg(&core, plan);
        dlg.show();
        QSignalSpy finished(&core, &Core::applyFinished);
        QPushButton *close = child<QPushButton>(&dlg, "closeButton");
        // Cancel once the first step (a DB patch) is reported running, as a user would.
        connect(&core, &Core::applyStepChanged, close, [close](int index, PlanStep::Status status) {
            if (index == 0 && status == PlanStep::Status::Running)
                close->click();
        });
        child<QPushButton>(&dlg, "startButton")->click();
        QVERIFY(finished.count() > 0 || finished.wait(30000)); // the dialog's slot ran first
        QVERIFY(!finished[0][0].toBool());
        QVERIFY2(finished[0][1].toString().startsWith("Cancelled"), qPrintable(finished[0][1].toString()));
        QVERIFY(child<QLabel>(&dlg, "resultLabel")->styleSheet().contains(Palette::error.name()));
        QVERIFY(close->isEnabled());
        QCOMPARE(close->text(), QString("Close"));

        QAbstractItemModel *model = child<QListView>(&dlg, "stepList")->model();
        const auto status = [model](int row) { return model->index(row, 0).data(PlanModel::StatusRole).toInt(); };
        QCOMPARE(status(0), int(PlanStep::Status::Done));
        QCOMPARE(status(model->rowCount() - 1), int(PlanStep::Status::Pending)); // the run stopped early

        // Working library: every destination DB lists only files that are present.
        int checked = 0;
        QDirIterator it(mDst, {"*.db"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString db = it.next();
            if (db.contains(".smt-staging"))
                continue;
            ++checked;
            for (const QString &name : librarydb::containerNames(db))
                QVERIFY2(QFileInfo::exists(QFileInfo(db).path() + "/" + name), qPrintable(db + ": " + name));
        }
        QCOMPARE(checked, 3); // Brass has one DB, Woodwinds two
    }

    void closeBlockedWhileRunning()
    {
        Core core;
        scan(core, mDst);
        PlanDialog dlg(&core, core.buildPlan());
        dlg.show();
        QSignalSpy finished(&core, &Core::applyFinished);
        child<QPushButton>(&dlg, "startButton")->click();
        QVERIFY(!dlg.close());
        dlg.reject(); // Escape
        QVERIFY(dlg.isVisible());
        QVERIFY(child<QLabel>(&dlg, "resultLabel")->text().contains("Cannot close while applying"));
        QVERIFY(finished.count() > 0 || finished.wait(30000)); // the dialog's slot ran first
        QVERIFY(dlg.close());
    }

    void staleStartShowsFailure()
    {
        Core core;
        scan(core, mDst);
        const Plan plan = core.buildPlan();
        QVERIFY(plan.canRun());
        const QStringList before = snapshot(mSrc);
        core.setDestinationDirectoryPath(mTmp.filePath("other"));
        PlanDialog dlg(&core, plan);
        dlg.show();
        QSignalSpy finished(&core, &Core::applyFinished);
        child<QPushButton>(&dlg, "startButton")->click();
        QVERIFY(finished.count() > 0 || finished.wait(30000)); // the dialog's slot ran first
        QLabel *result = child<QLabel>(&dlg, "resultLabel");
        QVERIFY(result->isVisible());
        QVERIFY2(result->text().contains("out of date"), qPrintable(result->text()));
        QVERIFY(result->styleSheet().contains(Palette::error.name()));
        QCOMPARE(child<QPushButton>(&dlg, "closeButton")->text(), QString("Close"));
        QCOMPARE(snapshot(mSrc), before);
        QVERIFY(!QFileInfo::exists(mDst));
    }

    void startReturningFalseStaysInReview()
    {
        Core core;
        scan(core, mDst);
        const Plan plan = core.buildPlan();
        PlanDialog first(&core, plan), second(&core, plan);
        first.show();
        second.show();
        QSignalSpy finished(&core, &Core::applyFinished);
        child<QPushButton>(&first, "startButton")->click();
        child<QPushButton>(&second, "startButton")->click(); // Core refuses: an apply is running
        QLabel *result = child<QLabel>(&second, "resultLabel");
        QVERIFY(result->isVisible());
        QVERIFY2(result->text().contains("already running"), qPrintable(result->text()));
        QVERIFY(child<QPushButton>(&second, "startButton")->isEnabled());
        QCOMPARE(child<QPushButton>(&second, "closeButton")->text(), QString("Close"));
        QVERIFY(finished.count() > 0 || finished.wait(30000));
        QVERIFY(finished[0][0].toBool());
        // The first dialog's run is not shown in the second one.
        QVERIFY(!child<QProgressBar>(&second, "progressBar")->isVisible());
        QVERIFY(result->text().contains("already running"));
        QVERIFY(second.close());
    }

    void signalsIgnoredInReview()
    {
        Core core;
        scan(core, mDst);
        const Plan plan = core.buildPlan();
        PlanDialog dlg(&core, plan);
        dlg.show();
        QSignalSpy finished(&core, &Core::applyFinished);
        QVERIFY(core.startApply(plan)); // someone else's apply, e.g. started without this dialog
        QVERIFY(finished.count() > 0 || finished.wait(30000));
        QVERIFY(finished[0][0].toBool());
        QAbstractItemModel *model = child<QListView>(&dlg, "stepList")->model();
        for (int i = 0; i < model->rowCount(); ++i)
            QCOMPARE(model->index(i, 0).data(PlanModel::StatusRole).toInt(), int(PlanStep::Status::Pending));
        QVERIFY(!child<QProgressBar>(&dlg, "progressBar")->isVisible());
        QVERIFY(!child<QLabel>(&dlg, "resultLabel")->isVisible());
        QVERIFY(child<QPushButton>(&dlg, "startButton")->isEnabled());
        QCOMPARE(child<QPushButton>(&dlg, "closeButton")->text(), QString("Close"));
    }
};

int main(int argc, char *argv[])
{
    // Never open real windows, even when the binary is run directly.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TstPlanDialog test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}
#include "tst_plandialog.moc"
