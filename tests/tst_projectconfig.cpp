#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include "profileregistry.h"
#include "projectconfig.h"

using namespace smt;

static void expectRejected(const QByteArray &json)
{
    QTemporaryDir d;
    QFile f(d.filePath("project.smt"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(json);
    f.close();
    QString err;
    QVERIFY(!loadProjectConfig(d.path(), builtInProfile(kBbcsoProfileId), &err).has_value());
    QVERIFY(!err.isEmpty());
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), json);
}


class TstProjectConfig : public QObject
{
    Q_OBJECT
private slots:
    void roundTrip()
    {
        const ProductProfile p = builtInProfile(kBbcsoProfileId);
        QTemporaryDir d;
        MicStatuses m;
        m["cl01"]["strings"] = false;
        m["mi01"]["brass"] = true;
        QString err;
        QVERIFY2(saveProjectConfig(d.path(), p, m, &err), qPrintable(err));
        QVERIFY(QFile::exists(d.filePath("project.smt")));
        const auto loaded = loadProjectConfig(d.path(), p, &err);
        QVERIFY2(loaded.has_value(), qPrintable(err));
        QVERIFY(!micEnabled(p, *loaded, {"cl01", "strings"}));
        QVERIFY(micEnabled(p, *loaded, {"mi01", "brass"}));
        QVERIFY(micEnabled(p, *loaded, {"cl01", "brass"}));
    }

    void profileMismatchRefusedAndFileKept()
    {
        QTemporaryDir d;
        QString err;
        QVERIFY(saveProjectConfig(d.path(), builtInProfile(kBbcsoProfileId), {}, &err));
        QFile f(d.filePath("project.smt"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray before = f.readAll();
        f.close();
        QVERIFY(!loadProjectConfig(d.path(), builtInProfile(kEwcProfileId), &err).has_value());
        QCOMPARE(err, QString("project.smt was made for the profile %1, not %2").arg(kBbcsoProfileId, kEwcProfileId));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), before);
    }

