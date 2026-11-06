#include <QtTest>
#include "unixonly.h"
#include <QImage>
#include <QCryptographicHash>
#include "profileregistry.h"

using namespace smt;

namespace {

const ProfileEntry *find(const QList<ProfileEntry> &l, const QString &id)
{
    for (const auto &e : l)
        if (e.id == id)
            return &e;
    return nullptr;
}

const QString kBbcso = QStringLiteral("name.mbishop.smtprofile.bbcso");
const QString kEwc = QStringLiteral("name.mbishop.smtprofile.ewc");

// Writes <dir>/<folder>/Contents/... as a minimal profile with the given bundle id.
void writeProfile(const QString &dir, const QString &folder, const QString &id, bool valid = true)
{
    const QString root = dir + "/" + folder + "/Contents";
    QVERIFY(QDir().mkpath(root + "/Resources"));
    auto put = [&](const QString &rel, const QByteArray &data) {
        QFile f(root + "/" + rel);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    };
    put("Info.plist", "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
                      "<key>CFBundleIdentifier</key><string>" + id.toUtf8() + "</string>"
                      "<key>CFBundleName</key><string>User " + id.toUtf8() + "</string>"
                      "<key>SMTProfileFormat</key><string>1</string>"
                      "<key>SMTProfileLibrary</key><string>library.json</string>"
                      "<key>SMTProfileMics</key><string>mics.json</string>"
                      "<key>SMTProfileGroups</key><string>groups.json</string></dict></plist>");
    put("Resources/library.json",
        valid ? "{\"folderPattern\":\"^User\",\"samplesDir\":\"Samples\",\"sampleFile\":\"_(?<mic>[a-z]{2}\\\\d{2})\\\\.spitfire$\"}"
              : "{\"folderPattern\":\"(\",\"sampleFile\":\"x\"}");
    put("Resources/mics.json", "{\"mics\":[],\"families\":[]}");
    put("Resources/groups.json", "{\"groups\":[]}");
}

} // namespace

// A cache dir that does not exist: tests never see the user's real image cache.
static const QString kNoUrl = QStringLiteral("/nonexistent/smt-no-url-cache");
static QString noCache(const QString &base) { return base + "/no-cache"; }

class TstProfileRegistry : public QObject
{
    Q_OBJECT
private slots:
    void builtInsAreValid() // empty user dir: the only source is the compiled-in :/profiles resources
    {
        const auto l = loadProfiles(QString());
        QCOMPARE(l.size(), 2);
        for (const auto &id : {kBbcso, kEwc}) {
            const auto *e = find(l, id);
            QVERIFY2(e, qPrintable(id));
            QVERIFY2(e->valid(), qPrintable(e->error));
            QVERIFY(e->builtIn);
            QVERIFY(e->path.startsWith(":/profiles/"));
            QVERIFY(e->profile->images.contains("main"));
            QVERIFY(e->profile->images.contains("logo"));
        }
    }

    void builtInMainImageIsEmptyWithoutCacheOrDownload()
    {
        const auto l = loadProfiles(QString());
        QTemporaryDir lib; // no NKS folder
        QVERIFY(find(l, kBbcso)->profile->bundlePath.startsWith(":/profiles/"));
        for (const QString &id : {kBbcso, kEwc}) {
            const ProductProfile &p = *find(l, id)->profile;
            QCOMPARE(resolveProfileImage(p, "main", lib.path(), noCache(lib.path()), kNoUrl), QString());
            QCOMPARE(pendingImageUrls(p, "main", lib.path(), noCache(lib.path()), kNoUrl).size(), 1); // the website picture
        }

        // The library's own tiny NKS thumbnail is no longer a main source.
        const QString dir = lib.path() + "/NKS/PAResources/image/Spitfire Audio/Spitfire Audio - BBC Symphony Orchestra";
        QVERIFY(QDir().mkpath(dir));
        QFile f(dir + "/MST_Artwork.png");
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(resolveProfileImage(*find(l, kBbcso)->profile, "main", lib.path(), noCache(lib.path()), kNoUrl), QString());
    }

