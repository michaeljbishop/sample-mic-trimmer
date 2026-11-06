#include <QtTest>
#include <QAbstractItemModelTester>
#include <QHeaderView>
#include "corelib.h"
#include "coreutil.h"
#include "fakelibrary.h"
#include "micmatrixmodel.h"
#include "micmatrixview.h"
#include "profileregistry.h"
#include "quickset.h"

using namespace smt;

static const QStringList kTokens{"TR01", "MO01"};
static const QStringList kSections{"brass", "woodwinds"};

static bool waitFor(QSignalSpy &spy)
{
    return spy.count() > 0 || spy.wait(10000);
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

class TstMicMatrixModel : public QObject
{
    Q_OBJECT

    QTemporaryDir mTmp;
    QString mSrc, mDst;

    // Index of a mic's cell: column 0 = name, 1.. = profile groups by key.
    static QModelIndex cell(const MicMatrixModel &m, const Core &core, const QString &mic, int column)
    {
        for (int g = 0; g < m.rowCount(); ++g) {
            const QModelIndex group = m.index(g, 0);
            for (int r = 0; r < m.rowCount(group); ++r) {
                const QModelIndex name = m.index(r, 0, group);
                if (name.data().toString().startsWith(core.profile().mic(mic)->name))
                    return m.index(r, column, group);
            }
        }
        return {};
    }
    static int col(const Core &core, const QString &group)
    {
        for (int i = 0; i < core.profile().groups.size(); ++i)
            if (core.profile().groups.at(i).key == group)
                return i + 1;
        return -1;
    }
    static Qt::CheckState check(const QModelIndex &i) { return i.data(Qt::CheckStateRole).value<Qt::CheckState>(); }

    // Follow-mode Core on the fake source with a destination that holds TR01/brass complete and
    // TR01/woodwinds half-copied.
    void startFollow(Core &core)
    {
        for (const char *section : {"brass", "woodwinds"}) {
            const QString rel = fakeSectionDir(section);
            QDir().mkpath(mDst + "/" + rel);
            for (const auto &f : scanTree(mSrc))
                if (f.path.startsWith(rel + "/") && f.path.contains("_TR01_"))
                    QVERIFY(QFile::copy(mSrc + "/" + f.path, mDst + "/" + f.path));
        }
        QVERIFY(QFile::remove(mDst + "/" + fakeSectionDir("woodwinds") + "/INSTR1_TR01_01.spitfire"));
        core.setSelectionFollowsDestination(true);
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(mSrc);
        core.setDestinationDirectoryPath(mDst);
        QVERIFY(waitFor(scan));
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        gPreviousHandler = qInstallMessageHandler(filterPlatformNoise);
    }

    void init()
    {
        QVERIFY(mTmp.isValid());
        mSrc = makeFakeLibrary(mTmp.filePath("src"), kTokens, kSections, 2, 1000);
        mDst = mTmp.filePath("dst/lib");
        const QString dataDir = qEnvironmentVariable("SMT_APP_DATA_DIR",
                QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
        QDir(dataDir + "/ScanCache").removeRecursively();
    }

    void cleanup()
    {
        QDir(mTmp.path()).removeRecursively();
        QDir().mkpath(mTmp.path());
    }

    void bbcsoTree()
    {
        Core core;
        MicMatrixModel m(&core);
        QAbstractItemModelTester tester(&m);
        QCOMPARE(m.rowCount(), 6);
        QCOMPARE(m.columnCount(), 6); // name + 4 sections + total
        QStringList groups;
        int mics = 0;
        for (int g = 0; g < m.rowCount(); ++g) {
            groups << m.index(g, 0).data().toString();
            mics += m.rowCount(m.index(g, 0));
        }
        QCOMPARE(groups, (QStringList{"Mixes", "CTAO", "Stereo/Mono", "Spills", "Other", "Atmos"}));
        QCOMPARE(mics, 20);
        QCOMPARE(m.headerData(0, Qt::Horizontal).toString(), QString("Microphone"));
        QCOMPARE(m.headerData(5, Qt::Horizontal).toString(), QString("Row total"));
        QCOMPARE(m.headerData(1, Qt::Horizontal).toString(), core.profile().groups.first().name);
        QVERIFY(!m.flags(m.index(0, 0)).testFlag(Qt::ItemIsUserCheckable)); // group rows have no checkbox
        QVERIFY(!m.index(0, 0).data(Qt::CheckStateRole).isValid());
    }

    void whitacreTreeAndProfileSwitchResets()
    {
        Core core;
        MicMatrixModel m(&core);
        QAbstractItemModelTester tester(&m);
        QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
        QVERIFY(core.setProfileById(kEwcProfileId));
        QCOMPARE(reset.count(), 1);
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(m.columnCount(), 7); // name + 5 sections + total
        QStringList groups;
        int mics = 0;
        for (int g = 0; g < m.rowCount(); ++g) {
            groups << m.index(g, 0).data().toString();
            mics += m.rowCount(m.index(g, 0));
        }
        QCOMPARE(groups, (QStringList{"Core", "Auxiliary", "Stereo"}));
        QCOMPARE(mics, 9);
        QVERIFY(core.setProfileById(kBbcsoProfileId));
        QCOMPARE(reset.count(), 2);
        QCOMPARE(m.rowCount(), 6);
        QCOMPARE(m.columnCount(), 6);
    }

    void checkStatesAndPartlyPresent()
    {
        Core core;
        MicMatrixModel m(&core);
        QAbstractItemModelTester tester(&m);
        startFollow(core);
        const int brass = col(core, "brass"), woods = col(core, "woodwinds");

        QCOMPARE(check(cell(m, core, "tr01", brass)), Qt::Checked);
        QCOMPARE(check(cell(m, core, "tr01", woods)), Qt::PartiallyChecked);
        QCOMPARE(check(cell(m, core, "mo01", brass)), Qt::Unchecked);
        QCOMPARE(check(cell(m, core, "tr01", 0)), Qt::PartiallyChecked); // dash: two of four sections wanted (one of them partly present)
        QCOMPARE(check(cell(m, core, "mo01", 0)), Qt::Unchecked);

        const QString shown = cell(m, core, "tr01", woods).data().toString();
        QVERIFY2(shown.endsWith(" partly present"), qPrintable(shown));
        QVERIFY(shown.startsWith(formatByteSize(core.spaceRequired({"tr01", "woodwinds"}))));
        QVERIFY(!cell(m, core, "tr01", woods).data(Qt::ToolTipRole).toString().isEmpty());
        QVERIFY(cell(m, core, "tr01", brass).data().toString() == formatByteSize(core.spaceRequired({"tr01", "brass"})));
        QVERIFY(!cell(m, core, "tr01", brass).data(Qt::ToolTipRole).toString().isEmpty());
        QVERIFY(core.spaceRequired({"mo01", "brass"}) > 0); // sizes shown for unticked cells too
    }

    void clickingSemantics()
    {
        Core core;
        MicMatrixModel m(&core);
        QAbstractItemModelTester tester(&m);
        startFollow(core);
        const QModelIndex partial = cell(m, core, "tr01", col(core, "woodwinds"));
        QVERIFY(m.flags(partial).testFlag(Qt::ItemIsUserCheckable));
        QVERIFY(!m.flags(partial).testFlag(Qt::ItemIsUserTristate));
        QCOMPARE(check(partial), Qt::PartiallyChecked);
        QVERIFY(m.setData(partial, Qt::Checked, Qt::CheckStateRole)); // what a view proposes after a click
        QCOMPARE(check(partial), Qt::Unchecked);
        QVERIFY(!core.isMicEnabled({"tr01", "woodwinds"}));
        QVERIFY(m.setData(partial, Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(check(partial), Qt::Checked);
        QVERIFY(m.setData(partial, Qt::Unchecked, Qt::CheckStateRole));
        QCOMPARE(check(partial), Qt::Unchecked);
    }

    void rowToggleAndLockedMics()
    {
        Core core;
        MicMatrixModel m(&core);
        QAbstractItemModelTester tester(&m);
        startFollow(core);
        const QModelIndex locked = cell(m, core, "mi01", 0);
        QVERIFY(core.profile().mic("mi01")->locked);
        QVERIFY(!m.flags(locked).testFlag(Qt::ItemIsUserCheckable));
        QVERIFY(!m.flags(locked).testFlag(Qt::ItemIsEnabled));
        QVERIFY(!m.flags(cell(m, core, "mi01", 1)).testFlag(Qt::ItemIsUserCheckable));
        QCOMPARE(check(locked), Qt::Checked);
        QVERIFY(locked.data().toString().contains("always on"));
        QCOMPARE(locked.data(Qt::ToolTipRole).toString(), QString("Always on"));
        QCOMPARE(cell(m, core, "mi01", 1).data(Qt::ToolTipRole).toString(), QString("Always on"));
        QVERIFY(cell(m, core, "mo01", 0).data(Qt::ToolTipRole).toString().contains("all sections"));
        QVERIFY(m.headerData(1, Qt::Horizontal, Qt::ToolTipRole).toString().contains("whole column"));
        QVERIFY(!m.headerData(0, Qt::Horizontal, Qt::ToolTipRole).isValid());
        QVERIFY(!m.setData(locked, Qt::Unchecked, Qt::CheckStateRole));
        QVERIFY(!m.setData(cell(m, core, "mi01", 1), Qt::Unchecked, Qt::CheckStateRole));

        const QModelIndex row = cell(m, core, "mo01", 0);
        QVERIFY(m.setData(row, Qt::Checked, Qt::CheckStateRole));
        for (int c = 1; c <= 4; ++c)
            QCOMPARE(check(cell(m, core, "mo01", c)), Qt::Checked);
        QCOMPARE(check(row), Qt::Checked);
        QVERIFY(m.setData(row, Qt::Unchecked, Qt::CheckStateRole));
        QCOMPARE(check(row), Qt::Unchecked);
        // tr01 has a partly present cell: ticking the row completes nothing, a second click unticks it all.
        const QModelIndex tr = cell(m, core, "tr01", 0);
        QVERIFY(m.setData(tr, Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(check(cell(m, core, "tr01", 3)), Qt::Checked);
        QVERIFY(m.setData(tr, Qt::Checked, Qt::CheckStateRole)); // everything on, one cell partly: unticks
        QCOMPARE(check(tr), Qt::Unchecked);
    }

    void columnToggleSkipsLockedMics()
    {
        Core core;
        MicMatrixModel m(&core);
        QAbstractItemModelTester tester(&m);
        startFollow(core);
        const int brass = col(core, "brass");
        QCOMPARE(m.headerData(brass, Qt::Horizontal, Qt::CheckStateRole).value<Qt::CheckState>(), Qt::PartiallyChecked);
        QSignalSpy header(&m, &QAbstractItemModel::headerDataChanged);
        m.toggleColumn(brass);
        QVERIFY(header.count() > 0);
        QCOMPARE(m.columnCheckState(brass), Qt::Checked);
        for (const MicDef &mic : core.profile().mics)
            QVERIFY2(core.isMicEnabled({mic.token, "brass"}), qPrintable(mic.token));
        m.toggleColumn(brass);
        QCOMPARE(m.columnCheckState(brass), Qt::Unchecked);
        for (const MicDef &mic : core.profile().mics)
            QCOMPARE(core.isMicEnabled({mic.token, "brass"}), mic.locked);
        QVERIFY(core.isMicEnabled({"mi01", "brass"}));
        // A dash ticks the missing mics and leaves the partly present cell enabled and partly present;
        // the column then counts as ticked (partly present is wanted) and the next click unticks all of it.
        const int woods = col(core, "woodwinds");
        m.toggleColumn(woods);
        QCOMPARE(m.columnCheckState(woods), Qt::Checked);
        QCOMPARE(check(cell(m, core, "tr01", woods)), Qt::PartiallyChecked);
        QVERIFY(core.isMicEnabled({"tr01", "woodwinds"}));
        QCOMPARE(check(cell(m, core, "mo01", woods)), Qt::Checked);
        m.toggleColumn(woods);
        QCOMPARE(m.columnCheckState(woods), Qt::Unchecked);
    }

    void partlyPresentCountsAsWantedInRowAndColumn()
    {
        Core core;
        MicMatrixModel m(&core);
        QAbstractItemModelTester tester(&m);
        startFollow(core); // tr01: brass complete, woodwinds partly present, the rest off
        core.setMicEnabled({"tr01", "percussion"}, true);
        core.setMicEnabled({"tr01", "strings"}, true);
        QCOMPARE(check(cell(m, core, "tr01", 0)), Qt::Checked); // all wanted, one partly present
        QCOMPARE(check(cell(m, core, "tr01", col(core, "woodwinds"))), Qt::PartiallyChecked);
        // Every other mic on in brass too: the column is ticked, and a click unticks the whole column
        // (fully copied mics included), not just the partly present cell.
        const int brass = col(core, "brass"), woods = col(core, "woodwinds");
        m.toggleColumn(woods); // dash -> ticks the missing
        QCOMPARE(m.columnCheckState(woods), Qt::Checked);
        QCOMPARE(check(cell(m, core, "mo01", woods)), Qt::Checked);
        QCOMPARE(check(cell(m, core, "tr01", woods)), Qt::PartiallyChecked); // left alone
        m.toggleColumn(woods); // ticked -> unticks all
        QCOMPARE(m.columnCheckState(woods), Qt::Unchecked);
        QCOMPARE(check(cell(m, core, "mo01", woods)), Qt::Unchecked);
        QCOMPARE(check(cell(m, core, "tr01", woods)), Qt::Unchecked);
        QCOMPARE(check(cell(m, core, "tr01", brass)), Qt::Checked); // other columns untouched
        // a ticked row (via the row box) unticks all its sections
        QVERIFY(m.setData(cell(m, core, "tr01", 0), Qt::Unchecked, Qt::CheckStateRole));
        QCOMPARE(check(cell(m, core, "tr01", 0)), Qt::Unchecked);
    }

    void sizesAndTotalsFollowCore()
    {
        Core core;
        MicMatrixModel m(&core);
        QAbstractItemModelTester tester(&m);
        startFollow(core);
        const int brass = col(core, "brass"), woods = col(core, "woodwinds");
        const size_t b = core.spaceRequired({"tr01", "brass"}), w = core.spaceRequired({"tr01", "woodwinds"});
        QCOMPARE(cell(m, core, "tr01", m.totalColumn()).data().toString(), formatByteSize(b + w)); // partly counts as selected
        QCOMPARE(cell(m, core, "tr01", m.totalColumn()).data(Qt::TextAlignmentRole).toInt(), int(Qt::AlignRight | Qt::AlignVCenter));
        QVERIFY(m.headerData(brass, Qt::Horizontal, MicMatrixModel::ColumnTotalRole).toULongLong() >= b);

        core.setMicEnabled({"tr01", "brass"}, false);
        QCOMPARE(cell(m, core, "tr01", m.totalColumn()).data().toString(), formatByteSize(w));
        const qulonglong colBefore = m.headerData(woods, Qt::Horizontal, MicMatrixModel::ColumnTotalRole).toULongLong();
        core.setMicEnabled({"mo01", "woodwinds"}, true);
        QCOMPARE(m.headerData(woods, Qt::Horizontal, MicMatrixModel::ColumnTotalRole).toULongLong(),
                 colBefore + core.spaceRequired({"mo01", "woodwinds"}));

        QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
        emit core.spaceRequiredChanged({"tr01", "woodwinds"}, 5);
        QVERIFY(changed.count() > 0);
        QCOMPARE(changed.last().at(0).toModelIndex().column(), 0);
        QCOMPARE(changed.last().at(1).toModelIndex().column(), m.totalColumn());
    }

    void followsQuickSets()
    {
        QTemporaryDir bundle;
        Core core;
        core.setProfile(profileWithQuickSet(kBbcsoProfileId, bundle));
        MicMatrixModel m(&core);
        QAbstractItemModelTester tester(&m);
        QVERIFY(!core.profile().quickSets.isEmpty());
        core.applyQuickSet(0);
        const QStringList families = core.profile().quickSets.first().families;
        for (const MicDef &mic : core.profile().mics)
            QCOMPARE(check(cell(m, core, mic.token, 0)),
                     mic.locked || families.contains(mic.family) ? Qt::Checked : Qt::Unchecked);
    }

    void keyboardMovesCellToCellAndSpaceToggles()
    {
        Core core;
        MicMatrixModel m(&core);
        MicMatrixView view(&m);
        view.resize(900, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        startFollow(core);
        const int brass = col(core, "brass"), woods = col(core, "woodwinds");
        view.setCurrentIndex(cell(m, core, "tr01", 0));
        QTest::keyClick(&view, Qt::Key_Right);
        QCOMPARE(view.currentIndex(), cell(m, core, "tr01", brass));
        QTest::keyClick(&view, Qt::Key_Space);
        QCOMPARE(check(cell(m, core, "tr01", brass)), Qt::Unchecked);
        for (int c = brass; c < woods; ++c)
            QTest::keyClick(&view, Qt::Key_Right);
        QCOMPARE(view.currentIndex(), cell(m, core, "tr01", woods));
        QCOMPARE(check(cell(m, core, "tr01", woods)), Qt::PartiallyChecked);
        QTest::keyClick(&view, Qt::Key_Space); // partly present: unticks
        QCOMPARE(check(cell(m, core, "tr01", woods)), Qt::Unchecked);
        QTest::keyClick(&view, Qt::Key_Space);
        QCOMPARE(check(cell(m, core, "tr01", woods)), Qt::Checked);
    }

    void headerClickTogglesColumn()
    {
        Core core;
        MicMatrixModel m(&core);
        MicMatrixView view(&m);
        view.resize(900, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        disableAllMics(core);
        QCOMPARE(m.columnCheckState(1), Qt::Unchecked);
        QHeaderView *h = view.header();
        QTest::mouseClick(h->viewport(), Qt::LeftButton, {}, QPoint(h->sectionViewportPosition(1) + 10, 10));
        QCOMPARE(m.columnCheckState(1), Qt::Checked);
        // a click on the "Microphone" header does nothing
        QTest::mouseClick(h->viewport(), Qt::LeftButton, {}, QPoint(10, 10));
        QCOMPARE(m.columnCheckState(1), Qt::Checked);
        // a profile switch and back rebuilds the layout: groups spanned and expanded, headings still clickable
        QVERIFY(core.setProfileById(kEwcProfileId));
        QVERIFY(core.setProfileById(kBbcsoProfileId));
        disableAllMics(core);
        for (int g = 0; g < m.rowCount(); ++g) {
            QVERIFY(view.isFirstColumnSpanned(g, QModelIndex()));
            QVERIFY(view.isExpanded(m.index(g, 0)));
        }
        QTest::mouseClick(h->viewport(), Qt::LeftButton, {}, QPoint(h->sectionViewportPosition(1) + 10, 10));
        QCOMPARE(m.columnCheckState(1), Qt::Checked);
        // groups span the row and a click on a cell toggles it
        QVERIFY(view.isFirstColumnSpanned(0, QModelIndex()));
        const QModelIndex c = cell(m, core, "tr01", 1);
        const bool before = core.isMicEnabled({"tr01", core.profile().groups.first().key});
        QTest::mouseClick(view.viewport(), Qt::LeftButton, {}, view.visualRect(c).center());
        QCOMPARE(core.isMicEnabled({"tr01", core.profile().groups.first().key}), !before);
    }
};

QTEST_MAIN(TstMicMatrixModel)
#include "tst_micmatrixmodel.moc"
