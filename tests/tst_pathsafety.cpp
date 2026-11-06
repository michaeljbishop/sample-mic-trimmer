#include <QtTest>
#include "unixonly.h"
#ifndef Q_OS_WIN
#include <unistd.h>
#endif
#include "pathsafety.h"
#include "pathsafety_testing.h"
#include "winpath.h"

using namespace smt;
using testing::MountId;

// A fake statfs: every canonical path under a key "mounts" share mounts[key].first with
// the path mounts[key].second (relative to that share's root) prepended.
static void fakeMounts(const QHash<QString, QPair<QString, QString>> &mounts)
{
    testing::setMountIdProvider([mounts](const QString &path) -> std::optional<MountId> {
        for (auto it = mounts.cbegin(); it != mounts.cend(); ++it)
            if (path == it.key() || path.startsWith(it.key() + '/')) {
                QString rel = it.value().second + path.mid(it.key().size());
                while (rel.startsWith('/'))
                    rel.remove(0, 1);
                return MountId{it.value().first, rel};
            }
        return std::nullopt;
    });
}

// A fake stat: paths under t/m1 and t/m2 are one server's folders seen through two mounts
// (dev 1 and 2; inode from the path below the mount, one birth time); others unknown.
static void fakeStatMounts(const QString &m1, const QString &m2)
{
    testing::setStatIdProvider([m1, m2](const QString &path) -> std::optional<testing::StatId> {
        const QString p = QFileInfo(path).canonicalFilePath();
        for (const auto &[root, dev] : {std::pair{m1, 1}, std::pair{m2, 2}})
            if (p == root || p.startsWith(root + '/'))
                return testing::StatId{quint64(dev), quint64(qHash(p.mid(root.size()))), 1700000000, 5};
        return std::nullopt;
    });
}

static QString canon(const QString &path)
{
    return QFileInfo(path).canonicalFilePath();
}

class TstPathSafety : public QObject
{
    Q_OBJECT
    static QString mk(const QTemporaryDir &t, const QString &rel)
    {
        QDir().mkpath(t.filePath(rel));
        return t.filePath(rel);
    }
private slots:
    void cleanup()
    {
        testing::setMountIdProvider({});
        testing::setStatIdProvider({});
        testing::setFinalPathProvider({});
        testing::setFileIdProvider({});
    }

