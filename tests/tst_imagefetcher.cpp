#include <QtTest>
#include "unixonly.h"
#include <QBuffer>
#include <QImage>
#include <QTcpServer>
#include <QTcpSocket>
#include "imagefetcher.h"

// Loopback only: no test ever reaches the real network.
class TstImageFetcher : public QObject
{
    Q_OBJECT
    QTcpServer server;
    int requests = 0;
    QByteArray png, jpg, wide, ok2000;

    QUrl url(const QString &path, const QString &host = QStringLiteral("127.0.0.1")) const
    {
        return QUrl(QStringLiteral("http://%1:%2%3").arg(host).arg(server.serverPort()).arg(path));
    }
    void reply(QTcpSocket *s, const QByteArray &head, const QByteArray &body)
    {
        s->write("HTTP/1.1 " + head + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        s->disconnectFromHost();
    }

private slots:
    void initTestCase()
    {
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        QImage(4, 4, QImage::Format_RGB32).save(&b, "PNG");
        QBuffer bj(&jpg);
        bj.open(QIODevice::WriteOnly);
        QImage(4, 4, QImage::Format_RGB32).save(&bj, "JPEG");
        QBuffer bw(&wide);
        bw.open(QIODevice::WriteOnly);
        QImage img(3000, 10, QImage::Format_RGB32);
        img.fill(Qt::white);
        img.save(&bw, "PNG");
        QBuffer bk(&ok2000);
        bk.open(QIODevice::WriteOnly);
        QImage(2000, 10, QImage::Format_RGB32).save(&bk, "PNG");
        connect(&server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *s = server.nextPendingConnection();
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            connect(s, &QTcpSocket::readyRead, this, [this, s] {
                const QByteArray req = s->readAll();
                ++requests;
                const QByteArray path = req.split(' ').value(1);
                if (path == "/ok")
                    reply(s, "200 OK", png);
                else if (path == "/text")
                    reply(s, "200 OK", "<html>not an image</html>");
                else if (path == "/jpg")
                    reply(s, "200 OK", jpg);
                else if (path == "/ok2000")
                    reply(s, "200 OK", ok2000);
                else if (path == "/wide")
                    reply(s, "200 OK", wide);
                else if (path == "/svg")
                    reply(s, "200 OK", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"4\"/>");
                else if (path == "/big") // decodes fine (bytes after the PNG end are ignored), only the size can refuse it
                    reply(s, "200 OK", png + QByteArray(9 * 1024 * 1024, '\0'));
                else if (path == "/big-nolen") {
                    s->write("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n" + png + QByteArray(9 * 1024 * 1024, '\0'));
                    s->disconnectFromHost();
                }
                else if (path == "/redir-closed")
                    s->write("HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:1/ok\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"),
                        s->disconnectFromHost();
                else if (path == "/redir")
                    s->write("HTTP/1.1 302 Found\r\nLocation: http://localhost:" + QByteArray::number(server.serverPort())
                             + "/ok\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"), s->disconnectFromHost();
                else if (path == "/redir-same")
                    s->write("HTTP/1.1 302 Found\r\nLocation: /ok\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"), s->disconnectFromHost();
                else
                    reply(s, "404 Not Found", "");
            });
        });
        QVERIFY(server.listen(QHostAddress::LocalHost));
    }
    void init() { requests = 0; }

    void savesADecodableImage()
    {
        QTemporaryDir d;
        ImageFetcher f(QStringLiteral("127.0.0.1"), false);
        QSignalSpy ok(&f, &ImageFetcher::fetched), bad(&f, &ImageFetcher::failed);
        const QString dest = d.path() + "/sub/img";
        f.fetch(url("/ok"), dest);
        QTRY_COMPARE(ok.size(), 1);
        QCOMPARE(bad.size(), 0);
        QVERIFY(!QImage(dest).isNull());
        f.fetch(url("/ok"), dest); // exists: silent, no new request
        QCOMPARE(ok.size(), 1);
        QCOMPARE(requests, 1);
    }

    void followsRedirectOnSameHost()
    {
        QTemporaryDir d;
        ImageFetcher f(QStringLiteral("127.0.0.1"), false);
        QSignalSpy ok(&f, &ImageFetcher::fetched), bad(&f, &ImageFetcher::failed);
        f.fetch(url("/redir-same"), d.path() + "/img");
        QTRY_VERIFY2(ok.size() + bad.size() > 0, "no result");
        QVERIFY2(ok.size() == 1, bad.isEmpty() ? "" : qPrintable(bad.first().value(1).toString()));
    }

    void nonImageBodyIsRefusedAndNothingSaved()
    {
        QTemporaryDir d;
        ImageFetcher f(QStringLiteral("127.0.0.1"), false);
        QSignalSpy bad(&f, &ImageFetcher::failed);
        f.fetch(url("/text"), d.path() + "/img");
        QTRY_COMPARE(bad.size(), 1);
        QVERIFY(QDir(d.path()).isEmpty());
        f.fetch(url("/text"), d.path() + "/img"); // never retried in the session
        QCOMPARE(bad.size(), 2);
        QCOMPARE(requests, 1);
    }

    void oversizeBodyIsRefusedWithAndWithoutContentLength()
    {
        for (const char *path : {"/big", "/big-nolen"}) {
            QTemporaryDir d;
            ImageFetcher f(QStringLiteral("127.0.0.1"), false);
            QSignalSpy bad(&f, &ImageFetcher::failed), ok(&f, &ImageFetcher::fetched);
            f.fetch(url(path), d.path() + "/img");
            QTRY_COMPARE_WITH_TIMEOUT(bad.size(), 1, 10000);
            QCOMPARE(ok.size(), 0);
            QVERIFY(QDir(d.path()).isEmpty());
        }
    }

