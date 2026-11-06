#include <QtTest>
#include <QLabel>
#include <QPushButton>
#include "corelib.h"
#include "coreutil.h"
#include "elidedlabel.h"
#include "fakelibrary.h"
#include "mainwindow.h"
#include "micmatrixview.h"
#include "profileartwork.h"
#include "profilepaths.h"
#include "profileregistry.h"
#include "palette.h"

using namespace smt;

static QtMessageHandler gPreviousHandler = nullptr;

// Drops the offscreen platform's noise so a real warning stands out.
static void filterPlatformNoise(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (qstrcmp(context.category, "qt.qpa.fonts") == 0
        || msg == QLatin1String("This plugin does not support propagateSizeHints()"))
        return;
    gPreviousHandler(type, context, msg);
}

class TstMainWindow : public QObject
{
    Q_OBJECT
    QTemporaryDir mTmp;

    template<typename T>
    static T *child(QObject *parent, const char *name)
    {
        T *c = parent->findChild<T *>(name);
        Q_ASSERT(c);
        return c;
    }

private slots:
    void initTestCase()
    {
        gPreviousHandler = qInstallMessageHandler(filterPlatformNoise);
        // Never the user's settings, cache or the network.
        qputenv("SMT_APP_DATA_DIR", mTmp.filePath("data").toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, mTmp.filePath("settings"));
        QSettings().setValue("DownloadProfileImages", false);
    }

    void windowTitleNamesTheProfile()
    {
        MainWindow w;
        auto *core = w.findChild<Core *>();
        QVERIFY(core);
        QCOMPARE(w.windowTitle(), mainWindowTitle(kApplicationName, core->profile().name));
        QVERIFY(core->setProfileById(kEwcProfileId));
        QVERIFY(w.windowTitle().endsWith(core->profile().name));
        QVERIFY(w.windowTitle().startsWith(kApplicationName));
    }

    void changeLocationButtonSitsUnderTheArtworkAndFollowsApply()
    {
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto *art = child<ProfileArtwork>(&w, "profileArtwork");
        auto *btn = child<QPushButton>(&w, "useInSpitfireButton");
        QCOMPARE(btn->parentWidget(), art->parentWidget());
        QVERIFY(btn->geometry().top() >= art->geometry().bottom());
        QVERIFY(btn->isEnabled());
        auto *core = w.findChild<Core *>();
        emit core->applyStepChanged(0, PlanStep::Status::Running, QString());
        QVERIFY(!btn->isEnabled());
        emit core->applyFinished(true, QString());
        QVERIFY(btn->isEnabled());
    }