    // ---- Windows flavour (UNVERIFIED against real Windows; the logic is pure and runs here) ----
    void windowsFlavourNameRulesReadEitherSeparatorAndIgnoreCase()
    {
        const QString src = "C:\\Libs\\Spitfire Audio - BBC Symphony Orchestra";
        using namespace testing;
        QCOMPARE(checkDestinationNameFor(true, src, "D:/out/spitfire audio - bbc symphony orchestra"), QString());
        QCOMPARE(checkDestinationNameFor(true, src, "\\\\server\\share\\Spitfire Audio - BBC Symphony Orchestra\\"), QString());
        QCOMPARE(checkDestinationNameFor(true, "//srv/share/Spitfire Audio - X", "C:\\o\\Spitfire Audio - X"), QString());
        QVERIFY(checkDestinationNameFor(true, src, "D:\\out\\Trimmed").contains("Spitfire Audio - BBC Symphony Orchestra"));
        QCOMPARE(checkDestinationNameFor(true, "C:\\Libs\\Other", "D:\\x"), QString()); // not a Spitfire product folder
        // the Unix flavour does not read a backslash as a separator: unchanged behaviour
#ifndef Q_OS_WIN // QFileInfo itself reads a backslash as a separator there
        QVERIFY(!checkDestinationNameFor(false, "/a/Spitfire Audio - X", "D:\\out\\Spitfire Audio - X").isEmpty());
#endif
    }
    void windowsFlavourResolveDestinationFolder()
    {
        const QString name = "Spitfire Audio - BBC Symphony Orchestra";
        const QString src = "C:\\Libs\\" + name;
        using namespace testing;
        QCOMPARE(resolveDestinationFolderFor(true, src, "D:\\Trimmed"), "D:/Trimmed/" + name);
        QCOMPARE(resolveDestinationFolderFor(true, src, "D:\\Trimmed\\"), "D:/Trimmed/" + name);
        QCOMPARE(resolveDestinationFolderFor(true, src, "D:/"), "D:/" + name); // a drive root: no doubled slash
        QCOMPARE(resolveDestinationFolderFor(true, src, "D:\\"), "D:/" + name);
        QCOMPARE(resolveDestinationFolderFor(true, src, "\\\\server\\share"), "//server/share/" + name);
        QCOMPARE(resolveDestinationFolderFor(true, src, "//server/share/x"), "//server/share/x/" + name);
        // the exact name (any case, either separator) is used as the user gave it
        QCOMPARE(resolveDestinationFolderFor(true, src, "D:\\out\\" + name), "D:\\out\\" + name);
        QCOMPARE(resolveDestinationFolderFor(true, src, "D:/out/" + name.toLower() + "/"), "D:/out/" + name.toLower() + "/");
        // no constraint without a Spitfire product source, or without a source
        QCOMPARE(resolveDestinationFolderFor(true, "C:\\x\\other", "D:\\y"), QString("D:\\y"));
        QCOMPARE(resolveDestinationFolderFor(true, QString(), "D:\\y"), QString("D:\\y"));
        // the source's own parent resolves to the source: the overlap check refuses it later
        QCOMPARE(winpath::key(resolveDestinationFolderFor(true, src, "C:\\Libs")), winpath::key(src));
    }
    void windowsFinalPathsOfTwoSpellingsOfOneFolderAreRefused()
    {
        QTemporaryDir t;
        const QString src = mk(t, "src/Spitfire Audio - X"), dst = mk(t, "dst");
        // as GetFinalPathNameByHandle would answer for a mapped drive and a UNC path of one share
        testing::setFinalPathProvider([src, dst](const QString &path) -> std::optional<QString> {
            if (path == canon(src))
                return QString("\\\\?\\UNC\\server\\share\\Lib\\Spitfire Audio - X");
            if (path == canon(dst))
                return QString("//SERVER/Share/lib/SPITFIRE AUDIO - X/sub");
            return std::nullopt;
        });
        const QString inside = checkSourceAndDestination(src, dst + "/out");
        QVERIFY2(inside.contains("inside the source folder (reached by another path)"), qPrintable(inside));
        QVERIFY(checkSourceAndDestination(src, dst).contains("inside the source folder (reached by another path)"));
        // the same folder
        testing::setFinalPathProvider([src, dst](const QString &path) -> std::optional<QString> {
            if (path == canon(src))
                return QString("Z:\\Lib\\X");
            if (path == canon(dst))
                return QString("z:/lib/x/");
            return std::nullopt;
        });
        QVERIFY(checkSourceAndDestination(src, dst).contains("same folder as the source (reached by another path)"));
        // the source inside an existing destination
        testing::setFinalPathProvider([src, dst](const QString &path) -> std::optional<QString> {
            if (path == canon(src))
                return QString("C:\\Data\\Lib\\Spitfire Audio - X");
            if (path == canon(dst))
                return QString("C:\\data");
            return std::nullopt;
        });
        QVERIFY(checkSourceAndDestination(src, dst).contains("source folder is inside the destination"));
    }
    void windowsFinalPathsOfUnrelatedFoldersPassAndAnUnknownOneRefuses()
    {
        QTemporaryDir t;
        const QString src = mk(t, "src/Spitfire Audio - X"), dst = mk(t, "dst");
        testing::setFinalPathProvider([src, dst](const QString &path) -> std::optional<QString> {
            if (path == canon(src))
                return QString("D:\\Lib\\Spitfire Audio - X");
            if (path == canon(dst))
                return QString("C:\\Out");
            return std::nullopt;
        });
        QCOMPARE(checkSourceAndDestination(src, dst + "/Spitfire Audio - X"), QString());
        QCOMPARE(checkSourceAndDestination(src, dst), QString());
        // a folder whose real location cannot be told refuses: over-refusing is the safe error
        testing::setFinalPathProvider([src](const QString &path) -> std::optional<QString> {
            return path == canon(src) ? std::optional<QString>("D:\\Lib") : std::nullopt;
        });
        QVERIFY(checkSourceAndDestination(src, dst).contains("could not be determined"));
        testing::setFinalPathProvider([dst](const QString &path) -> std::optional<QString> {
            return path == canon(dst) ? std::optional<QString>("C:\\Out") : std::nullopt;
        });
        QVERIFY(checkSourceAndDestination(src, dst).contains("could not be determined"));
    }
    void windowsFileIdentityCatchesAnAliasTheFinalPathMisses()
    {
        QTemporaryDir t;
        const QString src = mk(t, "src/Spitfire Audio - X"), dst = mk(t, "dst");
        testing::setFileIdProvider([src, dst](const QString &path) -> std::optional<testing::FileId> {
            if (path == canon(src) || path == canon(dst))
                return testing::FileId{7, 4242}; // one volume serial and file index: one folder
            return std::nullopt;
        });
        const QString r = checkSourceAndDestination(src, dst);
        QVERIFY2(r.contains("same folder as the source (reached by another path)"), qPrintable(r));
        // different indexes: nothing found
        testing::setFileIdProvider([src, dst](const QString &path) -> std::optional<testing::FileId> {
            if (path == canon(src))
                return testing::FileId{7, 1};
            return testing::FileId{7, 2};
        });
        QCOMPARE(checkSourceAndDestination(src, dst + "/Spitfire Audio - X"), QString());
    }
    void dotDotIsRefusedInTheWindowsSpellingToo()
    {
        QVERIFY(winpath::hasDotDotSegment("C:\\a\\..\\b", true));
    }
    void emptyDestRefused()
    {
        QTemporaryDir t;
        QVERIFY(!checkSourceAndDestination(mk(t, "src"), QString()).isEmpty());
    }
    void missingSourceRefused()
    {
        QTemporaryDir t;
        QVERIFY(!checkSourceAndDestination(t.filePath("nope"), mk(t, "dst")).isEmpty());
        QVERIFY(!checkSourceAndDestination(QString(), mk(t, "dst")).isEmpty());
    }
    void sameDirRefused()
    {
        QTemporaryDir t;
        const QString s = mk(t, "src");
        QVERIFY(!checkSourceAndDestination(s, s).isEmpty());
        QVERIFY(!checkSourceAndDestination(s, s + "/").isEmpty());
    }
    void sameDirByFirmlinkRefused()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "src");
        const QString alias = "/System/Volumes/Data" + QFileInfo(s).canonicalFilePath();
        if (!QFileInfo(alias).isDir())
            QSKIP("no /System/Volumes/Data firmlink alias here");
        QVERIFY(!checkSourceAndDestination(s, alias).isEmpty());
        QVERIFY(!checkSourceAndDestination(s, alias + "/new/deeper").isEmpty());
        QVERIFY(!checkSourceAndDestination(s, mk(t, "src/out").replace(s, alias)).isEmpty());
        // the source inside the destination
        QVERIFY(!checkSourceAndDestination(s, "/System/Volumes/Data" + QFileInfo(t.path()).canonicalFilePath()).isEmpty());
        // a sibling is still fine
        QVERIFY(checkSourceAndDestination(s, "/System/Volumes/Data" + QFileInfo(mk(t, "dst")).canonicalFilePath()).isEmpty());
    }
    void destInsideSourceRefused()
    {
        QTemporaryDir t;
        const QString s = mk(t, "src");
        QVERIFY(!checkSourceAndDestination(s, mk(t, "src/out")).isEmpty());
        // not yet created, but would land inside the source
        QVERIFY(!checkSourceAndDestination(s, s + "/new/deeper").isEmpty());
    }
    void sourceInsideDestRefused()
    {
        QTemporaryDir t;
        QVERIFY(!checkSourceAndDestination(mk(t, "dst/src"), t.filePath("dst")).isEmpty());
    }
    void symlinkToSourceRefused()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "src");
        QVERIFY(QFile::link(s, t.filePath("lnk")));
        QVERIFY(!checkSourceAndDestination(s, t.filePath("lnk")).isEmpty());
    }
    void symlinkIntoSourceRefused()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "src");
        mk(t, "src/sub");
        QVERIFY(QFile::link(s + "/sub", t.filePath("lnk")));
        QVERIFY(!checkSourceAndDestination(s, t.filePath("lnk")).isEmpty());
    }
    void siblingDirsAccepted()
    {
        QTemporaryDir t;
        QCOMPARE(checkSourceAndDestination(mk(t, "b"), mk(t, "bc")), QString());
        QCOMPARE(checkSourceAndDestination(mk(t, "bc"), mk(t, "b")), QString());
    }
    void unwritableDestRefused()
    {
        SMT_UNIX_ONLY();
#ifndef Q_OS_WIN
        if (geteuid() == 0)
            QSKIP("root ignores permissions");
#endif
        QTemporaryDir t;
        const QString d = mk(t, "dst");
        const QString s = mk(t, "src");
        QVERIFY(QFile::setPermissions(d, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        const bool refused = !checkSourceAndDestination(s, d).isEmpty();
        QFile::setPermissions(d, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QVERIFY(refused);
    }
    void nonExistentDestParentCreatedLater()
    {
        QTemporaryDir t;
        QCOMPARE(checkSourceAndDestination(mk(t, "src"), t.filePath("newdst")), QString());
        QCOMPARE(checkSourceAndDestination(mk(t, "src"), t.filePath("a/b/newdst")), QString());
    }
    void dotDotAfterSymlinkRefused()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "src");
        mk(t, "src/sub");
        QVERIFY(QFile::link(s + "/sub", t.filePath("lnk")));
        QVERIFY(!checkSourceAndDestination(s, t.filePath("lnk/../out")).isEmpty());
        QVERIFY(!checkSourceAndDestination(s, t.filePath("lnk/..")).isEmpty());
    }
    void symlinkIntoSourceThenMissingTailRefused()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "src");
        mk(t, "src/sub");
        QVERIFY(QFile::link(s + "/sub", t.filePath("lnk")));
        QVERIFY(!checkSourceAndDestination(s, t.filePath("lnk/new")).isEmpty());
    }
    void brokenSymlinkDestRefused()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        QVERIFY(QFile::link(t.filePath("gone"), t.filePath("lnk")));
        QVERIFY(!checkSourceAndDestination(mk(t, "src"), t.filePath("lnk")).isEmpty());
        QVERIFY(!checkSourceAndDestination(mk(t, "src"), t.filePath("lnk/new")).isEmpty());
    }
    void destIsRegularFileRefused()
    {
        QTemporaryDir t;
        QFile f(t.filePath("file"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        QVERIFY(!checkSourceAndDestination(mk(t, "src"), f.fileName()).isEmpty());
        QVERIFY(!checkSourceAndDestination(mk(t, "src"), f.fileName() + "/sub").isEmpty());
    }
    void sourceIsFileRefused()
    {
        QTemporaryDir t;
        QFile f(t.filePath("file"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        QVERIFY(!checkSourceAndDestination(f.fileName(), mk(t, "dst")).isEmpty());
    }
    void destEqualToSourceParentRefused()
    {
        QTemporaryDir t;
        QVERIFY(!checkSourceAndDestination(mk(t, "src"), t.path()).isEmpty());
    }
    void caseVariantOfSourceRefused()
    {
        QTemporaryDir t;
        QVERIFY(!checkSourceAndDestination(mk(t, "src"), t.filePath("SRC")).isEmpty());
        QVERIFY(!checkSourceAndDestination(mk(t, "src"), t.filePath("SRC/x")).isEmpty());
    }
    // Two mounts of one share: t/m1 and t/m2 are the same share root, as /Volumes/Share
    // and /Volumes/Share-1 would be.
    void sameShareSecondMountEqualRefused()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "m1/Lib"), d = mk(t, "m2/Lib");
        fakeMounts({{canon(t.filePath("m1")), {"//u@srv/Share", ""}}, {canon(t.filePath("m2")), {"//u@srv/Share", ""}}});
        const QString why = checkSourceAndDestination(s, d);
        QVERIFY2(why.contains("mounted twice"), qPrintable(why));
    }
    void sameShareDestInsideSourceRefused()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "m1/Lib");
        mk(t, "m2/Lib/Samples");
        fakeMounts({{canon(t.filePath("m1")), {"//u@srv/Share", ""}}, {canon(t.filePath("m2")), {"//u@srv/Share", ""}}});
        QVERIFY(!checkSourceAndDestination(s, t.filePath("m2/Lib/Samples")).isEmpty());
        QVERIFY(!checkSourceAndDestination(s, t.filePath("m2/Lib/new/out")).isEmpty()); // missing tail
    }
    void sameShareDestContainsSourceRefused()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "m1/Lib");
        mk(t, "m2/Lib");
        fakeMounts({{canon(t.filePath("m1")), {"//u@srv/Share", ""}}, {canon(t.filePath("m2")), {"//u@srv/Share", ""}}});
        QVERIFY(!checkSourceAndDestination(s, t.filePath("m2")).isEmpty());
    }
    void differentSharesAccepted()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "m1/Lib"), d = mk(t, "m2/Lib");
        fakeMounts({{canon(t.filePath("m1")), {"//u@srv/Share", ""}}, {canon(t.filePath("m2")), {"//u@srv/Other", ""}}});
        QCOMPARE(checkSourceAndDestination(s, d), QString());
        QCOMPARE(checkSourceAndDestination(s, d + "/new"), QString());
    }
    void sameVolumeSiblingsAccepted()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "A");
        fakeMounts({{canon(t.path()), {"/dev/disk3s5", "Users/me"}}});
        for (const QString &d : {mk(t, "A2"), mk(t, "A-1"), t.filePath("A-1/new"), mk(t, "B")})
            QCOMPARE(checkSourceAndDestination(s, d), QString());
        QCOMPARE(checkSourceAndDestination(mk(t, "A2"), s), QString());
    }
    void statfsFailureFallsThrough()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "m1/Lib"), d = mk(t, "m2/Lib");
        testing::setMountIdProvider([](const QString &) { return std::optional<MountId>(); });
        QCOMPARE(checkSourceAndDestination(s, d), QString());
        QVERIFY(!checkSourceAndDestination(s, s + "/sub").isEmpty()); // the path checks still run
        // only one side known: no verdict from mounts either
        fakeMounts({{canon(t.filePath("m1")), {"//u@srv/Share", ""}}});
        QCOMPARE(checkSourceAndDestination(s, d), QString());
    }
    void firmlinkStyleSameVolumeAliasRefused()
    {
        SMT_UNIX_ONLY();
        // /Users/me/Lib and /System/Volumes/Data/Users/me/Lib: one volume, two path spellings.
        QTemporaryDir t;
        const QString s = mk(t, "users/me/Lib");
        mk(t, "data/Users/me/Lib");
        fakeMounts({{canon(t.filePath("users")), {"/dev/disk3s5", "Users"}}, {canon(t.filePath("data")), {"/dev/disk3s5", ""}}});
        QVERIFY(!checkSourceAndDestination(s, t.filePath("data/Users/me/Lib")).isEmpty());
        QVERIFY(!checkSourceAndDestination(s, t.filePath("data/Users/me/Lib/out")).isEmpty());
        QCOMPARE(checkSourceAndDestination(s, mk(t, "data/Users/me/Lib2")), QString());
    }
    // The real statfs on this machine, no injection.
    void realStatfsSmoke()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString a = canon(mk(t, "a")), b = canon(mk(t, "b"));
        const auto ma = testing::realMountId(a), mb = testing::realMountId(b);
