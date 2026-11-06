#include <QtTest>
#include <memory>
#include "profile.h"
#include "profileregistry.h"

using namespace smt;

namespace {

using Parts = QMap<QString, QByteArray>;

QByteArray plist(const QMap<QString, QString> &kv)
{
    QByteArray x = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\"><dict>\n";
    for (auto it = kv.begin(); it != kv.end(); ++it)
        x += "<key>" + it.key().toUtf8() + "</key><string>" + it.value().toUtf8() + "</string>\n";
    return x + "</dict></plist>\n";
}

Parts validParts()
{
    Parts p;
    p["Contents/Info.plist"] = plist({{"CFBundleIdentifier", "com.example.test"},
                                      {"CFBundleName", "Test Profile"},
                                      {"CFBundleShortVersionString", "1.2"},
                                      {"SMTProfileFormat", "1"},
                                      {"SMTProfileLibrary", "library.json"},
                                      {"SMTProfileMics", "mics.json"},
                                      {"SMTProfileGroups", "groups.json"},
                                      {"SMTProfileImages", "images.json"},
                                      {"SMTProfileNotes", "notes.md"}});
    p["Contents/Resources/library.json"] =
        "{\"folderPattern\":\"^Test.*\",\"samplesDir\":\"Samples\",\"sampleFile\":\"^(?<group>.+)_(?<mic>[A-Z]+\\\\d+)\\\\.spitfire$\"}";
    p["Contents/Resources/mics.json"] =
        "{\"mics\":[{\"token\":\"TR01\",\"name\":\"Tree\",\"family\":\"tree\"},"
        "{\"token\":\"Close\",\"name\":\"Close\",\"family\":\"spot\",\"locked\":true,\"note\":\"n\"}],"
        "\"families\":[{\"key\":\"tree\",\"name\":\"Tree\",\"tab\":\"Main\"},{\"key\":\"spot\",\"name\":\"Spot\"}],"
        "\"quickSets\":[{\"name\":\"Tree only\",\"families\":[\"tree\"]}]}";
    p["Contents/Resources/groups.json"] = "{\"groups\":[{\"key\":\"a\",\"name\":\"A\",\"match\":\"^a\"},"
                                          "{\"key\":\"b\",\"name\":\"B\",\"match\":\"^b\"}]}";
    p["Contents/Resources/images.json"] =
        "{\"main\":{\"sources\":[\"library:NKS/*/x.png\",\"file:images/main.png\"]}}";
    p["Contents/Resources/notes.md"] = "hello";
    return p;
}

std::optional<ProductProfile> load(const Parts &parts, QString *err)
{
    static std::unique_ptr<QTemporaryDir> dir; // freed at exit
    dir.reset(new QTemporaryDir);
    for (auto it = parts.begin(); it != parts.end(); ++it) {
        const QString path = dir->filePath("T.smtprofile/" + it.key());
        QDir().mkpath(QFileInfo(path).path());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return std::nullopt;
        f.write(it.value());
    }
    return loadProfile(dir->filePath("T.smtprofile"), err);
}

void replaceIn(Parts &p, const QString &key, const QByteArray &from, const QByteArray &to)
{
    QVERIFY(p[key].contains(from));
    p[key].replace(from, to);
}

} // namespace

