#include <QtTest>
#include <QApplication>
#include <QStyleHints>
#include <QBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QStyle>
#include "darktheme.h"
#include "corelib.h"
#include "coreutil.h"
#include "fakelibrary.h"
#include "micmatrixcolors.h"
#include "micmatrixmodel.h"
#include "micmatrixview.h"
#include "profileregistry.h"

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

static int distance(const QColor &a, const QColor &b)
{
    return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue());
}

static double luma(const QColor &c)
{
    return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
}

class TstMicMatrixView : public QObject
{
    Q_OBJECT
    QTemporaryDir mTmp;

    static QModelIndex cell(const MicMatrixModel &m, const Core &core, const QString &mic, int column)
    {
        for (int g = 0; g < m.rowCount(); ++g)
            for (int r = 0; r < m.rowCount(m.index(g, 0)); ++r)
                if (m.index(r, 0, m.index(g, 0)).data().toString().startsWith(core.profile().mic(mic)->name))
                    return m.index(r, column, m.index(g, 0));
        return {};
    }
    // Grabs are device-pixel images (2x on a retina screen); positions here are logical.
    static QColor pixel(const QImage &img, int x, int y)
    {
        return img.pixelColor(int(x * img.devicePixelRatio()), int(y * img.devicePixelRatio()));
    }
    // The cell's colour at its right end, clear of the text.
    static QColor fillAt(const MicMatrixView &view, const QImage &img, const QModelIndex &i)
    {
        const QRect r = view.visualRect(i);
        return pixel(img, r.right() - 6, r.center().y());
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        gPreviousHandler = qInstallMessageHandler(filterPlatformNoise);
    }

    // TR01 in brass and woodwinds on disk, woodwinds half copied; follow mode on.
    void prepare(Core &core)
    {
        const QString src = makeFakeLibrary(mTmp.filePath("src"), {"TR01", "MO01"}, {"brass", "woodwinds"}, 2, 1000);
        const QString dst = mTmp.filePath("dst/lib");
        for (const char *section : {"brass", "woodwinds"}) {
            const QString rel = fakeSectionDir(section);
            QDir().mkpath(dst + "/" + rel);
            for (const auto &f : scanTree(src))
                if (f.path.startsWith(rel + "/") && f.path.contains("_TR01_"))
                    QVERIFY(QFile::copy(src + "/" + f.path, dst + "/" + f.path));
        }
        QVERIFY(QFile::remove(dst + "/" + fakeSectionDir("woodwinds") + "/INSTR1_TR01_01.spitfire"));
        core.setSelectionFollowsDestination(true);
        QSignalSpy scan(&core, &Core::scanFinished);
        core.setSourceDir(src);
        core.setDestinationDirectoryPath(dst);
        QVERIFY(scan.count() > 0 || scan.wait(10000));
    }

    void cleanup()
    {
        QDir(mTmp.path()).removeRecursively();
        QDir().mkpath(mTmp.path());
    }

    void clicksToggleCellsButNotLockedOnes()
    {
        Core core;
        prepare(core);
        MicMatrixModel m(&core);
        MicMatrixView view(&m);
        view.resize(980, 900);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto click = [&](const QString &mic, int col) {
            QTest::mouseClick(view.viewport(), Qt::LeftButton, {}, view.visualRect(cell(m, core, mic, col)).center());
        };
        QVERIFY(core.isMicEnabled({"tr01", "brass"}));
        click("tr01", 1);
        QVERIFY(!core.isMicEnabled({"tr01", "brass"}));
        click("tr01", 1);
        QVERIFY(core.isMicEnabled({"tr01", "brass"}));
        QVERIFY(!core.isMicEnabled({"mo01", "brass"}));
        click("mo01", 1);
        QVERIFY(core.isMicEnabled({"mo01", "brass"}));
        QCOMPARE(core.micCompleteness({"tr01", "woodwinds"}), Completeness::Partial);
        click("tr01", 4); // partly present: unticks
        QVERIFY(!core.isMicEnabled({"tr01", "woodwinds"}));
        QSignalSpy changed(&core, &Core::micEnabledChanged);
        click("mi01", 1); // always on: nothing happens
        click("mi01", 0);
        QCOMPARE(changed.count(), 0);
        QVERIFY(core.isMicEnabled({"mi01", "brass"}));
        click("mo01", 0); // the name cell toggles the row
        QVERIFY(changed.count() > 0);
    }