    void builtInMainImagePrefersSpitfireCache()
    {
        const auto l = loadProfiles(QString());
        QTemporaryDir lib;
        const QStringList bbcso = {"-5946911695875460174", "-5946911696707943383", "-2722765241741408607"};
        const QStringList whitacre = {"762646866156051825", "762646877082424537", "-3978629468329901306"};
        const std::pair<QString, QStringList> cases[] = {{kBbcso, bbcso}, {kEwc, whitacre}};
        for (const auto &c : cases) {
            for (const QString &name : c.second) {
                QTemporaryDir cache; // this file alone
                QVERIFY(QImage(4, 4, QImage::Format_RGB32).save(cache.path() + "/" + name, "PNG"));
                QCOMPARE(resolveProfileImage(*find(l, c.first)->profile, "main", lib.path(), cache.path(), kNoUrl),
                         cache.path() + "/" + name);
            }
        }
    }

    void resolvesCacheImageWithoutExtension()
    {
        QTemporaryDir cache;
        const QString path = cache.path() + "/-123";
        QVERIFY(QImage(4, 4, QImage::Format_RGB32).save(path, "PNG"));
        QVERIFY(!QImage(path).isNull()); // content sniffing, no extension
        ProductProfile p;
        p.images["main"] = {"cache:-123"};
        QCOMPARE(resolveProfileImage(p, "main", QString(), cache.path(), kNoUrl), path);
    }