class TstProfile : public QObject
{
    Q_OBJECT
private slots:
    void samplesDirIsNormalised()
    {
        auto parts = validParts();
        replaceIn(parts, "Contents/Resources/library.json", "\"samplesDir\":\"Samples\"", "\"samplesDir\":\"./Samples/\"");
        QString err;
        const auto p = load(parts, &err);
        QVERIFY2(p.has_value(), qPrintable(err));
        QCOMPARE(p->samplesDir, "Samples");
    }
    void emptyGroupMatchIsRejected()
    {
        auto parts = validParts();
        replaceIn(parts, "Contents/Resources/groups.json", "\"match\":\"^a\"", "\"match\":\"\"");
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY2(err.contains("match"), qPrintable(err));
    }
    void loadsAValidBundle()
    {
        QString err;
        const auto p = load(validParts(), &err);
        QVERIFY2(p.has_value(), qPrintable(err));
        QCOMPARE(p->id, "com.example.test");
        QCOMPARE(p->name, "Test Profile");
        QCOMPARE(p->version, "1.2");
        QCOMPARE(p->samplesDir, "Samples");
        QVERIFY(p->folderPattern.match("test lib").hasMatch());
        QCOMPARE(p->mics.size(), 2);
        QVERIFY(p->mic("tr01"));
        QVERIFY(!p->mic("tr01")->locked);
        QCOMPARE(p->mic("tr01")->family, "tree");
        QVERIFY(p->mic("close")->locked);
        QCOMPARE(p->mic("close")->note, "n");
        QVERIFY(!p->mic("nope"));
        QCOMPARE(p->families.size(), 2);
        QCOMPARE(p->families[0].tab, "Main");
        QCOMPARE(p->quickSets.size(), 1);
        QCOMPARE(p->quickSets[0].families, QStringList{"tree"});
        QCOMPARE(p->groups.size(), 2);
        QVERIFY(p->group("b"));
        QVERIFY(!p->group("c"));
        QVERIFY(!p->groups[0].byDir);
        QCOMPARE(p->images["main"].size(), 2);
        QCOMPARE(p->notes, "hello");
        QVERIFY(p->bundlePath.endsWith("T.smtprofile"));
    }

    void rejectsBadLibraryParts()
    {
        const QList<QPair<QByteArray, QByteArray>> cases{
            {"\"folderPattern\":\"^Test.*\"", "\"folderPatern\":\"^Test.*\""},
            {"\"samplesDir\":\"Samples\"", "\"samplesDir\":\"\""},
            {"\"samplesDir\":\"Samples\"", "\"samplesDir\":\"../..\""},
            {"\"samplesDir\":\"Samples\"", "\"samplesDir\":\"/\""}};
        for (const auto &[from, to] : cases) {
            auto parts = validParts();
            replaceIn(parts, "Contents/Resources/library.json", from, to);
            QString err;
            QVERIFY2(!load(parts, &err).has_value(), to.constData());
            QVERIFY(!err.isEmpty());
        }
    }

    void rejectsLibraryImageEscape()
    {
        auto parts = validParts();
        replaceIn(parts, "Contents/Resources/images.json", "library:NKS/*/x.png", "library:../../x.png");
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY2(err.contains("bad source"), qPrintable(err));
    }

    void rejectsDuplicateOrEmptyFamilyAndGroupKeys()
    {
        auto parts = validParts();
        replaceIn(parts, "Contents/Resources/mics.json", "\"key\":\"spot\"", "\"key\":\"tree\"");
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY2(err.contains("tree"), qPrintable(err));

        parts = validParts();
        replaceIn(parts, "Contents/Resources/groups.json", "\"key\":\"b\"", "\"key\":\"a\"");
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY2(err.contains("'a'"), qPrintable(err));

        parts = validParts();
        replaceIn(parts, "Contents/Resources/groups.json", "\"key\":\"b\"", "\"key\":\"\"");
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY2(err.contains("group key"), qPrintable(err));
    }

    void missingFormatSaysSo()
    {
        auto parts = validParts();
        replaceIn(parts, "Contents/Info.plist", "<key>SMTProfileFormat</key><string>1</string>", "");
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY2(err.contains("lacks SMTProfileFormat"), qPrintable(err));
    }

    void rejectsFormatTwo()
    {
        auto parts = validParts();
        parts["Contents/Info.plist"].replace("<string>1</string>", "<string>2</string>");
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY2(err.contains("newer app"), qPrintable(err));
    }

    void rejectsMissingRequiredPart()
    {
        auto parts = validParts();
        parts.remove("Contents/Resources/groups.json");
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY(!err.isEmpty());
    }

    void rejectsPathEscapingResources()
    {
        auto parts = validParts();
        parts["Contents/Info.plist"].replace("mics.json", "../x.json");
        parts["Contents/x.json"] = parts["Contents/Resources/mics.json"];
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY(!err.isEmpty());
    }

    void rejectsAbsolutePartPath()
    {
        auto parts = validParts();
        parts["Contents/Info.plist"].replace("mics.json", "/etc/hosts");
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY(!err.isEmpty());
    }

