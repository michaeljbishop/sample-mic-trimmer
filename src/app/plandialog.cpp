#include "plandialog.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include "corelib.h"
#include "coreutil.h"
#include "planmodel.h"
#include "palette.h"

using namespace smt;

static void showMessage(QLabel *label, const QString &text, bool ok)
{
    label->setText(text);
    label->setStyleSheet("color: " + (ok ? Palette::success : Palette::error).name());
    label->show();
}

PlanDialog::PlanDialog(Core *core, const Plan &plan, QWidget *parent)
    : QDialog(parent)
    , mCore(core)
    , mPlan(plan)
    , mModel(new PlanModel(this))
    , mStart(new QPushButton(tr("Start")))
    , mClose(new QPushButton(tr("Close")))
    , mProgress(new QProgressBar)
    , mProgressLine(new QLabel)
    , mResult(new QLabel)
{
    setWindowTitle(tr("Apply"));
    resize(720, 560);
    mModel->setPlan(plan);

    auto *summary = new QFormLayout;
    summary->addRow(tr("Data to copy:"), new QLabel(formatByteSize(size_t(plan.bytesToAdd))));
    summary->addRow(tr("Data to remove:"), new QLabel(formatByteSize(size_t(plan.bytesToFree))));
    summary->addRow(tr("Databases:"), new QLabel(formatByteSize(size_t(plan.bytesForDatabases))));
    auto *freeAfter = new QLabel;
    if (plan.freeBytesAfter < 0)
        showMessage(freeAfter, tr("Needs %1 more").arg(formatByteSize(size_t(-plan.freeBytesAfter))), false);
    else
        freeAfter->setText(formatByteSize(size_t(plan.freeBytesAfter)));
    summary->addRow(tr("Free space after:"), freeAfter);

    auto *refusals = new QLabel;
    refusals->setObjectName("refusalsLabel");
    refusals->setTextFormat(Qt::PlainText);
    refusals->setWordWrap(true);
    refusals->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (!plan.refusals.isEmpty())
        showMessage(refusals, "• " + plan.refusals.join("\n• "), false);
    else
        refusals->hide();

    auto *noFileChanges = new QLabel(tr("No file changes; databases will be refreshed only."));
    noFileChanges->setObjectName("noFileChangesLabel");
    noFileChanges->setVisible(plan.canRun() && !plan.isEmpty() && !plan.hasFileChanges());

    auto *list = new QListView;
    list->setObjectName("stepList");
    list->setModel(mModel);
    list->setUniformItemSizes(true);

    auto *warning = new QLabel(tr("Quit the Spitfire plugin/host (DAW) before applying."));
    warning->setObjectName("warningLabel");
    warning->setStyleSheet("font-weight: bold");

    mProgress->setObjectName("progressBar");
    mProgress->setRange(0, 1000);
    mProgress->hide();
    mProgressLine->setObjectName("progressLabel");
    mProgressLine->hide();
    mResult->setObjectName("resultLabel");
    mResult->setTextFormat(Qt::PlainText);
    mResult->setWordWrap(true);
    mResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mResult->hide();

    mStart->setObjectName("startButton");
    mClose->setObjectName("closeButton");
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(mClose);
    buttons->addWidget(mStart);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(summary);
    layout->addWidget(refusals);
    layout->addWidget(noFileChanges);
    layout->addWidget(list, 1);
    layout->addWidget(warning);
    layout->addWidget(mProgress);
    layout->addWidget(mProgressLine);
    layout->addWidget(mResult);
    layout->addLayout(buttons);

    connect(mStart, &QPushButton::clicked, this, [this] {
        if (!mCore->startApply(mPlan))
            return showMessage(mResult, tr("An apply is already running."), false);
        mResult->hide();
        setState(State::Running);
    });
    connect(mClose, &QPushButton::clicked, this, [this] {
        if (mState != State::Running)
            return QDialog::reject();
        mCore->cancelApply();
        mClose->setEnabled(false);
    });

    // Context `this`: disconnected when the dialog goes away. Signals before Start are another apply's.
    connect(mCore, &Core::applyStepChanged, this, [this, list](int index, PlanStep::Status status, const QString &error) {
        if (mState != State::Running)
            return;
        mModel->setStepStatus(index, status, error);
        list->scrollTo(mModel->index(index));
    });
    connect(mCore, &Core::applyProgress, this, [this](qint64 done, qint64 total, double bytesPerSecond) {
        if (mState != State::Running)
            return;
        mProgress->setValue(total > 0 ? int(done * 1000 / total) : 1000);
        mProgressLine->setText(formatProgressLine(done, total, bytesPerSecond));
    });
    connect(mCore, &Core::applyFinished, this, [this](bool success, const QString &message) {
        if (mState != State::Running)
            return;
        if (success)
            mProgress->setValue(1000);
        showMessage(mResult, message, success);
        setState(State::Finished);
    });

    setState(State::Review);
}

void PlanDialog::setState(State state)
{
    mState = state;
    mStart->setEnabled(state == State::Review && mPlan.canRun());
    mStart->setDefault(mStart->isEnabled());
    mClose->setDefault(state != State::Running && !mStart->isEnabled()); // Enter closes; never cancels
    mClose->setText(state == State::Running ? tr("Cancel") : tr("Close"));
    mClose->setEnabled(true);
    mProgress->setVisible(state != State::Review);
    mProgressLine->setVisible(state != State::Review);
}

void PlanDialog::reject()
{
    if (mState != State::Running)
        return QDialog::reject();
    showMessage(mResult, tr("Cannot close while applying; use Cancel."), false);
}
