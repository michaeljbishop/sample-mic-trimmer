#include <QtTest>
#include "completeness.h"
#include "profileregistry.h"

using namespace smt;

static const QString kA = QStringLiteral("Samples/_Brass/B1_TR01_01.spitfire");
static const QString kB = QStringLiteral("Samples/_Brass/B2_TR01_01.spitfire");
static const QString kOther = QStringLiteral("Samples/_Brass/B1_MO01_01.spitfire");
static const MicSignalGroupKey kTrBrass{"tr01", "brass"};
using C = Completeness;

static FileInfo fi(const QString &path, size_t size)
{
    FileInfo f{};
    f.path = path;
    f.size = size;
    return f;
}

class TstCompleteness : public QObject
{
    Q_OBJECT
    const ProductProfile p = builtInProfile(kBbcsoProfileId);

    static C state(const MicCompleteness &m, const MicSignalGroupKey &k)
    {
        return m.value(k.mic).value(k.group, C(-1)); // a missing key reads as an invalid value
    }

private slots:
    void emptyDestAllEmpty()
    {
        const MicCompleteness m = completeMics(p, {fi(kA, 10), fi(kOther, 5)}, {});
        int n = 0;
        forAllMicSignalGroupKeys(p, [&](const MicSignalGroupKey &k) { ++n; QVERIFY(state(m, k) == C::Empty); });
        QVERIFY(n > 0);
    }
    void allFilesPresentIsComplete()
    {
        const MicCompleteness m = completeMics(p, {fi(kA, 10), fi(kB, 20), fi(kOther, 5)}, {fi(kA, 10), fi(kB, 20)});
        QVERIFY(state(m, kTrBrass) == C::Complete);
        QVERIFY(state(m, {"mo01", "brass"}) == C::Empty);
    }
    void missingFileIsPartial()
    {
        QVERIFY(state(completeMics(p, {fi(kA, 10), fi(kB, 20)}, {fi(kA, 10)}), kTrBrass) == C::Partial);
    }
    void wrongSizeIsPartial()
    {
        QVERIFY(state(completeMics(p, {fi(kA, 10), fi(kB, 20)}, {fi(kA, 10), fi(kB, 19)}), kTrBrass) == C::Partial);
        QVERIFY(state(completeMics(p, {fi(kA, 10)}, {fi(kA, 11)}), kTrBrass) == C::Partial);
    }
    void unrelatedDestFilesDontMatter()
    {
        const MicCompleteness m = completeMics(p, {fi(kA, 10)}, {fi(kA, 10), fi(kOther, 5), fi("Samples/_Brass/x.txt", 1)});
        QVERIFY(state(m, kTrBrass) == C::Complete);
        QVERIFY(state(m, {"mo01", "brass"}) == C::Empty); // no source files: nothing to be complete
    }
    void emptySourceFallsBackToDest()
    {
        QVERIFY(state(completeMics(p, {}, {fi(kA, 10)}), kTrBrass) == C::Complete);
        QVERIFY(state(completeMics(p, {}, {}), kTrBrass) == C::Empty);
    }
    void toolArtifactsAreIgnored()
    {
        const QList<FileInfo> junk{fi("project.smt", 3), fi(".smt.lock", 1), fi(".DS_Store", 1),
                                   fi(".smt-staging/" + kA, 10), fi(kA + ".part", 10)};
        QVERIFY(state(completeMics(p, {}, junk), kTrBrass) == C::Empty);
        QVERIFY(state(completeMics(p, {fi(kA, 10)}, junk), kTrBrass) == C::Empty);
    }
    void whitacreGroupFromFileName()
    {
        const ProductProfile w = builtInProfile(kEwcProfileId);
        const QString f = "Samples/ERICW_ALTOS_LEG-INTAAH_RR1_AM01_01_V3.spitfire";
        const MicCompleteness m = completeMics(w, {fi(f, 7)}, {fi(f, 7)});
        QVERIFY(state(m, {"am01", "altos"}) == C::Complete);
        QVERIFY(state(m, {"am01", "tutti"}) == C::Empty);
    }
};

QTEST_APPLESS_MAIN(TstCompleteness)
#include "tst_completeness.moc"