    void cellsAndHeadingsAreFilledByState()
    {
        Core core;
        prepare(core);
        MicMatrixModel m(&core);
        MicMatrixView view(&m);
        view.resize(980, 900);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(m.index(0, 0).isValid());
        const QImage img = view.viewport()->grab().toImage();

        const QColor wanted = fillAt(view, img, cell(m, core, "tr01", 1));
        const QColor partly = fillAt(view, img, cell(m, core, "tr01", 4));
        const QColor off = fillAt(view, img, cell(m, core, "mo01", 1));
        const QColor locked = fillAt(view, img, cell(m, core, "mi01", 1));
        QCOMPARE(core.micCompleteness({"tr01", "woodwinds"}), Completeness::Partial);
        using namespace MatrixColors; // the painted pixels are exactly the colour set's fills
        QVERIFY(distance(wanted, on) < 6);
        QVERIFY(distance(partly, MatrixColors::partly) < 6);
        QVERIFY(distance(off, MatrixColors::off) < 6);
        QVERIFY(distance(locked, MatrixColors::locked) < 6);
        QVERIFY(distance(on, MatrixColors::off) > 60 && distance(on, half) > 40 && distance(half, MatrixColors::off) > 20);
        // the mic-name cell: some sections wanted = the damped tone
        QVERIFY(distance(fillAt(view, img, cell(m, core, "tr01", 0)), half) < 6);

        // headings: two-tone by the column's state; the probe sits in the left margin, clear of the text
        QHeaderView *h = view.header();
        auto headFill = [&](int i) {
            const QImage head = h->grab().toImage();
            return pixel(head, h->sectionViewportPosition(i) + 4, h->height() / 2);
        };
        QCOMPARE(m.columnCheckState(3), Qt::Unchecked);
        QCOMPARE(m.columnCheckState(1), Qt::PartiallyChecked); // brass: TR01 on, MO01 off
        QVERIFY(distance(headFill(3), MatrixColors::off) < 6);
        QVERIFY(distance(headFill(0), MatrixColors::off) < 6); // the name and total headings are dark
        QVERIFY(distance(headFill(m.totalColumn()), MatrixColors::off) < 6);
        QVERIFY(distance(headFill(1), half) < 6);
        m.toggleColumn(3); // all wanted
        QCOMPARE(m.columnCheckState(3), Qt::Checked);
        QVERIFY(distance(headFill(3), on) < 6);
        const QImage head = h->grab().toImage();
        QVERIFY(distance(pixel(head, 0, 0), MatrixColors::base) < 15); // no stray box in the corner
    }

    void footerTextIsTheInstallationSizeWithTheRemainingSpace()
    {
        Core core;
        prepare(core);
        QVERIFY(core.totalSpaceForSelectedItems() > 0);
        QCOMPARE(installationSizeText(core.totalSpaceForSelectedItems(), 42 * 1024),
                 QString("Installation size: %1 (42.0 KB space remaining)")
                     .arg(formatByteSize(core.totalSpaceForSelectedItems())));
        // always-copied files are part of it: the matrix cells alone are smaller
        MicMatrixModel m(&core);
        size_t cells = 0;
        for (int c = 1; c <= m.sectionCount(); ++c)
            cells += m.headerData(c, Qt::Horizontal, MicMatrixModel::ColumnTotalRole).toULongLong();
        size_t always = 0; // every source file that is not a TR01/MO01 mic file (IR1 files and the databases)
        for (const auto &f : scanTree(mTmp.filePath("src")))
            if (!f.path.contains("_TR01_") && !f.path.contains("_MO01_"))
                always += f.size;
        QVERIFY(always > 0);
        QCOMPARE(core.totalSpaceForSelectedItems(), cells + always);
    }

