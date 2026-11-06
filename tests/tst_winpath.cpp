#include <QtTest>
#include "winpath.h"

using namespace smt::winpath;
using R = Relation;

// Windows-shaped path strings, checked on any platform (the helpers are pure). UNVERIFIED against real Windows.
class TstWinPath : public QObject
{
    Q_OBJECT
private slots:
    void normalizeReadsEveryWindowsSpelling()
    {
        QCOMPARE(normalize("C:\\x\\y"), QString("C:/x/y"));
        QCOMPARE(normalize("C:/x/y/"), QString("C:/x/y"));
        QCOMPARE(normalize("C:\\\\x\\\\\\y"), QString("C:/x/y")); // repeated separators
        QCOMPARE(normalize("C:/x/./y"), QString("C:/x/y"));
        QCOMPARE(normalize("c:\\X"), QString("c:/X")); // case is kept, key() folds it
        QCOMPARE(normalize("C:"), QString("C:/"));
        QCOMPARE(normalize("C:\\"), QString("C:/"));
        QCOMPARE(normalize("\\\\?\\C:\\x\\y"), QString("C:/x/y")); // the extended-length prefix
        QCOMPARE(normalize("//?/C:/x"), QString("C:/x"));
        QCOMPARE(normalize("\\\\.\\C:\\x"), QString("C:/x"));
        QCOMPARE(normalize("//server/share/x"), QString("//server/share/x")); // UNC
        QCOMPARE(normalize("\\\\server\\share\\x\\"), QString("//server/share/x"));
        QCOMPARE(normalize("\\\\?\\UNC\\server\\share\\x"), QString("//server/share/x"));
        QCOMPARE(normalize("\\\\?\\unc\\server\\share"), QString("//server/share"));
        QCOMPARE(normalize("/"), QString("/"));
        QCOMPARE(normalize("/a/b/"), QString("/a/b"));
        QCOMPARE(normalize("C:/x/../y"), QString("C:/x/../y")); // ".." is kept, never resolved here
    }
    void keyIgnoresCaseAndSeparators()
    {
        QCOMPARE(key("C:\\Spitfire Audio\\Lib"), key("c:/SPITFIRE AUDIO/lib/"));
        QVERIFY(key("//Server/Share/Lib") != key("//Server/Share/Lib2"));
    }
    void rootsAndLeafNames()
    {
        for (const char *root : {"C:/", "C:\\", "C:", "d:/", "/", "//server/share", "\\\\server\\share\\", "\\\\?\\C:\\",
                                 "\\\\?\\UNC\\server\\share"})
            QVERIFY2(isRoot(root), root);
        for (const char *notRoot : {"C:/x", "//server/share/x", "/x", "C:\\x\\"})
            QVERIFY2(!isRoot(notRoot), notRoot);
        QCOMPARE(leafName("C:\\Music\\Spitfire Audio - X\\"), QString("Spitfire Audio - X"));
        QCOMPARE(leafName("//server/share/Spitfire Audio - X"), QString("Spitfire Audio - X"));
        QCOMPARE(leafName("\\\\?\\UNC\\server\\share\\Lib"), QString("Lib"));
        QCOMPARE(leafName("C:/"), QString());
        QCOMPARE(leafName("//server/share"), QString());
    }
    void joinDoesNotDoubleTheSlashOfARoot()
    {
        QCOMPARE(join("C:/", "x"), QString("C:/x"));
        QCOMPARE(join("C:\\", "x"), QString("C:/x"));
        QCOMPARE(join("C:\\a\\b\\", "x"), QString("C:/a/b/x"));
        QCOMPARE(join("//server/share", "x"), QString("//server/share/x"));
        QCOMPARE(join("/", "x"), QString("/x"));
    }
    void dotDotIsFoundWithEitherSeparator()
    {
        QVERIFY(hasDotDotSegment("C:/a/../b", false));
        QVERIFY(hasDotDotSegment("C:\\a\\..\\b", true));
        QVERIFY(!hasDotDotSegment("C:\\a\\..\\b", false)); // the Unix flavour: a backslash is a name character
        QVERIFY(!hasDotDotSegment("C:/a/..b/c...", true)); // not a ".." segment
        QVERIFY(hasDotDotSegment("..", true));
    }
    void relateComparesCaseInsensitivelyWithEitherSeparator()
    {
        QCOMPARE(relate("C:\\Lib", "c:/lib/"), R::Same);
        QCOMPARE(relate("C:/Lib/Sub/Deep", "C:/lib"), R::AInsideB);
        QCOMPARE(relate("C:/lib", "C:\\LIB\\sub"), R::BInsideA);
        QCOMPARE(relate("C:/Lib2", "C:/Lib"), R::Unrelated); // a prefix of the name is not a parent
        QCOMPARE(relate("D:/Lib", "C:/Lib"), R::Unrelated);
        QCOMPARE(relate("C:/Lib", "C:/"), R::AInsideB); // everything is inside a drive root
        QCOMPARE(relate("//Server/Share/Lib", "\\\\?\\UNC\\server\\SHARE\\lib"), R::Same);
        QCOMPARE(relate("//server/share/Lib/x", "//server/share"), R::AInsideB);
        QCOMPARE(relate("//server/share2/Lib", "//server/share/Lib"), R::Unrelated);
    }
    void pathListsCompareByKey()
    {
        QVERIFY(samePathLists({"C:\\Lib\\Samples"}, {"c:/lib/samples"}));
        QVERIFY(!samePathLists({"C:\\Lib\\Samples"}, {"C:/Lib/Presets"}));
        QVERIFY(!samePathLists({"C:/a"}, {"C:/a", "C:/b"}));
        QVERIFY(!samePathLists({"C:/a", "C:/b"}, {"C:/b", "C:/a"})); // order matters
        QVERIFY(samePathLists({}, {}));
    }
    void toNativeUsesBackslashes()
    {
        QCOMPARE(toNative("C:/a/b"), QString("C:\\a\\b"));
        QCOMPARE(toNative("//server/share/x"), QString("\\\\server\\share\\x"));
    }
};

QTEST_APPLESS_MAIN(TstWinPath)
#include "tst_winpath.moc"
