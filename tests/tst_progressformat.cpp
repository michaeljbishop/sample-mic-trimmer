#include <QtTest>
#include "coreutil.h"

static const qint64 kMB = 1024 * 1024;
static const qint64 kGB = 1024 * kMB;

class TstProgressFormat : public QObject
{
    Q_OBJECT
private slots:
    void typical()
    {
        QCOMPARE(smt::formatProgressLine(1300000000, 4 * kGB, 85.0 * kMB),
                 QString("1.2 GB / 4.0 GB (30%)  85.0 MB/s  ETA 0:33"));
    }
    void zeroTotalHasNoDivideByZero()
    {
        QCOMPARE(smt::formatProgressLine(0, 0, 0), QString("0 bytes / 0 bytes (100%)  0 bytes/s  ETA --"));
    }
    void unknownRate()
    {
        QCOMPARE(smt::formatProgressLine(0, kGB, 0), QString("0 bytes / 1.0 GB (0%)  0 bytes/s  ETA --"));
    }
    void installationSizeVariants()
    {
        QCOMPARE(installationSizeText(5 * kGB, 100 * kGB), QString("Installation size: 5.0 GB (100.0 GB space remaining)"));
        QCOMPARE(installationSizeText(5 * kGB, -kGB), QString("Installation size: 5.0 GB (Needs 1.0 GB more space)"));
        QCOMPARE(installationSizeText(5 * kGB, std::nullopt), QString("Installation size: 5.0 GB"));
        QCOMPARE(installationSizeText(5 * kGB, 0), QString("Installation size: 5.0 GB (0 bytes space remaining)")); // exactly enough
        QCOMPARE(installationSizeText(5 * kGB, -1), QString("Installation size: 5.0 GB (Needs 1 bytes more space)"));
    }
    void footerStateResetsWhenTheDestinationIsCleared()
    {
        InstallationFooter f;
        QVERIFY(!f.needsMoreSpace());
        f.remaining = -kGB;
        QVERIFY(f.needsMoreSpace());
        QCOMPARE(f.text(kGB), QString("Installation size: 1.0 GB (Needs 1.0 GB more space)"));
        f.remaining.reset(); // what MainWindow does when the destination path becomes empty
        QVERIFY(!f.needsMoreSpace());
        QCOMPARE(f.text(kGB), QString("Installation size: 1.0 GB"));
    }
    void applyButtonTextAndEnabledFollowThePlan()
    {
        QCOMPARE(applyButtonText({}), QString("Apply")); // unknown (a scan runs)
        QVERIFY(!applyEnabled({})); // while a scan runs Apply waits
        PendingChange none; // no step at all
        none.known = true;
        QCOMPARE(applyButtonText(none), QString("No changes"));
        QVERIFY(!applyEnabled(none));
        PendingChange dbOnly; // only database rebuilds: resumes an interrupted apply
        dbOnly.known = dbOnly.hasSteps = true;
        QCOMPARE(applyButtonText(dbOnly), QString("Apply (update databases)"));
        QVERIFY(applyEnabled(dbOnly));
        PendingChange some;
        some.known = some.any = some.hasSteps = true;
        some.add = 3 * kGB;
        some.remove = 300 * kMB;
        QCOMPARE(applyButtonText(some), QString("Apply (+3.0 GB / \u2212300.0 MB)"));
        QVERIFY(applyEnabled(some));
        some.refused = true; // e.g. not enough space: the steps exist but nothing can run
        some.firstRefusal = "not enough space";
        QCOMPARE(applyButtonText(some), QString("Apply (+3.0 GB / \u2212300.0 MB)"));
        QVERIFY(!applyEnabled(some));
        PendingChange refusedNoSteps; // overlap, name mismatch, no destination, ...
        refusedNoSteps.known = refusedNoSteps.refused = true;
        QCOMPARE(applyButtonText(refusedNoSteps), QString("Apply"));
        QVERIFY(!applyEnabled(refusedNoSteps));
    }
    void scanStatusTextFormatting()
    {
        QCOMPARE(scanStatusText({}), QString());
        ScanStatus s;
        s.source = true;
        QCOMPARE(scanStatusText(s), QString("Scanning Source\u2026"));
        s.sourceFiles = 12345;
        QCOMPARE(scanStatusText(s), QString("Scanning Source\u2026 12,345 files")); // no total known
        s.sourceExpected = 50000;
        QCOMPARE(scanStatusText(s), QString("Scanning Source\u2026 24%"));
        s.sourceFiles = 50000;
        QCOMPARE(scanStatusText(s), QString("Scanning Source\u2026 99%")); // never 100 before it finished
        s.dest = true;
        QCOMPARE(scanStatusText(s), QString("Scanning Source\u2026 99% \u00b7 Reduced Mic Installation\u2026"));
        s.source = false;
        QCOMPARE(scanStatusText(s), QString("Scanning Reduced Mic Installation\u2026"));
    }
    void windowTitleNamesTheProfile()
    {
        QCOMPARE(mainWindowTitle("App", "Eric Whitacre Choir"), QString("App \u2014 Eric Whitacre Choir"));
        QCOMPARE(mainWindowTitle("App", {}), QString("App"));
    }
    void etaOverAnHour()
    {
        QCOMPARE(smt::formatProgressLine(0, 3725 * kMB, double(kMB)),
                 QString("0 bytes / 3.6 GB (0%)  1.0 MB/s  ETA 1:02:05"));
    }
};

QTEST_GUILESS_MAIN(TstProgressFormat)
#include "tst_progressformat.moc"