    // The footer's right edge (a label with the view's inset as right margin, like MainWindow) against the
    // right-most text pixel of a Row total cell.
    void footerLinesUpUnderTheRowTotalColumn()
    {
        Core core;
        prepare(core);
        MicMatrixModel m(&core);
        for (int height : {300, 1500}) { // with and without the vertical scrollbar
            QWidget box;
            auto *col = new QVBoxLayout(&box);
            col->setContentsMargins(0, 0, 0, 0);
            col->setSpacing(0);
            auto *view = new MicMatrixView(&m);
            col->addWidget(view);
            auto *row = new QHBoxLayout;
            auto *label = new QLabel;
            QPalette pal;
            pal.setColor(QPalette::Window, Qt::black);
            pal.setColor(QPalette::WindowText, Qt::white);
            label->setPalette(pal);
            label->setAutoFillBackground(true);
            label->setAlignment(Qt::AlignRight);
            row->addWidget(label, 1);
            col->addLayout(row);
            auto align = [&] { row->setContentsMargins(0, 0, view->totalTextRightInset(), 0); };
            connect(view, &MicMatrixView::totalTextRightInsetChanged, this, align);
            align();
            box.resize(980, height);
            box.show();
            QVERIFY(QTest::qWaitForWindowExposed(&box));
            QTest::qWait(100);
            QCOMPARE(view->verticalScrollBar()->isVisible(), height == 300);
            const QModelIndex total = cell(m, core, "tr01", m.totalColumn());
            label->setText(total.data().toString());
            label->setFont(view->font());
            QTest::qWait(50);
            auto rightMost = [](const QImage &img, const QRect &r) { // logical x of the right-most light pixel
                int best = -1;
                const qreal d = img.devicePixelRatio();
                for (int y = int(r.top() * d); y <= int(r.bottom() * d); ++y)
                    for (int x = int(r.left() * d); x <= int(r.right() * d); ++x)
                        if (luma(img.pixelColor(x, y)) > 0.5)
                            best = qMax(best, int(x / d));
                return best;
            };
            const QImage vimg = view->viewport()->grab().toImage();
            const QRect cr = view->visualRect(total);
            const int cellRight = rightMost(vimg, cr) + view->viewport()->mapTo(&box, QPoint(0, 0)).x();
            const QImage limg = label->grab().toImage();
            const int labelRight = rightMost(limg, QRect(QPoint(0, 0), label->size())) + label->mapTo(&box, QPoint(0, 0)).x();
            QVERIFY2(qAbs(cellRight - labelRight) <= 2,
                     qPrintable(QString("height %1: cell %2 footer %3").arg(height).arg(cellRight).arg(labelRight)));
        }
    }

    void currentCellOutlineShowsOnEveryTile()
    {
        using namespace MatrixColors;
        for (const QColor &fill : tileFills())
            QVERIFY2(qMax(contrast(fill, ringOuter), contrast(fill, ringInner)) >= 3.0, qPrintable(fill.name()));
    }

    void themeForcesDarkWithThePaletteColours()
    {
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light); // as if the system were light
        applyDarkTheme(*qobject_cast<QApplication *>(QCoreApplication::instance()));
        if (QGuiApplication::platformName() != QLatin1String("offscreen")) // that platform ignores the override
            QCOMPARE(QGuiApplication::styleHints()->colorScheme(), Qt::ColorScheme::Dark);
        QCOMPARE(qApp->style()->name(), QStringLiteral("fusion"));
        QCOMPARE(QGuiApplication::palette().color(QPalette::Window), QColor("#232323"));
        QCOMPARE(QGuiApplication::palette().color(QPalette::Highlight), QColor("#5990c0"));
    }

    void legendSwatchesAreTheMatrixColours()
    {
        const auto &l = MatrixColors::legend();
        QCOMPARE(l.size(), 5);
        QCOMPARE(l[0].first, MatrixColors::on);
        QCOMPARE(l[1].first, MatrixColors::half);
        QCOMPARE(l[2].first, MatrixColors::partly);
        QCOMPARE(l[3].first, MatrixColors::locked);
        QCOMPARE(l[4].first, MatrixColors::off);
        QVERIFY(l[2].second.contains("Apply completes it"));
    }

    void colourPairsMeetWcagContrast()
    {
        for (const auto &p : MatrixColors::pairs())
            QVERIFY2(MatrixColors::contrast(p.first, p.second) >= 4.5,
                     qPrintable(p.first.name() + " / " + p.second.name()));
        QVERIFY(MatrixColors::contrast(MatrixColors::off, MatrixColors::offText) >= 7);
        QVERIFY(MatrixColors::contrast(Palette::background, Palette::error) >= 4.5); // "Needs Z more space"
        QVERIFY(MatrixColors::contrast(Palette::background, Palette::success) >= 4.5); // plan result
        QVERIFY(MatrixColors::contrast(Palette::background, Palette::amber) >= 4.5); // "Select a microphone"
        QVERIFY(MatrixColors::contrast(Palette::background, Palette::errorMuted) >= 4.5); // refusals
    }
};

QTEST_MAIN(TstMicMatrixView)
#include "tst_micmatrixview.moc"
