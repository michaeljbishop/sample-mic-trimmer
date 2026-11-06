#include <QtTest>
#include <QDir>
#include <QTemporaryDir>
#include "analysis.h"
#include "profileregistry.h"
#include "testlib.h"

using namespace smt;

class TstAnalysis : public QObject
{
    Q_OBJECT
    QTemporaryDir dir;

    static QList<FileInfo> scan(const QString &root, const QList<QPair<QString, qint64>> &files)
    {
        QList<FileInfo> out;
        for (const auto &f : files) {
            QDir().mkpath(QFileInfo(root + "/" + f.first).absolutePath());
            if (!f.first.endsWith(".db")) writeFile(root + "/" + f.first, f.second); // DBs are real, made by makeDb
            FileInfo fi;
            fi.path = f.first;
            fi.size = size_t(f.second);
            out << fi;
        }
        return out;
    }
    static const MicSummary *find(const Analysis &a, const QString &token, const QString &group)
    {
        for (const auto &m : a.mics)
            if (m.token == token && m.group == group) return &m;
        return nullptr;
    }
    // Makes root/Samples/_Brass with one DB listing the containers; returns the scan (db included).
    QList<FileInfo> brass(const QString &root, const QStringList &containers, const QList<QPair<QString, qint64>> &extra = {})
    {
        QDir().mkpath(root + "/Samples/_Brass");
        makeDb(root + "/Samples/_Brass/B.db", containers);
        QList<QPair<QString, qint64>> files = extra;
        files.prepend({"Samples/_Brass/B.db", QFileInfo(root + "/Samples/_Brass/B.db").size()});
        return scan(root, files);
    }

private slots:
    void listsEveryMicInDb()
    {
        QTemporaryDir d;
        const auto s = brass(d.path(), {"A_TR01.spitfire", "A_TR01_02.spitfire", "A_AF01.spitfire", "BBCSO_MV1_IR1.spitfire"});
        const Analysis a = analyzeLibrary(builtInProfile(kBbcsoProfileId), d.path(), s);
        QCOMPARE(a.mics.size(), 2);
        QCOMPARE(a.dbsFound, QStringList{"Samples/_Brass/B.db"});
        const auto *tr = find(a, "TR01", "brass");
        QVERIFY(tr);
        QVERIFY(tr->known);
        QCOMPARE(tr->fileCount, 2);
        QCOMPARE(tr->name, builtInProfile(kBbcsoProfileId).mic("tr01")->name);
        QVERIFY(find(a, "AF01", "brass"));
    }
    void unionAcrossTwoDbsInOneSection()
    {
        QTemporaryDir d;
        QDir().mkpath(d.path() + "/Samples/_Woodwinds");
        makeDb(d.path() + "/Samples/_Woodwinds/W1.db", {"A_TR01.spitfire"});
        makeDb(d.path() + "/Samples/_Woodwinds/W2.db", {"B_TR01.spitfire", "B_CL01.spitfire"});
        const Analysis a = analyzeLibrary(builtInProfile(kBbcsoProfileId), d.path(), {{"Samples/_Woodwinds/W1.db", 1}, {"Samples/_Woodwinds/W2.db", 1}});
        QCOMPARE(a.dbsFound.size(), 2);
        QCOMPARE(find(a, "TR01", "woodwinds")->fileCount, 2);
        QVERIFY(find(a, "CL01", "woodwinds"));
    }
    void unknownTokenReportedWithRawToken()
    {
        QTemporaryDir d;
        const Analysis a = analyzeLibrary(builtInProfile(kBbcsoProfileId), d.path(), brass(d.path(), {"A_ZZ01.spitfire", "B_ZZ01_01.spitfire", "A_TR01.spitfire"}));
        const auto *z = find(a, "ZZ01", "brass");
        QVERIFY(z);
        QVERIFY(!z->known);
        QCOMPARE(z->name, "ZZ01");
        QCOMPARE(a.unknownTokens, QStringList{"ZZ01"});
    }
    void sizesComeFromScan()
    {
        QTemporaryDir d;
        const Analysis a = analyzeLibrary(builtInProfile(kBbcsoProfileId), d.path(), brass(d.path(), {"A_TR01.spitfire", "B_TR01.spitfire", "A_AF01.spitfire"},
                                                          {{"Samples/_Brass/A_TR01.spitfire", 100}, {"Samples/_Brass/B_TR01.spitfire", 50},
                                                           {"Samples/_Brass/A_AF01.spitfire", 7}}));
        QCOMPARE(find(a, "TR01", "brass")->bytes, 150);
        QCOMPARE(find(a, "AF01", "brass")->bytes, 7);
        QVERIFY(a.filesMissingFromDisk.isEmpty());
    }
    void missingFilesReported()
    {
        QTemporaryDir d;
        const Analysis a = analyzeLibrary(builtInProfile(kBbcsoProfileId), d.path(), brass(d.path(), {"B_TR01.spitfire", "A_TR01.spitfire"}, {{"Samples/_Brass/A_TR01.spitfire", 10}}));
        QCOMPARE(a.filesMissingFromDisk, QStringList{"Samples/_Brass/B_TR01.spitfire"});
        const auto *tr = find(a, "TR01", "brass");
        QCOMPARE(tr->fileCount, 2);
        QCOMPARE(tr->bytes, 10);
    }
    void unreadableDbWarnsAndContinues()
    {
        QTemporaryDir d;
        auto s = brass(d.path(), {"A_TR01.spitfire"});
        QDir().mkpath(d.path() + "/Samples/_Strings");
        writeFile(d.path() + "/Samples/_Strings/bad.db", 20); // not a sqlite file
        s << FileInfo{"Samples/_Strings/bad.db", 20, {}, {}, {}, {}};
        s << FileInfo{"Samples/_Strings/gone.db", 0, {}, {}, {}, {}}; // listed but absent
        const Analysis a = analyzeLibrary(builtInProfile(kBbcsoProfileId), d.path(), s);
        QCOMPARE(a.warnings.size(), 2);
        QVERIFY(a.warnings.at(0).contains("bad.db") || a.warnings.at(1).contains("bad.db"));
        QVERIFY(find(a, "TR01", "brass"));
        QCOMPARE(a.dbsFound.size(), 3);
    }
    void toTextMentionsEveryMicAndUnknownTokens()
    {
        QTemporaryDir d;
        const Analysis a = analyzeLibrary(builtInProfile(kBbcsoProfileId), d.path(), brass(d.path(), {"A_TR01.spitfire", "A_ZZ01.spitfire", "B_AF01.spitfire"},
                                                          {{"Samples/_Brass/A_TR01.spitfire", 2048}}));
        const QString t = a.toText();
        QVERIFY(t.contains(builtInProfile(kBbcsoProfileId).mic("tr01")->name));
        QVERIFY(t.contains(builtInProfile(kBbcsoProfileId).mic("af01")->name));
        QVERIFY(t.contains("ZZ01"));
        QVERIFY(t.contains("2.0 KB"));
        QVERIFY(t.contains("Samples/_Brass/B_AF01.spitfire")); // missing
    }
    void unknownCodesAreReported()
    {
        QTemporaryDir d;
        QDir().mkpath(d.path() + "/Samples");
        makeDb(d.path() + "/Samples/Tutti.db", {"ERICW_TUTTI_A_RR1_CL06_01.spitfire", "ERICW_TUTTI_B_RR1_CL06_01.spitfire",
                                                "ERICW_TUTTI_C_RR1_CL06_02.spitfire", "ERICW_TUTTI_A_RR1_CL01_01.spitfire",
                                                "ERICW_VERB_IR.spitfire"});
        const auto s = scan(d.path(), {{"Samples/Tutti.db", 1}, {"Samples/ERICW_TUTTI_A_RR1_CL01_01.spitfire", 5}});
        const Analysis a = analyzeLibrary(builtInProfile(kEwcProfileId), d.path(), s);
        QCOMPARE(a.unknownTokens, QStringList{"CL06"});
        const auto *cl = find(a, "CL01", "tutti");
        QVERIFY(cl);
        QVERIFY(cl->known);
        QCOMPARE(cl->mic, "cl01");
        QCOMPARE(cl->fileCount, 1);
        QCOMPARE(cl->bytes, 5);
        const auto *unknown = find(a, "CL06", "tutti");
        QVERIFY(unknown);
        QVERIFY(!unknown->known);
        QCOMPARE(unknown->fileCount, 3);
        const QString t = a.toText();
        QVERIFY2(t.contains("Tutti / Close (CL01): 1 files"), qPrintable(t));
        QVERIFY2(t.contains("Unknown mic codes (kept, never removed): CL06 (3 files)"), qPrintable(t));
    }
    void unlistedWhitacreCodesAreUnknownAndKept()
    {
        QTemporaryDir d;
        QDir().mkpath(d.path() + "/Samples");
        makeDb(d.path() + "/Samples/Tutti.db", {"ERICW_TUTTI_A_RR1_CL05_01.spitfire", "ERICW_TUTTI_A_RR1_ST04_01.spitfire"});
        const Analysis a = analyzeLibrary(builtInProfile(kEwcProfileId), d.path(), scan(d.path(), {{"Samples/Tutti.db", 1}}));
        QCOMPARE(a.unknownTokens, (QStringList{"CL05", "ST04"}));
        QVERIFY2(a.toText().contains("Unknown mic codes (kept, never removed): CL05 (1 files), ST04 (1 files)"), qPrintable(a.toText()));
    }
    void filesMatchingNoMicOrGroupAreReported()
    {
        QTemporaryDir d;
        QDir().mkpath(d.path() + "/Samples");
        makeDb(d.path() + "/Samples/Tutti.db", {"ERICW_CHOIR_X_RR1_CL05_01.spitfire", "ERICW_VERB_IR.spitfire",
                                                "ERICW_TUTTI_A_RR1_CL01_01.spitfire"});
        const Analysis a = analyzeLibrary(builtInProfile(kEwcProfileId), d.path(), scan(d.path(), {{"Samples/Tutti.db", 1}}));
        QCOMPARE(a.noMicFiles, 2);
        QCOMPARE(a.noMicExamples, (QStringList{"ERICW_CHOIR_X_RR1_CL05_01.spitfire", "ERICW_VERB_IR.spitfire"}));
        const QString t = a.toText();
        QVERIFY2(t.contains("2 sample files match no mic/group of this profile (kept): e.g. "
                            "ERICW_CHOIR_X_RR1_CL05_01.spitfire, ERICW_VERB_IR.spitfire"), qPrintable(t));
    }
};

QTEST_MAIN(TstAnalysis)
#include "tst_analysis.moc"