    void emptyStateThenMatrixLegendStatusLinesAndApplyText()
    {
        const QString src = makeFakeLibrary(mTmp.filePath("src"), {"TR01", "MO01"}, {"brass", "woodwinds"}, 2, 1000);
        const QString dst = mTmp.filePath("dst/lib");
        QSettings().remove("Profiles"); // no stored folders: nothing chosen yet
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto *core = w.findChild<Core *>();
        auto *view = child<MicMatrixView>(&w, "matrixView");
        auto *hint = child<QLabel>(&w, "emptyHintLabel");
        auto *legend = child<QLabel>(&w, "legendLabel");
        auto *footer = child<QLabel>(&w, "installationSizeLabel");
        auto *apply = child<QPushButton>(&w, "applyButton");
        QVERIFY(!core->hasSourceContent());
        QVERIFY(hint->isVisible());
        QVERIFY(hint->text().contains("Choose a Source Installation to begin"));
        QVERIFY(hint->text().contains("Its microphones and sizes will appear here."));
        QVERIFY(!view->isVisible() && !legend->isVisible() && !footer->isVisible());
        QTRY_COMPARE(apply->text(), QString("Apply"));
        QVERIFY(child<ElidedLabel>(&w, "sourceDirLabel")->text() == "No folder chosen");

        QSignalSpy scan(core, &Core::scanFinished);
        core->setSourceDir(src);
        core->setDestinationDirectoryPath(dst);
        QTRY_VERIFY(scan.count() > 0);
        QTRY_VERIFY(view->isVisible());
        QVERIFY(!hint->isVisible());
        QVERIFY(legend->isVisible() && footer->isVisible());
        for (const char *word : {"Included", "Some included", "Partly present", "Always on", "Not included"})
            QVERIFY2(legend->text().contains(word), word);
        // Everything is selected and nothing is in the destination yet: the whole copy is pending.
        QTRY_VERIFY(apply->text().startsWith("Apply (+"));
        QVERIFY(apply->text().contains("−"));
        QVERIFY(apply->isEnabled());

        QSignalSpy fin(core, &Core::applyFinished);
        QVERIFY(core->startApply(core->buildPlan()));
        QTRY_VERIFY_WITH_TIMEOUT(fin.count() > 0, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(apply->text(), QString("Apply (update databases)"), 10000);
        QVERIFY(apply->isEnabled()); // database rebuilds are still steps

        // A destination equal to the source: refused, Apply greyed with the reason.
        core->setDestinationDirectoryPath(src);
        QTRY_VERIFY(!apply->isEnabled());
        QTRY_VERIFY(!apply->toolTip().isEmpty());

        core->setSourceDir(QString()); // back to nothing chosen
        QTRY_VERIFY(hint->isVisible());
        QVERIFY(!view->isVisible());
    }

    // While a scan runs Apply waits, and the scan's progress is said beside it (muted, not the error colour).
    void applyWaitsForTheScanAndSaysSo()
    {
        const QString name = "Spitfire Audio - BBC Symphony Orchestra";
        const QString src = makeFakeLibrary(mTmp.filePath("scan/" + name), {"TR01", "MO01"}, {"brass", "woodwinds"}, 600, 1);
        QSettings().remove("Profiles");
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto *core = w.findChild<Core *>();
        auto *apply = child<QPushButton>(&w, "applyButton");
        auto *why = child<QLabel>(&w, "applyStatusLabel");
        auto *hint = child<QLabel>(&w, "emptyHintLabel");
        core->setSourceDir(src);
        core->setDestinationDirectoryPath(mTmp.filePath("scan/dst/" + name));
        QVERIFY(core->scanning()); // the scan runs on a pool thread, so it is still going
        QTRY_VERIFY(why->text().startsWith("Scanning"));
        QVERIFY(!apply->isEnabled());
        QVERIFY(why->styleSheet().contains(Palette::body.name())); // muted grey, not the error colour
        QVERIFY(!why->styleSheet().contains(Palette::errorMuted.name()));
        QTRY_VERIFY_WITH_TIMEOUT(!core->scanning(), 20000);
        QTRY_VERIFY(!why->text().startsWith("Scanning"));
        QTRY_VERIFY(apply->isEnabled());
        QVERIFY(!hint->isVisible());
    }

    // The Choose… flow without the dialog: another name gets the exact-named child, which is what is shown and stored;
    // the source's own parent resolves to the source and Apply says why it cannot run.
    void aPickedFolderBecomesTheExactNamedChildAndAnOverlapIsSaid()
    {
        const QString name = "Spitfire Audio - BBC Symphony Orchestra";
        const QString src = makeFakeLibrary(mTmp.filePath("pick/" + name), {"TR01", "MO01"}, {"brass"}, 2, 1000);
        const QString chosen = mTmp.filePath("pick-dst/Trimmed");
        QDir().mkpath(chosen);
        QSettings().remove("Profiles");
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto *core = w.findChild<Core *>();
        auto *label = child<ElidedLabel>(&w, "destDirLabel");
        auto *apply = child<QPushButton>(&w, "applyButton");
        auto *why = child<QLabel>(&w, "applyStatusLabel");
        core->setSourceDir(src);
        w.chooseDestinationFolder(chosen);
        const QString resolved = chosen + "/" + name;
        QCOMPARE(core->destinationDirectoryPath(), resolved);
        QCOMPARE(label->text(), resolved); // the label shows the resolved path
        QVERIFY(!QFileInfo::exists(resolved)); // nothing was created by choosing
        ProfilePaths paths;
        QCOMPARE(paths.destination(core->profile().id), resolved); // and it is what is stored
        QTRY_VERIFY(apply->isEnabled());
        // the exact name is used as it is
        const QString exact = mTmp.filePath("pick-exact/" + name);
        QDir().mkpath(exact);
        w.chooseDestinationFolder(exact);
        QCOMPARE(core->destinationDirectoryPath(), exact);
        // the source's own parent resolves to the source: an overlap, said beside Apply
        w.chooseDestinationFolder(QFileInfo(src).absolutePath());
        QCOMPARE(core->destinationDirectoryPath(), src);
        QTRY_VERIFY(why->text().contains("same folder as the source"));
        QVERIFY(!apply->isEnabled());
    }

    // Why Apply is greyed is said next to it, not only in a tooltip.
    void refusalIsShownBesideApply()
    {
        const QString src = makeFakeLibrary(mTmp.filePath("Spitfire Audio - Test Library"), {"TR01", "MO01"},
                                            {"brass", "woodwinds"}, 2, 1000);
        QSettings().remove("Profiles");
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto *core = w.findChild<Core *>();
        auto *apply = child<QPushButton>(&w, "applyButton");
        auto *why = child<QLabel>(&w, "applyStatusLabel");
        QTRY_VERIFY(why->text().isEmpty()); // nothing chosen: the empty-state hint speaks
        const QSize applySize = apply->size(); // the same in every state, whatever the text or message
        // and it stays at the right edge: measured from the window's edge, because the window may grow once the table
        // appears (wider fonts on Windows)
        const int applyRight = w.width() - apply->geometry().right();
        const int applyTop = apply->geometry().top();
        QVERIFY(applySize.width() > 100);

        core->setSourceDir(src);
        core->setDestinationDirectoryPath(mTmp.filePath("good/Spitfire Audio - Test Library"));
        QTRY_VERIFY(apply->isEnabled());
        QVERIFY(why->text().isEmpty());
        QTRY_VERIFY(apply->text().startsWith("Apply (+"));
        QCOMPARE(apply->size(), applySize); // "Apply (+... / −...)"
        QCOMPARE(w.width() - apply->geometry().right(), applyRight);
        QVERIFY(qAbs(apply->geometry().top() - applyTop) < 40); // no message: same row, not pushed around
        const QSize before = w.minimumSizeHint();
        core->setDestinationDirectoryPath(mTmp.filePath("wrongname/lib")); // not named like the source
        QTRY_VERIFY(why->text().contains("must be named")); // after the scan's progress text
        QVERIFY(why->styleSheet().contains(Palette::errorMuted.name()));
        QVERIFY(!apply->isEnabled());
        QCOMPARE(apply->toolTip(), why->text());
        QVERIFY(why->wordWrap());
        auto *view = child<MicMatrixView>(&w, "matrixView");
        QCOMPARE(why->mapTo(&w, QPoint(0, 0)).x(), view->mapTo(&w, QPoint(0, 0)).x()); // starts at the table's left edge
        QVERIFY(why->geometry().right() <= apply->geometry().left()); // left of Apply
        QCOMPARE(w.minimumSizeHint(), before); // a long message does not widen the window
        QVERIFY(why->geometry().height() > 0);
        QCOMPARE(apply->size(), applySize); // with a long message beside it
        QCOMPARE(w.width() - apply->geometry().right(), applyRight);

        core->setDestinationDirectoryPath(mTmp.filePath("good/Spitfire Audio - Test Library"));
        QTRY_VERIFY(why->text().isEmpty());
        QTRY_VERIFY(apply->isEnabled());

        // No microphone selected (Whitacre lists none of this library's tokens, and has no locked mic).
        QVERIFY(core->setProfileById(kEwcProfileId));
        forAllMicSignalGroupKeys(core->profile(), [&](const MicSignalGroupKey &key) { core->setMicEnabled(key, false); });
        QVERIFY(!core->anyMicEnabled());
        QTRY_COMPARE(why->text(), QString("Select at least one microphone."));
        QVERIFY(!apply->isEnabled());
        QCOMPARE(apply->size(), applySize); // plain "Apply"
    }
};

QTEST_MAIN(TstMainWindow)
#include "tst_mainwindow.moc"
