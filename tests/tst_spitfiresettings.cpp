#include <QtTest>
#include "profileregistry.h"
#include "spitfiresettings.h"

using namespace smt;

class TstSpitfireSettings : public QObject
{
    Q_OBJECT
    static QString write(const QTemporaryDir &t, const QByteArray &json)
    {
        const QString f = t.filePath("Spitfire.properties");
        QFile file(f);
        if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size())
            qFatal("cannot write the test file");
        return f;
    }
    static QByteArray readRaw(const QString &f)
    {
        QFile file(f);
        if (!file.open(QIODevice::ReadOnly))
            qFatal("cannot read the test file");
        return file.readAll();
    }
    static QJsonObject read(const QString &f) { return QJsonDocument::fromJson(readRaw(f)).object(); }
    static QString mkdir(const QTemporaryDir &t, const QString &rel)
    {
        QDir().mkpath(t.filePath(rel));
        return t.filePath(rel);
    }
    static QStringList list(const QJsonObject &entry, const QString &key)
    {
        QStringList out;
        for (const auto &v : entry[key].toArray())
            out << QDir::fromNativeSeparators(v.toString()); // Windows writes backslashes
        return out;
    }

private slots:
    void settingsDirEnvOverrideWinsEverywhere()
    {
        const auto no = [](const QString &) { return false; };
        QCOMPARE(testing::pickSpitfireSettingsDir("/x", {"/a", "/b"}, false, no), QString("/x"));
        QCOMPARE(testing::pickSpitfireSettingsDir("/x", {"/a", "/b"}, true, no), QString("/x"));
    }
    void settingsDirWindowsPicksTheFirstCandidateWithProperties()
    {
        QTemporaryDir t;
        const QString music = mkdir(t, "Music/Spitfire Audio/Settings"), docs = mkdir(t, "Documents/Spitfire Audio/Settings");
        const auto has = [](const QString &d) { return QFileInfo::exists(d + "/Spitfire.properties"); };
        QCOMPARE(testing::pickSpitfireSettingsDir({}, {music, docs}, true, has), music); // none: the first
        QVERIFY(QFile(docs + "/Spitfire.properties").open(QIODevice::WriteOnly));
        QCOMPARE(testing::pickSpitfireSettingsDir({}, {music, docs}, true, has), docs);
        QVERIFY(QFile(music + "/Spitfire.properties").open(QIODevice::WriteOnly));
        QCOMPARE(testing::pickSpitfireSettingsDir({}, {music, docs}, true, has), music);
        // off Windows only the first is ever used
        QCOMPARE(testing::pickSpitfireSettingsDir({}, {docs, music}, false, has), docs);
    }
    void windowsFlavourWritesNativeSeparatorsAndLeavesAnEquivalentSpellingAlone()
    {
        QTemporaryDir t;
        const QString lib = mkdir(t, "New/Spitfire Audio - BBC Symphony Orchestra");
        const QString f = write(t, "{\"BBC Symphony Orchestra\":{\"config\":[\"C:\\\\old\\\\Config\"],\"patches\":[],\"presets\":[],\"samples\":[]}}");
        const auto r = testing::repointLibraryFor(true, f, builtInProfile(kBbcsoProfileId), lib);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY(r.written);
        // the raw value: list() would turn the backslashes back into "/"
        const QString written = read(f)["BBC Symphony Orchestra"].toObject()["samples"].toArray().at(0).toString();
        QCOMPARE(written, QString(lib + "/Samples").replace('/', '\\'));
        // the same folder spelled with forward slashes and another case: not rewritten
        const QString before = QString::fromUtf8(readRaw(f));
        QVERIFY(!testing::repointLibraryFor(true, f, builtInProfile(kBbcsoProfileId), lib.toUpper()).written
                || !QFileInfo(lib.toUpper()).isDir()); // (a case-sensitive file system cannot open the upper-cased folder)
        QCOMPARE(QString::fromUtf8(readRaw(f)), before);
    }
    void windowsFlavourRefusesDriveAndShareRoots()
    {
        QTemporaryDir t;
        const QString f = write(t, "{\"BBC Symphony Orchestra\":{}}");
        for (const char *root : {"C:/", "C:\\", "//server/share"}) {
            const auto r = testing::repointLibraryFor(true, f, builtInProfile(kBbcsoProfileId), root);
            QVERIFY2(!r.ok(), root);
        }
    }
    void bbcsoPointsAtTheFolderWhateverItPointedAtBefore()
    {
        QTemporaryDir t;
        const QString lib = mkdir(t, "New/Spitfire Audio - BBC Symphony Orchestra");
        // The user's case: the entry points at an unrelated older folder.
        const QString f = write(t, "{\"BBC Symphony Orchestra\":{\"config\":[\"/old/place/Config\"],\"patches\":[\"/old/place/Patches\"],"
                                   "\"presets\":[\"/old/place/Presets\"],\"samples\":[\"/old/place/Samples\"]},"
                                   "\"Other\":{\"samples\":[\"/lib/Spitfire Audio - Other/Samples\"]}}");
        const auto r = repointLibrary(f, builtInProfile(kBbcsoProfileId), lib);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.products, QStringList{"BBC Symphony Orchestra"});
        const auto o = read(f);
        const auto bbc = o["BBC Symphony Orchestra"].toObject();
        QCOMPARE(list(bbc, "config"), QStringList{lib + "/Config"});
        QCOMPARE(list(bbc, "patches"), QStringList{lib + "/Patches"});
        QCOMPARE(list(bbc, "presets"), QStringList{lib + "/Presets"});
        QCOMPARE(list(bbc, "samples"), QStringList{lib + "/Samples"});
        QCOMPARE(list(o["Other"].toObject(), "samples"), QStringList{"/lib/Spitfire Audio - Other/Samples"}); // untouched
    }
    void whitacreRepointsAllThreeEntriesWithTheirSubPathsAndLeavesOthers()
    {
        QTemporaryDir t;
        const QString lib = mkdir(t, "Spitfire Audio - Eric Whitacre Choir");
        const QString old = "/old-volume/Spitfire Audio - Eric Whitacre Choir";
        auto arr = [&](const QStringList &subs) {
            QStringList q;
            for (const QString &s : subs)
                q << "\"" + old + "/" + s + "\"";
            return q.join(",");
        };
        const QByteArray original = QString(
            "{\"Eric Whitacre Choir\":{\"patches\":[%1],\"presets\":[%2],\"samples\":[%3],\"other\":[\"keep\"]},"
            "\"Eric Whitacre Choir Evo Grid\":{\"patches\":[%4],\"presets\":[%5],\"samples\":[%3]},"
            "\"Eric Whitacre Choir Samples\":{\"samples\":[%3]},"
            "\"BBC Symphony Orchestra\":{\"samples\":[\"/bbc/Samples\"]},"
            "\"Labs\":{\"samples\":[\"/labs/Samples\"]}}")
            .arg(arr({"Eric Whitacre Choir", "Patches", "Patches/Eric Whitacre Choir"}),
                 arr({"Eric Whitacre Choir", "Presets", "Presets/Eric Whitacre Choir"}), arr({"Samples"}),
                 arr({"Eric Whitacre Choir Evo Grid", "Patches/Eric Whitacre Choir Evo Grid"}),
                 arr({"Eric Whitacre Choir Evo Grid", "Presets/Eric Whitacre Choir Evo Grid"}))
            .toUtf8();
        const QString f = write(t, original);
        const auto r = repointLibrary(f, builtInProfile(kEwcProfileId), lib);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.products, (QStringList{"Eric Whitacre Choir", "Eric Whitacre Choir Evo Grid", "Eric Whitacre Choir Samples"}));
        const auto o = read(f);
        const auto e = o["Eric Whitacre Choir"].toObject();
        QCOMPARE(list(e, "patches"), (QStringList{lib + "/Eric Whitacre Choir", lib + "/Patches", lib + "/Patches/Eric Whitacre Choir"}));
        QCOMPARE(list(e, "presets"), (QStringList{lib + "/Eric Whitacre Choir", lib + "/Presets", lib + "/Presets/Eric Whitacre Choir"}));
        QCOMPARE(list(e, "samples"), QStringList{lib + "/Samples"});
        QCOMPARE(list(e, "other"), QStringList{"keep"}); // a key the profile does not list
        const auto evo = o["Eric Whitacre Choir Evo Grid"].toObject();
        QCOMPARE(list(evo, "patches"), (QStringList{lib + "/Eric Whitacre Choir Evo Grid", lib + "/Patches/Eric Whitacre Choir Evo Grid"}));
        QCOMPARE(list(evo, "presets"), (QStringList{lib + "/Eric Whitacre Choir Evo Grid", lib + "/Presets/Eric Whitacre Choir Evo Grid"}));
        QCOMPARE(list(evo, "samples"), QStringList{lib + "/Samples"});
        QCOMPARE(list(o["Eric Whitacre Choir Samples"].toObject(), "samples"), QStringList{lib + "/Samples"});
        // other products are untouched
        QCOMPARE(list(o["BBC Symphony Orchestra"].toObject(), "samples"), QStringList{"/bbc/Samples"});
        QCOMPARE(list(o["Labs"].toObject(), "samples"), QStringList{"/labs/Samples"});
        QCOMPARE(readRaw(f + ".smt-backup"), original);
    }
    void aSubPathOfTheFolderItselfIsWritten()
    {
        QTemporaryDir t;
        const QString lib = mkdir(t, "Spitfire Audio - BBC Symphony Orchestra");
        ProductProfile p = builtInProfile(kBbcsoProfileId);
        p.spitfireEntries = {{"P", {{"samples", {QString(), "Samples"}}}}};
        const QString f = write(t, "{\"P\":{\"samples\":[\"/x\"]}}");
        QVERIFY(repointLibrary(f, p, lib + "/").ok()); // a trailing slash is cleaned
        QCOMPARE(list(read(f)["P"].toObject(), "samples"), (QStringList{lib, lib + "/Samples"}));
    }
    void backsUpTheOriginalAndIsIdempotent()
    {
        QTemporaryDir t;
        const QString lib = mkdir(t, "Spitfire Audio - BBC Symphony Orchestra");
        const QByteArray original = "{\"BBC Symphony Orchestra\":{\"config\":[\"/a\"],\"patches\":[\"/a\"],\"presets\":[\"/a\"],\"samples\":[\"/a\"]}}";
        const QString f = write(t, original);
        const ProductProfile p = builtInProfile(kBbcsoProfileId);
        QVERIFY(repointLibrary(f, p, lib).ok());
        QCOMPARE(readRaw(f + ".smt-backup"), original);
        const QByteArray once = readRaw(f);
        QVERIFY(once != original);
        QVERIFY(repointLibrary(f, p, lib).ok()); // again: same file, and the backup is still the original
        QCOMPARE(readRaw(f), once);
        QCOMPARE(readRaw(f + ".smt-backup"), original);
    }
    void missingEntryOrBadInputWritesNothing()
    {
        QTemporaryDir t;
        const QString lib = mkdir(t, "Spitfire Audio - BBC Symphony Orchestra");
        const ProductProfile p = builtInProfile(kBbcsoProfileId);
        // the entry the profile lists is not in the file
        const QByteArray other = "{\"Other\":{\"samples\":[\"/o\"]}}";
        QString f = write(t, other);
        const auto r = repointLibrary(f, p, lib);
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains("BBC Symphony Orchestra"), qPrintable(r.error));
        QCOMPARE(readRaw(f), other);
        QVERIFY(!QFileInfo::exists(f + ".smt-backup"));
        // corrupt JSON stays as it is
        const QByteArray bad = "not json";
        f = write(t, bad);
        QVERIFY(!repointLibrary(f, p, lib).ok());
        QCOMPARE(readRaw(f), bad);
        // no file, no folder, a profile without entries
        QVERIFY(!repointLibrary(t.filePath("missing"), p, lib).ok());
        f = write(t, "{\"BBC Symphony Orchestra\":{}}");
        QVERIFY(!repointLibrary(f, p, t.filePath("nodir")).ok());
        ProductProfile none = p;
        none.spitfireEntries.clear();
        const auto nr = repointLibrary(f, none, lib);
        QVERIFY(!nr.ok());
        QVERIFY2(nr.error.contains("does not describe"), qPrintable(nr.error));
        QVERIFY(!QFileInfo::exists(f + ".smt-backup"));
    }
    void theFirstBackupIsNeverOverwritten()
    {
        QTemporaryDir t;
        const QString lib1 = mkdir(t, "a/Spitfire Audio - BBC Symphony Orchestra"), lib2 = mkdir(t, "b/Spitfire Audio - BBC Symphony Orchestra");
        const QByteArray original = "{\"BBC Symphony Orchestra\":{\"config\":[\"/o\"],\"patches\":[\"/o\"],\"presets\":[\"/o\"],\"samples\":[\"/o\"]}}";
        const QString f = write(t, original);
        const ProductProfile p = builtInProfile(kBbcsoProfileId);
        auto r = repointLibrary(f, p, lib1);
        QVERIFY(r.ok() && r.written);
        QCOMPARE(r.backup, f + ".smt-backup");
        r = repointLibrary(f, p, lib2); // a second, different change
        QVERIFY(r.ok() && r.written);
        QCOMPARE(readRaw(f + ".smt-backup"), original); // still the very first original
        QVERIFY(list(read(f)["BBC Symphony Orchestra"].toObject(), "config").first().startsWith(lib2));
        r = repointLibrary(f, p, lib2); // nothing to change
        QVERIFY(r.ok() && !r.written);
        QCOMPARE(r.backup, f + ".smt-backup");
    }
    void aFileThatAlreadyPointsThereIsNotWrittenNorBackedUp()
    {
        QTemporaryDir t;
        const QString lib = mkdir(t, "Spitfire Audio - BBC Symphony Orchestra");
        const ProductProfile p = builtInProfile(kBbcsoProfileId);
        const QString f = write(t, "{\"BBC Symphony Orchestra\":{\"samples\":[\"/x\"]}}");
        QVERIFY(repointLibrary(f, p, lib).ok());
        QFile::remove(f + ".smt-backup");
        const auto r = repointLibrary(f, p, lib);
        QVERIFY(r.ok() && !r.written && r.backup.isEmpty());
        QVERIFY(!QFileInfo::exists(f + ".smt-backup"));
    }
    void anotherProductsFolderIsRefusedWithTheFileUnchanged()
    {
        QTemporaryDir t;
        const QString labs = mkdir(t, "Spitfire Originals - Labs");
        const QByteArray original = "{\"BBC Symphony Orchestra\":{\"samples\":[\"/x\"]}}";
        const QString f = write(t, original);
        const auto r = repointLibrary(f, builtInProfile(kBbcsoProfileId), labs);
        QVERIFY(!r.ok());
        QVERIFY2(r.error.contains("Spitfire Originals - Labs") && r.error.contains("BBC Symphony Orchestra")
                     && r.error.contains("must start with") && r.error.contains("matching profile"), qPrintable(r.error));
        QCOMPARE(readRaw(f), original);
        QVERIFY(!QFileInfo::exists(f + ".smt-backup"));
        // the file system root never becomes "//Samples"
        const auto root = repointLibrary(f, builtInProfile(kBbcsoProfileId), "/");
        QVERIFY(!root.ok());
        QCOMPARE(readRaw(f), original);
    }
    void aTrailingSlashAndUnicodeAndUnlistedEntriesSurviveReserialising()
    {
        QTemporaryDir t;
        const QString lib = mkdir(t, "Spitfire Audio - BBC Symphony Orchestra");
        const QByteArray original = QString::fromUtf8(
            "{\"Zeta Über\":{\"samples\":[\"/z/Sämples ♪\"],\"nested\":{\"a\":[1,2,{\"b\":null}]}},"
            "\"BBC Symphony Orchestra\":{\"samples\":[\"/x\"],\"extra\":true},\"Alpha\":{\"k\":[\"v\"]}}").toUtf8();
        const QString f = write(t, original);
        const QJsonObject before = read(f);
        QVERIFY(repointLibrary(f, builtInProfile(kBbcsoProfileId), lib + "/").ok()); // trailing slash
        const QJsonObject after = read(f);
        QCOMPARE(after["Zeta \u00dcber"], before["Zeta \u00dcber"]);
        QCOMPARE(after["Alpha"], before["Alpha"]);
        QCOMPARE(after["BBC Symphony Orchestra"].toObject()["extra"], QJsonValue(true));
        QCOMPARE(list(after["BBC Symphony Orchestra"].toObject(), "samples"), QStringList{lib + "/Samples"});
        QVERIFY(!readRaw(f).contains("//Samples"));
    }
    void aSecondMissingEntryStopsBeforeAnyWrite()
    {
        QTemporaryDir t;
        const QString lib = mkdir(t, "Spitfire Audio - BBC Symphony Orchestra");
        ProductProfile p = builtInProfile(kBbcsoProfileId);
        p.spitfireEntries << SpitfireEntry{"Missing", {{"samples", {"Samples"}}}};
        const QByteArray original = "{\"BBC Symphony Orchestra\":{\"samples\":[\"/a\"]}}";
        const QString f = write(t, original);
        QVERIFY(!repointLibrary(f, p, lib).ok());
        QCOMPARE(readRaw(f), original);
        QVERIFY(!QFileInfo::exists(f + ".smt-backup"));
    }
};

QTEST_APPLESS_MAIN(TstSpitfireSettings)
#include "tst_spitfiresettings.moc"
