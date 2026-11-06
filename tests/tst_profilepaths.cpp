#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "profilepaths.h"

class TstProfilePaths : public QObject
{
    Q_OBJECT
    QTemporaryDir mDir;
    const QString bbcso = "name.mbishop.smtprofile.bbcso";
    const QString choir = "name.mbishop.smtprofile.choir";

private slots:
    void init()
    {
        // Never the user's real settings: a fresh INI store in a temp dir.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, mDir.path());
        QSettings().clear();
        QSettings().sync();
    }

    void independentPerProfile()
    {
        ProfilePaths p;
        p.setSource(bbcso, "/a");
        p.setDestination(bbcso, "/b");
        p.setSource(choir, "/c");
        QCOMPARE(p.source(bbcso), QString("/a"));
        QCOMPARE(p.destination(bbcso), QString("/b"));
        QCOMPARE(p.source(choir), QString("/c"));
        QCOMPARE(p.destination(choir), QString());
    }
    void unknownProfileIsEmpty() { QCOMPARE(ProfilePaths().source("nope"), QString()); }
    void legacyOnlyForBbcsoAndUntilSet()
    {
        QSettings s;
        s.setValue("SourceDirectory", "/old-src");
        s.setValue("BackupDirectory", "/old-dst");
        ProfilePaths p;
        QCOMPARE(p.source(bbcso), QString("/old-src"));
        QCOMPARE(p.destination(bbcso), QString("/old-dst"));
        QCOMPARE(p.source(choir), QString());
        p.setSource(bbcso, "/new");
        QCOMPARE(p.source(bbcso), QString("/new"));
        QCOMPARE(p.destination(bbcso), QString("/old-dst"));
        p.setSource(bbcso, ""); // an explicitly stored empty value wins over the legacy one
        QCOMPARE(p.source(bbcso), QString());
        QCOMPARE(QSettings().value("SourceDirectory").toString(), QString("/old-src"));
    }
    void lastProfileRoundTrip()
    {
        ProfilePaths p;
        QCOMPARE(p.lastProfile(), QString());
        p.setLastProfile(choir);
        QCOMPARE(ProfilePaths().lastProfile(), choir);
    }
    void idsWithDotsAndSlashesRoundTrip()
    {
        ProfilePaths p;
        p.setSource("a.b/c", "/x");
        p.setSource("a.b", "/y");
        QCOMPARE(p.source("a.b/c"), QString("/x"));
        QCOMPARE(p.source("a.b"), QString("/y"));
        const QStringList keys = QSettings().allKeys();
        QVERIFY(keys.contains("Profiles/a%2Eb%2Fc/SourceDirectory"));
        for (const QString &k : keys)
            QCOMPARE(k.section('/', 1, -2).count('.'), 0); // no raw dot in the id part
    }
};

QTEST_GUILESS_MAIN(TstProfilePaths)
#include "tst_profilepaths.moc"