    void cacheComesBeforeLibraryBeforeFileAndMissingFallsThrough()
    {
        QTemporaryDir lib, bundle, cache;
        QVERIFY(QDir().mkpath(bundle.path() + "/Contents/Resources/images"));
        QVERIFY(QDir().mkpath(lib.path() + "/a/X"));
        for (const QString &f : {bundle.path() + "/Contents/Resources/images/main.png", lib.path() + "/a/X/p.png", cache.path() + "/c2"}) {
            QFile file(f);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        ProductProfile p;
        p.bundlePath = bundle.path();
        p.images["main"] = {"cache:c1", "cache:c2", "library:a/*/p.png", "file:images/main.png"};
        QCOMPARE(resolveProfileImage(p, "main", lib.path(), cache.path(), kNoUrl), cache.path() + "/c2");
        QVERIFY(QFile::remove(cache.path() + "/c2"));
        QCOMPARE(resolveProfileImage(p, "main", lib.path(), cache.path(), kNoUrl), lib.path() + "/a/X/p.png");
        QCOMPARE(resolveProfileImage(p, "main", QString(), cache.path(), kNoUrl),
                 bundle.path() + "/Contents/Resources/images/main.png");
    }

    void badCacheNamesNeverResolve()
    {
        QTemporaryDir parent;
        const QString cache = parent.path() + "/cache";
        QVERIFY(QDir().mkpath(cache + "/sub"));
        // Real targets, so each rule alone must be what refuses the name.
        for (const QString &f : {parent.path() + "/secret", cache + "/sub/x", cache + "/.hidden", cache + "/a..b"}) {
            QFile file(f);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        ProductProfile p;
        for (const QString &n : {"../secret", "a..b/../../secret", "sub/x", "/etc/hosts", ".hidden", "a..b", "", ".."}) {
            p.images["main"] = {"cache:" + n};
            QCOMPARE(resolveProfileImage(p, "main", QString(), cache, kNoUrl), QString());
        }
    }

    void cacheSymlinkEscapingTheCacheDirDoesNotResolve()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir parent;
        const QString cache = parent.path() + "/cache";
        QVERIFY(QDir().mkpath(cache));
        QVERIFY(QImage(4, 4, QImage::Format_RGB32).save(parent.path() + "/outside", "PNG"));
        QVERIFY(QFile::link(parent.path() + "/outside", cache + "/link"));
        ProductProfile p;
        p.images["main"] = {"cache:link"};
        QCOMPARE(resolveProfileImage(p, "main", QString(), cache, kNoUrl), QString());
    }

    void bbcsoMicTableMatchesToday()
    {
        const auto p = find(loadProfiles(QString()), kBbcso)->profile;
        QCOMPARE(p->mics.size(), 20);
        const QList<QPair<QString, QString>> expect{
            {"mo01", "Mono"}, {"tr01", "Tree"}, {"ou01", "Outriggers"}, {"am01", "Ambient"},
            {"ba01", "Balcony"}, {"si01", "Sides"}, {"af01", "Atmos Front"}, {"ar01", "Atmos Rear"},
            {"md01", "Mids"}, {"st01", "Section Stereo"}, {"le01", "Leader"}, {"cl01", "Close"},
            {"cw01", "Close Wide"}, {"ss01", "Spill Strings"}, {"sw01", "Spill Wind"},
            {"sb01", "Spill Brass"}, {"sp01", "Spill Percussion"}, {"sf01", "Spill Full"},
            {"mi01", "Full Mix 1"}, {"mi02", "Full Mix 2"}};
        for (const auto &[token, name] : expect) {
            QVERIFY2(p->mic(token), qPrintable(token));
            QCOMPARE(p->mic(token)->name, name);
            QCOMPARE(p->mic(token)->locked, token == "mi01");
        }
        QCOMPARE(p->mic("cw01")->name, "Close Wide");
        QCOMPARE(p->mic("cw01")->family, "closewide");
        QCOMPARE(p->families.size(), 20); // every mic is its own family
        QStringList groups;
        for (const auto &g : p->groups) {
            QVERIFY(g.byDir);
            groups << g.key + "=" + g.match;
        }
        QCOMPARE(groups, (QStringList{"brass=_Brass", "percussion=_Percussion", "strings=_Strings", "woodwinds=_Woodwinds"}));
        // tabs and rows exactly as the old mainwindow.cpp tabList, in order (the GUI follows family order)
        const QList<QPair<QString, QStringList>> tabs{{"Mixes", {"mix1", "mix2"}},
                                                      {"CTAO", {"close", "closewide", "tree", "ambient", "outriggers"}},
                                                      {"Stereo/Mono", {"mono", "sectionstereo"}},
                                                      {"Spills", {"spillfull", "spillbrass", "spillpercussion", "spillstrings", "spillwind"}},
                                                      {"Other", {"leader", "mids", "sides", "balcony"}},
                                                      {"Atmos", {"atmosfront", "atmosrear"}}};
        int seen = 0;
        for (const auto &[tab, keys] : tabs)
            for (const auto &key : keys) {
                QVERIFY(seen < p->families.size());
                const FamilyDef &f = p->families[seen];
                QCOMPARE(f.key, key);
                QCOMPARE(f.tab, tab);
                ++seen;
            }
        QCOMPARE(seen, 20);
        QVERIFY(p->quickSets.isEmpty()); // no built-in quick set ships
        // classification regex still reads a real BBCSO file name
        const auto m = p->sampleFile.match("x/BBCSO_Violins_Leg_TR01_02.spitfire");
        QVERIFY(m.hasMatch());
        QCOMPARE(m.captured("mic"), "TR01");
    }

    void whitacreMicTableAndLocks()
    {
        const auto p = find(loadProfiles(QString()), kEwc)->profile;
        QCOMPARE(p->mics.size(), 9);
        for (const char *t : {"cl01", "tr01", "am01", "ou01", "mi01", "ga01", "st01", "st02", "st03"}) {
            QVERIFY2(p->mic(t), t);
            QVERIFY2(!p->mic(t)->locked, t);
        }
        for (const char *t : {"cl02", "cl03", "cl04", "cl05", "st04"})
            QVERIFY2(!p->mic(t), t); // not offered: always copied
        QCOMPARE(p->mic("st01")->name, "Big");
        QCOMPARE(p->mic("mi01")->family, "mid");
        QStringList core, aux, stereo, order;
        for (const auto &f : p->families) {
            order << f.key;
            QVERIFY2(f.key != "required", "required family");
            QVERIFY2(f.tab == "Core" || f.tab == "Auxiliary" || f.tab == "Stereo", qPrintable(f.tab));
            (f.tab == "Core" ? core : f.tab == "Auxiliary" ? aux : stereo) << f.key;
        }
        QCOMPARE(core, (QStringList{"close", "tree", "ambient", "outriggers"}));
        QCOMPARE(aux, (QStringList{"mid", "gallery"}));
        QCOMPARE(stereo, (QStringList{"big", "medium", "small"}));
        QCOMPARE(order, core + aux + stereo);
        QStringList g;
        for (const auto &x : p->groups)
            g << x.key;
        QCOMPARE(g, (QStringList{"sopranos", "altos", "tenors", "bass", "tutti"}));
        QVERIFY(p->quickSets.isEmpty());
        QVERIFY(p->notes.contains("Error 4"));
        const auto m = p->sampleFile.match("ERICW_SOPRANOS_Long_CL01_V2.spitfire");
        QVERIFY(m.hasMatch());
        QCOMPARE(m.captured("group"), "SOPRANOS");
        QCOMPARE(m.captured("mic"), "CL01");
    }

    void userProfileOverridesBuiltInById()
    {
        QTemporaryDir d;
        writeProfile(d.path(), "Mine.smtprofile", kBbcso);
        writeProfile(d.path(), "Other.smtprofile", "com.example.other");
        const auto l = loadProfiles(d.path());
        QCOMPARE(l.size(), 3);
        const auto *e = find(l, kBbcso);
        QVERIFY(e->valid());
        QVERIFY(!e->builtIn);
        QCOMPARE(e->name, "User " + kBbcso);
        QVERIFY(find(l, "com.example.other"));
    }

    void lastUserProfileWithSameIdWins()
    {
        QTemporaryDir d;
        writeProfile(d.path(), "A.smtprofile", "com.example.dup");
        writeProfile(d.path(), "B.smtprofile", "com.example.dup");
        const auto l = loadProfiles(d.path());
        QCOMPARE(l.size(), 3);
        QVERIFY(find(l, "com.example.dup")->path.endsWith("/B.smtprofile"));
    }

    void brokenUserProfileDoesNotHideBuiltIn()
    {
        QTemporaryDir d;
        writeProfile(d.path(), "Broken.smtprofile", kBbcso, false);
        const auto l = loadProfiles(d.path());
        QCOMPARE(l.size(), 3);
        const auto *b = find(l, kBbcso);
        QVERIFY(b->valid());
        QVERIFY(b->builtIn);
        const auto *bad = find(l, "Broken");
        QVERIFY(bad);
        QVERIFY(!bad->valid());
        QVERIFY(!bad->error.isEmpty());
        QVERIFY(!bad->builtIn);
    }

    void noBundleInTestProcess() { QVERIFY(bundledProfilesDir().isEmpty()); }

    void builtInDirReplacesResources()
    {
        QTemporaryDir b;
        writeProfile(b.path(), "Copy.smtprofile", kBbcso);
        const auto l = loadProfilesFrom(b.path(), QString());
        QCOMPARE(l.size(), 1);
        QCOMPARE(l[0].id, kBbcso);
        QVERIFY(l[0].builtIn);
        QVERIFY(l[0].path.startsWith(b.path()));
    }

    void emptyBuiltInDirFallsBackToResources()
    {
        const auto l = loadProfilesFrom(QString(), QString());
        QCOMPARE(l.size(), 2);
        QVERIFY(find(l, kBbcso)->path.startsWith(":/profiles/"));
    }

    void missingUserDirIsFine()
    {
        QCOMPARE(loadProfiles("/nonexistent/smt-test-dir").size(), 2);
    }

    void userProfilesDirFollowsEnv()
    {
        struct Restore {
            const bool had = qEnvironmentVariableIsSet("SMT_APP_DATA_DIR");
            const QByteArray old = qgetenv("SMT_APP_DATA_DIR");
            ~Restore() { had ? (void)qputenv("SMT_APP_DATA_DIR", old) : (void)qunsetenv("SMT_APP_DATA_DIR"); }
        } restore;
        QTemporaryDir d;
        qputenv("SMT_APP_DATA_DIR", d.path().toUtf8());
        QCOMPARE(userProfilesDir(), d.path() + "/Profiles");
    }

    void urlSourceResolvesToDownloadedCopyInSourceOrder()
    {
        const QString url = "https://www.spitfireaudio.com/a.png?width=500";
        QTemporaryDir cache, urls;
        ProductProfile p;
        p.images["main"] = {"cache:c1", "url:" + url, "file:images/main.png"};
        QCOMPARE(resolveProfileImage(p, "main", QString(), cache.path(), urls.path()), QString());
        QCOMPARE(imageUrlCacheName(QUrl(url)), QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex()));
        const QString file = urls.path() + "/" + imageUrlCacheName(QUrl(url));
        QVERIFY(QImage(4, 4, QImage::Format_RGB32).save(file, "PNG"));
        QCOMPARE(resolveProfileImage(p, "main", QString(), cache.path(), urls.path()), file);
        QVERIFY(QImage(4, 4, QImage::Format_RGB32).save(cache.path() + "/c1", "PNG")); // earlier source wins
        QCOMPARE(resolveProfileImage(p, "main", QString(), cache.path(), urls.path()), cache.path() + "/c1");
    }