#if defined(Q_OS_MACOS) || defined(Q_OS_BSD4)
        QVERIFY(ma && mb);
        QVERIFY(!ma->from.isEmpty());
        QCOMPARE(ma->from, mb->from);
        QVERIFY2(ma->rel.endsWith("/a") && mb->rel.endsWith("/b"), qPrintable(ma->rel + " " + mb->rel));
        QCOMPARE(ma->rel.chopped(1), mb->rel.chopped(1));
        const QString alias = "/System/Volumes/Data" + a;
        if (QFileInfo(alias).isDir()) {
            const auto mAlias = testing::realMountId(alias);
            QVERIFY(mAlias);
            QCOMPARE(mAlias->from, ma->from);
            QCOMPARE(mAlias->rel, ma->rel);
            // a case variant of the alias: canonicalFilePath restores the on-disk case
            const auto mCase = testing::realMountId(canon("/SYSTEM/volumes/DATA" + a));
            QVERIFY(mCase);
            QCOMPARE(mCase->rel, ma->rel);
        }
#else
        QVERIFY(!ma && !mb);
#endif
        QCOMPARE(checkSourceAndDestination(a, b), QString());
        QCOMPARE(checkSourceAndDestination(a, mk(t, "a2")), QString());
        QVERIFY(!checkSourceAndDestination(a, a).isEmpty());
    }
    void mountSourceComparedCaseInsensitively()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "m1/Lib"), d = mk(t, "m2/Lib");
        fakeMounts({{canon(t.filePath("m1")), {"//U@SRV.local/Share", ""}}, {canon(t.filePath("m2")), {"//u@srv.LOCAL/share", ""}}});
        QVERIFY(!checkSourceAndDestination(s, d).isEmpty());
    }
    // Second mount with a differently spelled mount source: only inode + birth time tell.
    void statIdentitySameFolderOtherMountRefused()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "m1/Lib");
        mk(t, "m2/Lib");
        fakeStatMounts(canon(t.filePath("m1")), canon(t.filePath("m2")));
        QVERIFY(checkSourceAndDestination(s, t.filePath("m2/Lib")).contains("another mount"));
        QVERIFY(!checkSourceAndDestination(s, t.filePath("m2/Lib/new/out")).isEmpty()); // new folder in it
    }
    void statIdentityDestInsideSourceRefused()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "m1/Lib");
        mk(t, "m1/Lib/Samples/_Brass");
        mk(t, "m2/Lib/Samples/_Brass");
        fakeStatMounts(canon(t.filePath("m1")), canon(t.filePath("m2")));
        QVERIFY(!checkSourceAndDestination(s, t.filePath("m2/Lib/Samples")).isEmpty());
        QVERIFY(!checkSourceAndDestination(s, t.filePath("m2/Lib/Samples/_Brass/new")).isEmpty());
    }
    void statIdentityDestContainsSourceRefused()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "m1/Lib");
        mk(t, "m2/Lib");
        fakeStatMounts(canon(t.filePath("m1")), canon(t.filePath("m2")));
        QVERIFY(!checkSourceAndDestination(s, t.filePath("m2")).isEmpty());
    }
    void statIdentityDifferentFoldersAccepted()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "m1/Lib");
        fakeStatMounts(canon(t.filePath("m1")), canon(t.filePath("m2")));
        for (const QString &d : {mk(t, "m2/Lib2"), mk(t, "m2/Other/Lib"), t.filePath("m2/Other/new"), mk(t, "m1/Lib-1")})
            QCOMPARE(checkSourceAndDestination(s, d), QString());
    }
    // The real stat on this machine, no injection: ordinary folders are never one object.
    void realStatIdentitySmoke()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString a = mk(t, "a"), b = mk(t, "b");
        QCOMPARE(checkSourceAndDestination(a, b), QString());
        QCOMPARE(checkSourceAndDestination(b, a), QString());
        QCOMPARE(checkSourceAndDestination(a, t.filePath("b/new")), QString());
        // a copied library: new inode and birth time
        mk(t, "lib/Samples/_Brass");
        QFile f(t.filePath("lib/Samples/_Brass/x.db"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        mk(t, "copy/Samples/_Brass");
        QVERIFY(QFile::copy(f.fileName(), t.filePath("copy/Samples/_Brass/x.db")));
        QCOMPARE(checkSourceAndDestination(t.filePath("lib"), t.filePath("copy")), QString());
        QCOMPARE(checkSourceAndDestination(t.filePath("lib"), t.filePath("copy/Samples")), QString());
#if defined(Q_OS_MACOS) || defined(Q_OS_BSD4)
        const auto ia = testing::realStatId(a), ib = testing::realStatId(b);
        QVERIFY(ia && ib);
        QVERIFY(ia->birthSec != 0 || ia->birthNsec != 0);
        QVERIFY(ia->ino != ib->ino);
        // a source on another volume (every volume root has inode 2 on APFS: birth time tells)
        bool otherVolume = false;
        for (const QString &other : {QStringLiteral("/System/Volumes/Preboot"), QStringLiteral("/System/Volumes/VM")}) {
            const auto io = testing::realStatId(other);
            if (!io || io->dev == ia->dev)
                continue;
            otherVolume = true;
            QCOMPARE(checkSourceAndDestination(other, b), QString());
        }
        if (!otherVolume)
            QSKIP("no readable directory on another local volume");
#endif
    }
    void statIdentitySameInodeDifferentBirthAccepted()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "m1/Lib"), d = mk(t, "m2/Lib");
        const QString m1 = canon(t.filePath("m1"));
        testing::setStatIdProvider([m1](const QString &path) -> std::optional<testing::StatId> {
            const QString p = QFileInfo(path).canonicalFilePath();
            return testing::StatId{p.startsWith(m1 + '/') ? 1u : 2u, 42, p.startsWith(m1 + '/') ? 100 : 200, 0};
        });
        QCOMPARE(checkSourceAndDestination(s, d), QString());
        QCOMPARE(checkSourceAndDestination(s, d + "/new"), QString());
    }
    void statIdentityNoBirthTimeFallsThrough()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir t;
        const QString s = mk(t, "m1/Lib"), d = mk(t, "m2/Lib");
        testing::setStatIdProvider([](const QString &) { return std::optional<testing::StatId>(); });
        QCOMPARE(checkSourceAndDestination(s, d), QString());
        QVERIFY(!checkSourceAndDestination(s, s + "/sub").isEmpty()); // the path checks still run
    }
    void destinationNameMustMatchSpitfireProductFolder()
    {
        const QString src = "/a/Spitfire Audio - BBC Symphony Orchestra";
        QCOMPARE(checkDestinationName(src, "/b/Spitfire Audio - BBC Symphony Orchestra/"), QString());
        QVERIFY(!checkDestinationName(src, "/b/TEST - Spitfire Audio - BBC Symphony Orchestra").isEmpty());
        QVERIFY(checkDestinationName(src, "/b/x").contains("Spitfire Audio - BBC Symphony Orchestra"));
        QCOMPARE(checkDestinationName("/a/other", "/b/x"), QString()); // not a Spitfire product folder
    }

    void resolveDestinationFolderUsesTheExactNameOrCreatesAChildWithIt()
    {
        const QString src = "/lib/Spitfire Audio - BBC Symphony Orchestra";
        const QString name = "Spitfire Audio - BBC Symphony Orchestra";
        // the exact name: used as it is (also with a trailing slash, which the dialog may give)
        QCOMPARE(resolveDestinationFolder(src, "/b/" + name), "/b/" + name);
        QCOMPARE(resolveDestinationFolder(src, "/b/" + name + "/"), "/b/" + name + "/");
        // another name: a folder with the exact name inside it
        QCOMPARE(resolveDestinationFolder(src, "/b/Trimmed"), "/b/Trimmed/" + name);
        QCOMPARE(resolveDestinationFolder(src, "/b/Trimmed/"), "/b/Trimmed/" + name);
        QCOMPARE(resolveDestinationFolder(src + "/", "/b/Trimmed"), "/b/Trimmed/" + name); // a slash on the source
        // a look-alike is not "the exact name"
        QCOMPARE(resolveDestinationFolder(src, "/b/TEST - " + name), "/b/TEST - " + name + "/" + name);
        // inside a correctly named parent: still a child
        QCOMPARE(resolveDestinationFolder(src, "/b/" + name + "/Sub"), "/b/" + name + "/Sub/" + name);
        // no constraint when the source is no Spitfire product folder, or nothing is known yet
        QCOMPARE(resolveDestinationFolder("/a/other", "/b/x"), QString("/b/x"));
        QCOMPARE(resolveDestinationFolder(QString(), "/b/x"), QString("/b/x"));
        QCOMPARE(resolveDestinationFolder(src, QString()), QString());
        // the result passes the name check, and the source's own parent resolves to the source: an overlap
        QCOMPARE(checkDestinationName(src, resolveDestinationFolder(src, "/b/Trimmed")), QString());
        // choosing the source's own parent resolves to the source itself: the overlap check refuses it
        QTemporaryDir t;
        const QString real = t.filePath(name);
        QVERIFY(QDir().mkpath(real));
        QCOMPARE(resolveDestinationFolder(real, t.path()), real);
        QVERIFY(checkSourceAndDestination(real, resolveDestinationFolder(real, t.path())).contains("same folder"));
    }
};

QTEST_APPLESS_MAIN(TstPathSafety)
#include "tst_pathsafety.moc"