    void spitfireEntriesAreOptionalAndParsed()
    {
        auto parts = validParts();
        QString err;
        auto p = load(parts, &err);
        QVERIFY2(p.has_value(), qPrintable(err));
        QVERIFY(p->spitfireEntries.isEmpty()); // the part is optional
        parts["Contents/Info.plist"].replace("<key>SMTProfileNotes</key>",
                                             "<key>SMTProfileSpitfire</key><string>spitfire.json</string><key>SMTProfileNotes</key>");
        parts["Contents/Resources/spitfire.json"] =
            "{\"entries\":[{\"name\":\"Test\",\"keys\":{\"samples\":[\"Samples\"],\"patches\":[\"\",\"Patches/Test\"]}}]}";
        p = load(parts, &err);
        QVERIFY2(p.has_value(), qPrintable(err));
        QCOMPARE(p->spitfireEntries.size(), 1);
        QCOMPARE(p->spitfireEntries[0].name, QString("Test"));
        QCOMPARE(p->spitfireEntries[0].keys["patches"], (QStringList{"", "Patches/Test"}));
    }

    void rejectsBadSpitfireEntries_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("empty name") << QByteArray("{\"entries\":[{\"name\":\"\",\"keys\":{\"samples\":[\"S\"]}}]}");
        QTest::newRow("no keys") << QByteArray("{\"entries\":[{\"name\":\"A\",\"keys\":{}}]}");
        QTest::newRow("dotdot") << QByteArray("{\"entries\":[{\"name\":\"A\",\"keys\":{\"samples\":[\"../x\"]}}]}");
        QTest::newRow("dotdot inside") << QByteArray("{\"entries\":[{\"name\":\"A\",\"keys\":{\"samples\":[\"a/../../x\"]}}]}");
        QTest::newRow("absolute") << QByteArray("{\"entries\":[{\"name\":\"A\",\"keys\":{\"samples\":[\"/etc\"]}}]}");
        QTest::newRow("not a list") << QByteArray("{\"entries\":[{\"name\":\"A\",\"keys\":{\"samples\":\"Samples\"}}]}");
        QTest::newRow("duplicate entry") << QByteArray("{\"entries\":[{\"name\":\"A\",\"keys\":{\"s\":[\"S\"]}},{\"name\":\"A\",\"keys\":{\"s\":[\"S\"]}}]}");
        QTest::newRow("empty key") << QByteArray("{\"entries\":[{\"name\":\"A\",\"keys\":{\"\":[\"S\"]}}]}");
        QTest::newRow("empty segment") << QByteArray("{\"entries\":[{\"name\":\"A\",\"keys\":{\"s\":[\"a//b\"]}}]}");
        QTest::newRow("trailing slash") << QByteArray("{\"entries\":[{\"name\":\"A\",\"keys\":{\"s\":[\"a/\"]}}]}");
        QTest::newRow("dot segment") << QByteArray("{\"entries\":[{\"name\":\"A\",\"keys\":{\"s\":[\"a/./b\"]}}]}");
        QTest::newRow("backslash") << QByteArray("{\"entries\":[{\"name\":\"A\",\"keys\":{\"s\":[\"a\\\\b\"]}}]}");
        QTest::newRow("too many sub-paths") << QByteArray("{\"entries\":[{\"name\":\"A\",\"keys\":{\"s\":[\"1\",\"2\",\"3\",\"4\",\"5\",\"6\",\"7\",\"8\",\"9\",\"10\",\"11\",\"12\",\"13\",\"14\",\"15\",\"16\",\"17\"]}}]}");
        QTest::newRow("not a string") << QByteArray("{\"entries\":[{\"name\":\"A\",\"keys\":{\"samples\":[1]}}]}");
    }
    void rejectsBadSpitfireEntries()
    {
        QFETCH(QByteArray, json);
        auto parts = validParts();
        parts["Contents/Info.plist"].replace("<key>SMTProfileNotes</key>",
                                             "<key>SMTProfileSpitfire</key><string>spitfire.json</string><key>SMTProfileNotes</key>");
        parts["Contents/Resources/spitfire.json"] = json;
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY2(err.contains("spitfire.json"), qPrintable(err));
    }

    void legalOddNamesAndLimits()
    {
        auto parts = validParts();
        parts["Contents/Info.plist"].replace("<key>SMTProfileNotes</key>",
                                             "<key>SMTProfileSpitfire</key><string>spitfire.json</string><key>SMTProfileNotes</key>");
        parts["Contents/Resources/spitfire.json"] =
            "{\"entries\":[{\"name\":\"A\",\"keys\":{\"s\":[\"a..b\",\"\",\"x/y z\"]}}]}"; // "a..b" is a legal name
        QString err;
        auto p = load(parts, &err);
        QVERIFY2(p.has_value(), qPrintable(err));
        QCOMPARE(p->spitfireEntries[0].keys["s"], (QStringList{"a..b", "", "x/y z"}));
        // 64 entries are fine, 65 are not
        auto many = [&](int n) {
            QByteArray j = "{\"entries\":[";
            for (int i = 0; i < n; ++i)
                j += QByteArray(i ? "," : "") + "{\"name\":\"E" + QByteArray::number(i) + "\",\"keys\":{\"s\":[\"S\"]}}";
            return j + "]}";
        };
        parts["Contents/Resources/spitfire.json"] = many(64);
        QVERIFY(load(parts, &err).has_value());
        parts["Contents/Resources/spitfire.json"] = many(65);
        QVERIFY(!load(parts, &err).has_value());
    }

    void builtInProfilesDescribeTheirSpitfireEntries()
    {
        QCOMPARE(builtInProfile(kBbcsoProfileId).spitfireEntries.size(), 1);
        const auto w = builtInProfile(kEwcProfileId).spitfireEntries;
        QStringList names;
        for (const auto &e : w)
            names << e.name;
        QCOMPARE(names, (QStringList{"Eric Whitacre Choir", "Eric Whitacre Choir Evo Grid", "Eric Whitacre Choir Samples"}));
    }

    void rejectsBadSampleFileRegex()
    {
        auto parts = validParts();
        replaceIn(parts, "Contents/Resources/library.json", "^(?<group>", "^(?<group>(");
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY(!err.isEmpty());
    }

    void rejectsSampleFileWithoutMicGroup()
    {
        auto parts = validParts();
        replaceIn(parts, "Contents/Resources/library.json", "(?<mic>", "(?<m>");
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY2(err.contains("mic"), qPrintable(err));
    }

    void rejectsDuplicateMicToken()
    {
        auto parts = validParts();
        replaceIn(parts, "Contents/Resources/mics.json", "\"Close\"", "\"tr01\"");
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY(!err.isEmpty());
    }

    void rejectsQuickSetWithUnknownFamily()
    {
        auto parts = validParts();
        replaceIn(parts, "Contents/Resources/mics.json", "\"families\":[\"tree\"]", "\"families\":[\"zzz\"]");
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY2(err.contains("zzz"), qPrintable(err));
    }

    void rejectsMicWithUnknownFamily()
    {
        auto parts = validParts();
        replaceIn(parts, "Contents/Resources/mics.json", "\"family\":\"spot\"", "\"family\":\"zzz\"");
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY2(err.contains("zzz"), qPrintable(err));
    }

    void rejectsGroupModeMix()
    {
        auto parts = validParts();
        replaceIn(parts, "Contents/Resources/groups.json", "\"match\":\"^a\"", "\"match\":\"^a\",\"by\":\"dir\"");
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY(!err.isEmpty());
    }

    void rejectsNameGroupsWithoutGroupCapture()
    {
        auto parts = validParts();
        replaceIn(parts, "Contents/Resources/library.json", "(?<group>", "(?<g>");
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        QVERIFY2(err.contains("group"), qPrintable(err));
    }

    void acceptsDirGroupsWithoutGroupCapture()
    {
        auto parts = validParts();
        replaceIn(parts, "Contents/Resources/library.json", "(?<group>", "(?<g>");
        parts["Contents/Resources/groups.json"] = "{\"groups\":[{\"key\":\"a\",\"name\":\"A\",\"match\":\"^a\",\"by\":\"dir\"}]}";
        QString err;
        const auto p = load(parts, &err);
        QVERIFY2(p.has_value(), qPrintable(err));
        QVERIFY(p->groups[0].byDir);
    }

    void rejectsBadImageSource()
    {
        auto parts = validParts();
        replaceIn(parts, "Contents/Resources/images.json", "file:images/main.png", "file:../../x.png");
        QString err;
        QVERIFY(!load(parts, &err).has_value());
        parts = validParts();
        replaceIn(parts, "Contents/Resources/images.json", "file:images/main.png", "http://x");
        QVERIFY(!load(parts, &err).has_value());
    }

    void rejectsBadCacheImageSource()
    {
        for (const QString &bad : {"cache:../x", "cache:a/b", "cache:/abs", "cache:.hidden", "cache:"}) {
            auto parts = validParts();
            replaceIn(parts, "Contents/Resources/images.json", "file:images/main.png", bad.toUtf8());
            QString err;
            QVERIFY2(!load(parts, &err).has_value(), qPrintable(bad));
        }
        auto parts = validParts();
        replaceIn(parts, "Contents/Resources/images.json", "file:images/main.png", "cache:-5946911695875460174");
        QString err;
        QVERIFY2(load(parts, &err).has_value(), qPrintable(err));
    }

    void urlImageSourceMustBeAllowListed()
    {
        const QStringList bad = {"http://www.spitfireaudio.com/a.png", "https://example.com/a.png",
                                 "https://www.spitfireaudio.com.evil.com/a.png", "https://www.spitfireaudio.com@evil.com/",
                                 "https://user@www.spitfireaudio.com/a.png", "https://www.spitfireaudio.com:8443/a.png",
                                 "https://www.spitfireaudio.com/a/../b.png", "https://www.spitfireaudio.com",
                                 "https://www.spitfireaudio.com/" + QString(300, 'a')};
        for (const QString &u : bad) {
            auto parts = validParts();
            replaceIn(parts, "Contents/Resources/images.json", "file:images/main.png", ("url:" + u).toUtf8());
            QString err;
            QVERIFY2(!load(parts, &err).has_value(), qPrintable(u));
            QVERIFY2(err.contains("bad source"), qPrintable(err));
        }
        for (const QString &u : {"https://www.spitfireaudio.com/cdn/shop/files/x.jpg?width=500", "HTTPS://WWW.SpitfireAudio.com/x.png"}) {
            auto parts = validParts();
            replaceIn(parts, "Contents/Resources/images.json", "file:images/main.png", ("url:" + u).toUtf8());
            QString err;
            QVERIFY2(load(parts, &err).has_value(), qPrintable(err));
        }
    }

    void creditMustBeAllowListedToo()
    {
        auto parts = validParts();
        replaceIn(parts, "Contents/Resources/images.json", "{\"sources\"", "{\"credit\":\"https://www.spitfireaudio.com/eric-whitacre-choir\",\"sources\"");
        QString err;
        const auto p = load(parts, &err);
        QVERIFY2(p.has_value(), qPrintable(err));
        QCOMPARE(p->imageCredits["main"], QString("https://www.spitfireaudio.com/eric-whitacre-choir"));
        for (const char *bad : {"http://www.spitfireaudio.com/x", "https://evil.com/x"}) {
            parts = validParts();
            replaceIn(parts, "Contents/Resources/images.json", "{\"sources\"", QByteArray("{\"credit\":\"") + bad + "\",\"sources\"");
            QVERIFY2(!load(parts, &err).has_value(), bad);
            QVERIFY2(err.contains("credit"), qPrintable(err));
        }
    }

    void missingImagesAndNotesAreFine()
    {
        auto parts = validParts();
        parts["Contents/Info.plist"] = plist({{"CFBundleIdentifier", "com.example.test"},
                                              {"CFBundleName", "Test Profile"},
                                              {"CFBundleShortVersionString", "1.2"},
                                              {"SMTProfileFormat", "1"},
                                              {"SMTProfileLibrary", "library.json"},
                                              {"SMTProfileMics", "mics.json"},
                                              {"SMTProfileGroups", "groups.json"}});
        QString err;
        const auto p = load(parts, &err);
        QVERIFY2(p.has_value(), qPrintable(err));
        QVERIFY(p->images.isEmpty());
        QVERIFY(p->notes.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstProfile)
#include "tst_profile.moc"