    void urlCopySymlinkLeavingTheUrlCacheDoesNotResolve()
    {
        SMT_UNIX_ONLY();
        const QString url = "https://www.spitfireaudio.com/a.png";
        QTemporaryDir parent;
        const QString urls = parent.path() + "/urls";
        QVERIFY(QDir().mkpath(urls));
        QVERIFY(QImage(4, 4, QImage::Format_RGB32).save(parent.path() + "/outside", "PNG"));
        QVERIFY(QFile::link(parent.path() + "/outside", urls + "/" + imageUrlCacheName(QUrl(url))));
        ProductProfile p;
        p.images["main"] = {"url:" + url};
        QCOMPARE(resolveProfileImage(p, "main", QString(), QString(), urls), QString());
    }

    void pendingImageUrlsStopAtResolvingSource()
    {
        const QString u1 = "https://www.spitfireaudio.com/1.png", u2 = "https://www.spitfireaudio.com/2.png";
        QTemporaryDir cache, urls;
        ProductProfile p;
        p.images["main"] = {"url:" + u1, "cache:c1", "url:" + u2};
        QCOMPARE(pendingImageUrls(p, "main", QString(), cache.path(), urls.path()), (QList<QUrl>{QUrl(u1), QUrl(u2)}));
        QVERIFY(QImage(4, 4, QImage::Format_RGB32).save(cache.path() + "/c1", "PNG"));
        QCOMPARE(pendingImageUrls(p, "main", QString(), cache.path(), urls.path()), (QList<QUrl>{QUrl(u1)})); // u2 is behind c1
        QVERIFY(QImage(4, 4, QImage::Format_RGB32).save(urls.path() + "/" + imageUrlCacheName(QUrl(u1)), "PNG"));
        QVERIFY(pendingImageUrls(p, "main", QString(), cache.path(), urls.path()).isEmpty());
        QVERIFY(pendingImageUrls(p, "other", QString(), cache.path(), urls.path()).isEmpty());
    }

