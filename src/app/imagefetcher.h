#pragma once

#include <QNetworkAccessManager>
#include <QDeadlineTimer>
#include <QObject>
#include <QSet>
#include <QUrl>

// Downloads one profile picture to a local file: https on one allow-listed host only (redirects too), 15 s,
// at most 8 MB, 30 s in total, must decode as an image before it is saved. A failed URL is not tried again in this session.
class ImageFetcher : public QObject
{
    Q_OBJECT
public:
    // `requireHttps` = false is for loopback tests only.
    explicit ImageFetcher(const QString &allowedHost = QStringLiteral("www.spitfireaudio.com"), bool requireHttps = true,
                          QObject *parent = nullptr);
    void fetch(const QUrl &url, const QString &destFile);
    bool redirectAllowed(const QUrl &from, const QUrl &to) const; // same rules as the first request, same effective port

signals:
    void fetched(const QUrl &url);
    void failed(const QUrl &url, const QString &reason);

private:
    bool allowed(const QUrl &url) const;
    void start(const QUrl &original, const QUrl &url, const QString &destFile, int redirects, QDeadlineTimer deadline);
    void fail(const QUrl &url, const QString &why);

    QNetworkAccessManager mNam;
    QString mHost;
    bool mHttps;
    QSet<QUrl> mFailed, mInFlight, mDone;
};