    void acceptsPngAndJpegRefusesSvgAndHugeDimensions()
    {
        QTemporaryDir d;
        ImageFetcher f(QStringLiteral("127.0.0.1"), false);
        QSignalSpy bad(&f, &ImageFetcher::failed), ok(&f, &ImageFetcher::fetched);
        f.fetch(url("/ok"), d.path() + "/png");
        f.fetch(url("/jpg"), d.path() + "/jpg");
        f.fetch(url("/ok2000"), d.path() + "/ok2000");
        QTRY_COMPARE(ok.size(), 3);
        f.fetch(url("/svg"), d.path() + "/svg");
        f.fetch(url("/wide"), d.path() + "/wide");
        QTRY_COMPARE(bad.size(), 2);
        QVERIFY(!QFileInfo::exists(d.path() + "/svg") && !QFileInfo::exists(d.path() + "/wide"));
    }

    void folderOrEscapingSymlinkAtDestinationNeverLoops()
    {
        SMT_UNIX_ONLY();
        QTemporaryDir d;
        ImageFetcher f(QStringLiteral("127.0.0.1"), false);
        int fetchedCount = 0;
        connect(&f, &ImageFetcher::fetched, this, [&](const QUrl &u) {
            if (++fetchedCount < 50) // what MainWindow::updateArtwork does
                f.fetch(u, d.path() + "/dir");
        });
        QVERIFY(QDir().mkpath(d.path() + "/dir"));
        f.fetch(url("/ok"), d.path() + "/dir");
        QTest::qWait(300);
        QVERIFY(fetchedCount < 2);
        QTemporaryDir other;
        QVERIFY(QImage(4, 4, QImage::Format_RGB32).save(other.path() + "/x", "PNG"));
        QVERIFY(QFile::link(other.path() + "/x", d.path() + "/link2"));
        const int before = requests;
        f.fetch(url("/ok"), d.path() + "/link2"); // existing file behind a symlink: silent
        QCOMPARE(requests, before);
    }

    void wrongHostIsRejectedWithoutARequest()
    {
        QTemporaryDir d;
        ImageFetcher f(QStringLiteral("127.0.0.1"), false);
        QSignalSpy bad(&f, &ImageFetcher::failed);
        f.fetch(url("/ok", "localhost"), d.path() + "/img");
        QCOMPARE(bad.size(), 1);
        QCOMPARE(requests, 0);
    }

    void defaultFetcherRefusesHttpAndOtherHosts()
    {
        QTemporaryDir d;
        ImageFetcher f; // production rules; every URL below is refused before any request
        QSignalSpy bad(&f, &ImageFetcher::failed);
        for (const char *u : {"http://www.spitfireaudio.com/a.png", "https://example.com/a.png",
                              "https://www.spitfireaudio.com:444/a.png", "https://www.spitfireaudio.com@evil.com/a.png"})
            f.fetch(QUrl(QString::fromLatin1(u)), d.path() + "/img");
        QCOMPARE(bad.size(), 4);
        QCOMPARE(requests, 0);
    }

    void redirectToAnotherHostIsRejected()
    {
        QTemporaryDir d;
        ImageFetcher f(QStringLiteral("127.0.0.1"), false);
        QSignalSpy bad(&f, &ImageFetcher::failed), ok(&f, &ImageFetcher::fetched);
        f.fetch(url("/redir"), d.path() + "/img");
        QTRY_COMPARE(bad.size(), 1);
        QCOMPARE(bad.first().value(1).toString(), QString("redirect refused"));
        QCOMPARE(ok.size(), 0);
        QCOMPARE(requests, 1);
        QVERIFY(QDir(d.path()).isEmpty());
    }

    void redirectRules()
    {
        ImageFetcher f; // production rules
        const QUrl from("https://www.spitfireaudio.com/a.png");
        QVERIFY(f.redirectAllowed(from, QUrl("https://www.spitfireaudio.com/b.png")));
        QVERIFY(f.redirectAllowed(from, QUrl("https://WWW.spitfireaudio.com:443/b.png")));
        QVERIFY(f.redirectAllowed(QUrl("https://www.spitfireaudio.com:443/a"), QUrl("https://www.spitfireaudio.com/b")));
        QVERIFY(!f.redirectAllowed(from, QUrl("https://www.spitfireaudio.com:8443/b.png")));
        QVERIFY(!f.redirectAllowed(from, QUrl("https://cdn.spitfireaudio.com/b.png")));
        QVERIFY(!f.redirectAllowed(from, QUrl("http://www.spitfireaudio.com/b.png")));
        QVERIFY(!f.redirectAllowed(from, QUrl("https://user@www.spitfireaudio.com/b.png")));
    }

    void redirectToAnotherPortIsRefusedBeforeConnecting()
    {
        QTemporaryDir d;
        ImageFetcher f(QStringLiteral("127.0.0.1"), false);
        QSignalSpy bad(&f, &ImageFetcher::failed);
        f.fetch(url("/redir-closed"), d.path() + "/img"); // Location: http://127.0.0.1:1/ok (nothing listens there)
        QTRY_COMPARE(bad.size(), 1);
        QCOMPARE(bad.first().value(1).toString(), QString("redirect refused")); // not "connection refused"
        QCOMPARE(requests, 1);
    }
};

QTEST_MAIN(TstImageFetcher)
#include "tst_imagefetcher.moc"