    void builtInProfilesCarryUrlAndCredit()
    {
        const auto l = loadProfiles(QString());
        for (const QString &id : {kBbcso, kEwc}) {
            const auto &p = *find(l, id)->profile;
            QVERIFY(p.images["main"].filter("url:https://www.spitfireaudio.com/cdn/shop/files/").size() == 1);
            QVERIFY(p.imageCredits["main"].startsWith("https://www.spitfireaudio.com/"));
            QVERIFY(!p.images["main"].filter("MST_Artwork").size());
            QVERIFY(p.images["logo"].filter("MST_Logo").size() == 1);
        }
    }

    void urlCacheDirFollowsEnv()
    {
        struct Restore {
            const bool had = qEnvironmentVariableIsSet("SMT_APP_DATA_DIR");
            const QByteArray old = qgetenv("SMT_APP_DATA_DIR");
            ~Restore() { had ? (void)qputenv("SMT_APP_DATA_DIR", old) : (void)qunsetenv("SMT_APP_DATA_DIR"); }
        } restore;
        QTemporaryDir d;
        qputenv("SMT_APP_DATA_DIR", d.path().toUtf8());
        QCOMPARE(profileImageUrlCacheDir(), d.path() + "/ImageCache");
    }

    void resolvesLibraryGlobImage()
    {
        QTemporaryDir lib;
        QVERIFY(QDir().mkpath(lib.path() + "/a/Foo/b"));
        QVERIFY(QDir().mkpath(lib.path() + "/a/Bar/b"));
        QFile f(lib.path() + "/a/Bar/b/pic.png");
        QVERIFY(f.open(QIODevice::WriteOnly));
        ProductProfile p;
        p.images["main"] = {"library:a/*/b/pic.png"};
        QCOMPARE(resolveProfileImage(p, "main", lib.path(), noCache(lib.path()), kNoUrl), lib.path() + "/a/Bar/b/pic.png");
        p.images["deep"] = {"library:a/*/pic.png"}; // '*' is one level only
        QCOMPARE(resolveProfileImage(p, "deep", lib.path(), noCache(lib.path()), kNoUrl), QString());
    }