    void oldFileWithoutProfileLoadsAsBbcso()
    {
        QTemporaryDir d;
        QFile f(d.filePath("project.smt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"micSignals\":{\"cl01\":{\"strings\":false}}}");
        f.close();
        QString err;
        const auto b = loadProjectConfig(d.path(), builtInProfile(kBbcsoProfileId), &err);
        QVERIFY2(b.has_value(), qPrintable(err));
        QVERIFY(!micEnabled(builtInProfile(kBbcsoProfileId), *b, {"cl01", "strings"}));
        QVERIFY(!loadProjectConfig(d.path(), builtInProfile(kEwcProfileId), &err).has_value());
    }

    void savedFileNamesItsProfile()
    {
        QTemporaryDir d;
        QString err;
        QVERIFY(saveProjectConfig(d.path(), builtInProfile(kEwcProfileId), {}, &err));
        QFile f(d.filePath("project.smt"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(f.readAll()).object()["profile"].toString(), kEwcProfileId);
        QVERIFY(loadProjectConfig(d.path(), builtInProfile(kEwcProfileId), &err).has_value());
    }

    void missingFileMeansAllEnabled()
    {
        const ProductProfile p = builtInProfile(kBbcsoProfileId);
        QTemporaryDir d;
        QString err;
        const auto loaded = loadProjectConfig(d.path(), p, &err);
        QVERIFY2(loaded.has_value(), qPrintable(err));
        QVERIFY(loaded->isEmpty());
        QVERIFY(micEnabled(p, *loaded, {"cl01", "strings"}));
    }

    void corruptFileIsAnErrorNotDefaults()
    {
        QTemporaryDir d;
        const QString p = d.filePath("project.smt");
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{not json");
        f.close();
        QString err;
        QVERIFY(!loadProjectConfig(d.path(), builtInProfile(kBbcsoProfileId), &err).has_value());
        QVERIFY(!err.isEmpty());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("{not json"));
    }

    void wrongShapeMicSignalsString() { expectRejected("{\"micSignals\":\"x\"}"); }
    void wrongShapePositionNotObject()
    {
        expectRejected(QByteArray("{\"micSignals\":{\"") + builtInProfile(kBbcsoProfileId).mics.at(1).token.toUtf8() + "\":3}}");
    }
    void wrongShapeSectionNotBool()
    {
        expectRejected(QByteArray("{\"micSignals\":{\"") + builtInProfile(kBbcsoProfileId).mics.at(1).token.toUtf8() + "\":{\""
                       + builtInProfile(kBbcsoProfileId).groups.at(1).key.toUtf8() + "\":\"yes\"}}}");
    }

    void unknownKeysTolerated()
    {
        const ProductProfile p = builtInProfile(kBbcsoProfileId);
        QTemporaryDir d;
        QFile f(d.filePath("project.smt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray("{\"micSignals\":{\"bogus\":5,\"") + builtInProfile(kBbcsoProfileId).mics.at(1).token.toUtf8()
                + "\":{\"bogusSection\":\"z\",\"" + builtInProfile(kBbcsoProfileId).groups.at(1).key.toUtf8() + "\":false}}}");
        f.close();
        QString err;
        const auto m = loadProjectConfig(d.path(), p, &err);
        QVERIFY2(m.has_value(), qPrintable(err));
        QVERIFY(!micEnabled(p, *m, {p.mics.at(1).token, p.groups.at(1).key}));
    }

    void unknownKeyIsNotEnabled()
    {
        const ProductProfile p = builtInProfile(kBbcsoProfileId);
        MicStatuses m;
        QVERIFY(!micEnabled(p, m, {"", "strings"}));
        QVERIFY(!micEnabled(p, m, {"zz01", "strings"}));
        QVERIFY(!micEnabled(p, m, {"cl01", ""}));
        QVERIFY(!micEnabled(p, m, {"cl01", "tutti"}));
    }

    void lockedMicIsEnabledEvenIfFileSaysOff()
    {
        const ProductProfile p = builtInProfile(kBbcsoProfileId);
        QTemporaryDir d;
        QFile f(d.filePath("project.smt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"micSignals\":{\"mi01\":{\"brass\":false},\"mi02\":{\"brass\":false}}}");
        f.close();
        QString err;
        const auto loaded = loadProjectConfig(d.path(), p, &err);
        QVERIFY2(loaded.has_value(), qPrintable(err));
        QVERIFY(micEnabled(p, *loaded, {"mi01", "brass"}));
        QVERIFY(!micEnabled(p, *loaded, {"mi02", "brass"}));
        MicStatuses m;
        m["mi01"]["strings"] = false;
        QVERIFY(micEnabled(p, m, {"mi01", "strings"}));
    }

    void whitacreConfigRoundTrips()
    {
        const ProductProfile p = builtInProfile(kEwcProfileId);
        QTemporaryDir d;
        MicStatuses m;
        m["cl01"]["tutti"] = false;
        m["ga01"]["sopranos"] = false;
        m["cl02"]["altos"] = false; // not in the profile: not written
        m["tr01"]["brass"] = false; // no such group: not written
        m["zz01"]["tutti"] = false; // no such mic: not written
        QString err;
        QVERIFY2(saveProjectConfig(d.path(), p, m, &err), qPrintable(err));
        QFile f(d.filePath("project.smt"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject j = QJsonDocument::fromJson(f.readAll()).object()["micSignals"].toObject();
        QCOMPARE(j.size(), p.mics.size());
        QVERIFY(!j.contains("zz01"));
        QCOMPARE(j["tr01"].toObject().keys(), (QStringList{"altos", "bass", "sopranos", "tenors", "tutti"}));
        QVERIFY(!j.contains("cl02"));

        const auto loaded = loadProjectConfig(d.path(), p, &err);
        QVERIFY2(loaded.has_value(), qPrintable(err));
        QVERIFY(!micEnabled(p, *loaded, {"cl01", "tutti"}));
        QVERIFY(!micEnabled(p, *loaded, {"ga01", "sopranos"}));
        QVERIFY(micEnabled(p, *loaded, {"cl01", "altos"}));
        QVERIFY(!micEnabled(p, *loaded, {"cl02", "altos"}));
        QVERIFY(!micEnabled(p, *loaded, {"tr01", "brass"}));
        QVERIFY(!micEnabled(p, *loaded, {"zz01", "tutti"}));
    }
};

QTEST_MAIN(TstProjectConfig)
#include "tst_projectconfig.moc"
