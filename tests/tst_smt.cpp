#include <QtTest>
#include "smt.h"
#include "profile.h"
#include "profileregistry.h"

using namespace smt;

class TstSmt : public QObject
{
    Q_OBJECT
private slots:
    void keysWithSamePositionDifferentSectionAreNotEqual()
    {
        const MicSignalGroupKey a{"tr01", "brass"};
        const MicSignalGroupKey b{"tr01", "strings"};
        QVERIFY(!(a == b));
        QVERIFY(a == a);
    }
    void classifyMicSuffix()
    {
        const auto bbcso = builtInProfile(kBbcsoProfileId);
        auto c = classify(bbcso, "Samples/_Brass/X_AF01_01.spitfire");
        QCOMPARE(c.kind, FileKind::Mic);
        QCOMPARE(c.key, (MicSignalGroupKey{"af01", "brass"}));
        QCOMPARE(c.micToken, "AF01");
        c = classify(bbcso, "Samples/_Brass/X_TR01.spitfire");
        QCOMPARE(c.kind, FileKind::Mic);
        QCOMPARE(c.key.mic, "tr01");
        QCOMPARE(classify(bbcso, "Samples/_Brass/x_tr01.spitfire").kind, FileKind::Mic);
    }
    void classifyIgnoresTokenInsideName()
    {
        const auto bbcso = builtInProfile(kBbcsoProfileId);
        auto c = classify(bbcso, "Samples/_Brass/X_MI01_Y_TR01.spitfire");
        QCOMPARE(c.kind, FileKind::Mic);
        QCOMPARE(c.key.mic, "tr01");
        QCOMPARE(classify(bbcso, "Samples/_Brass/BBCSO_MV1_IR1.spitfire").kind, FileKind::Other);
    }
    void classifyUnknownTokenIsOther()
    {
        const auto bbcso = builtInProfile(kBbcsoProfileId);
        auto c = classify(bbcso, "Samples/_Brass/X_ZZ01.spitfire");
        QCOMPARE(c.kind, FileKind::Other);
        QCOMPARE(c.micToken, "ZZ01");
        QCOMPARE(classify(bbcso, "Samples/_Misc/X_TR01.spitfire").kind, FileKind::Other);
    }
    void classifyDatabase()
    {
        const auto bbcso = builtInProfile(kBbcsoProfileId);
        QCOMPARE(classify(bbcso, "Samples/_Brass/BBCSO_BRASS_V020.db").kind, FileKind::Database);
        QCOMPARE(classify(bbcso, "samples/_Brass/X.DB").kind, FileKind::Database);
    }
    void nonSpitfireFileIsNeverAMic()
    {
        auto p = builtInProfile(kBbcsoProfileId);
        p.sampleFile = QRegularExpression("_(?<mic>[A-Z]{2}\\d{2})");
        QCOMPARE(classify(p, "Samples/_Brass/X_TR01.spitfire").kind, FileKind::Mic);
        const auto c = classify(p, "Samples/_Brass/notes_CL01.txt");
        QCOMPARE(c.kind, FileKind::Other);
        QVERIFY(c.micToken.isEmpty());
    }
    void dbOutsideSamplesIsOther()
    {
        const auto bbcso = builtInProfile(kBbcsoProfileId);
        QCOMPARE(classify(bbcso, "Instruments/cache.db").kind, FileKind::Other);
        QCOMPARE(classify(bbcso, "top.db").kind, FileKind::Other);
        QCOMPARE(classify(bbcso, "Patches/Samples/_Brass/X.db").kind, FileKind::Other);
    }

    void bbcsoClassifiesLikeToday()
    {
        const auto p = builtInProfile(kBbcsoProfileId);
        auto c = classify(p, "Samples/_Brass/BBCSO_X_RR1_TR01.spitfire");
        QCOMPARE(c.kind, FileKind::Mic);
        QCOMPARE(c.key.mic, "tr01");
        QCOMPARE(c.key.group, "brass");
        QCOMPARE(c.micToken, "TR01");
        c = classify(p, "Samples/_Strings/X_MI01_01.spitfire");
        QCOMPARE(c.kind, FileKind::Mic);
        QCOMPARE(c.key.mic, "mi01");
        QCOMPARE(c.key.group, "strings");
        c = classify(p, "Samples/_Misc/X_TR01.spitfire");
        QCOMPARE(c.kind, FileKind::Other);
        QCOMPARE(c.micToken, "TR01");
        QCOMPARE(classify(p, "Samples/_Brass/X_ZZ01.spitfire").kind, FileKind::Other);
        QCOMPARE(classify(p, "Samples/_Brass/X_ZZ01.spitfire").micToken, "ZZ01");
        QCOMPARE(classify(p, "Samples/_brass/X_TR01.spitfire").kind, FileKind::Other); // group dirs match exactly
    }
    void whitacreClassifies()
    {
        const auto p = builtInProfile(kEwcProfileId);
        auto c = classify(p, "Samples/ERICW_ALTOS_LEG-INTAAH_RR1_AM01_01_V3.spitfire");
        QCOMPARE(c.kind, FileKind::Mic);
        QCOMPARE(c.key.mic, "am01");
        QCOMPARE(c.key.group, "altos");
        QCOMPARE(classify(p, "Samples/ERICW_VERB_IR.spitfire").kind, FileKind::Other);
        QVERIFY(classify(p, "Samples/ERICW_VERB_IR.spitfire").micToken.isEmpty());
        c = classify(p, "Samples/ERICW_TUTTI_X_RR1_CL06_01.spitfire");
        QCOMPARE(c.kind, FileKind::Other);
        QCOMPARE(c.micToken, "CL06");
        c = classify(p, "Samples/ERICW_TUTTI_X_RR1_CL05_01.spitfire"); // not offered: always copied
        QCOMPARE(c.kind, FileKind::Other);
        QCOMPARE(c.micToken, "CL05");
        QCOMPARE(classify(p, "Samples/ERICW_CHOIR_X_RR1_CL05_01.spitfire").kind, FileKind::Other);
    }
    void databasesAreDatabases()
    {
        const auto p = builtInProfile(kEwcProfileId);
        QCOMPARE(classify(p, "Samples/Altos.db").kind, FileKind::Database);
        QVERIFY(isDatabasePath("Samples/_Brass/x.DB"));
        QVERIFY(!isDatabasePath("Other/x.db"));
        QCOMPARE(classify(p, "Other/x.db").kind, FileKind::Other);
    }
    void filesOutsideSamplesAreOther()
    {
        const auto p = builtInProfile(kEwcProfileId);
        const auto c = classify(p, "Other/ERICW_ALTOS_X_AM01_01.spitfire");
        QCOMPARE(c.kind, FileKind::Other);
        QVERIFY(c.micToken.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TstSmt)
#include "tst_smt.moc"