    void fallsBackToProfileFile()
    {
        QTemporaryDir lib, bundle;
        QVERIFY(QDir().mkpath(bundle.path() + "/Contents/Resources/images"));
        QFile f(bundle.path() + "/Contents/Resources/images/main.png");
        QVERIFY(f.open(QIODevice::WriteOnly));
        ProductProfile p;
        p.bundlePath = bundle.path();
        p.images["main"] = {"library:nope/*.png", "file:images/main.png"};
        QCOMPARE(resolveProfileImage(p, "main", lib.path(), noCache(lib.path()), kNoUrl), bundle.path() + "/Contents/Resources/images/main.png");
    }

    void refusesFileOutsideResources()
    {
        QTemporaryDir bundle;
        QVERIFY(QDir().mkpath(bundle.path() + "/Contents/Resources"));
        QFile f(bundle.path() + "/Contents/secret.png");
        QVERIFY(f.open(QIODevice::WriteOnly));
        ProductProfile p;
        p.bundlePath = bundle.path();
        p.images["main"] = {"file:../secret.png"};
        QCOMPARE(resolveProfileImage(p, "main", bundle.path(), noCache(bundle.path()), kNoUrl), QString());
    }

    void missingImageIsEmpty()
    {
        ProductProfile p;
        QCOMPARE(resolveProfileImage(p, "main", QDir::tempPath(), noCache(QDir::tempPath()), kNoUrl), QString());
        p.images["main"] = {"library:x.png"};
        QCOMPARE(resolveProfileImage(p, "main", QString(), noCache(QDir::tempPath()), kNoUrl), QString());
    }

    void suggestsByFolderName()
    {
        const auto l = loadProfiles(QString());
        const auto *w = suggestProfile(l, "Spitfire Audio - Eric Whitacre Choir 2");
        QVERIFY(w);
        QCOMPARE(w->id, kEwc);
        const auto *b = suggestProfile(l, "Spitfire Audio - BBC Symphony Orchestra (Pro)");
        QVERIFY(b);
        QCOMPARE(b->id, kBbcso);
        QVERIFY(!suggestProfile(l, "Other"));
    }
};

QTEST_GUILESS_MAIN(TstProfileRegistry)
#include "tst_profileregistry.moc"
