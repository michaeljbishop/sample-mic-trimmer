#include "imagefetcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QDeadlineTimer>
#include <QTimer>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>

namespace {
constexpr qint64 kMaxBytes = 8 * 1024 * 1024;
constexpr int kMaxRedirects = 3, kTimeoutMs = 15000, kTotalMs = 30000, kMaxDimension = 2048;
} // namespace

ImageFetcher::ImageFetcher(const QString &allowedHost, bool requireHttps, QObject *parent)
    : QObject(parent)
    , mHost(allowedHost.toLower())
    , mHttps(requireHttps)
{
}

bool ImageFetcher::allowed(const QUrl &url) const
{
    const bool schemeOk = mHttps ? url.scheme() == QLatin1String("https")
                                 : url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http");
    return url.isValid() && schemeOk && url.host().toLower() == mHost && url.userInfo().isEmpty() && (!mHttps || url.port(443) == 443);
}

bool ImageFetcher::redirectAllowed(const QUrl &from, const QUrl &to) const
{
    return allowed(to) && to.port(443) == from.port(443);
}

void ImageFetcher::fail(const QUrl &url, const QString &why)
{
    mFailed.insert(url);
    mInFlight.remove(url);
    emit failed(url, why);
}

void ImageFetcher::fetch(const QUrl &url, const QString &destFile)
{
    if (mInFlight.contains(url))
        return;
    if (mDone.contains(url) || QFileInfo(destFile).isFile())
        return; // silent: callers re-run on fetched(), so this must never signal
    if (mFailed.contains(url)) {
        emit failed(url, tr("already failed in this session"));
        return;
    }
    if (!allowed(url)) {
        fail(url, tr("URL not allowed"));
        return;
    }
    mInFlight.insert(url);
    start(url, url, destFile, 0, QDeadlineTimer(kTotalMs));
}

void ImageFetcher::start(const QUrl &original, const QUrl &url, const QString &destFile, int redirects, QDeadlineTimer deadline)
{
    if (deadline.hasExpired())
        return fail(original, tr("timed out"));
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "SampleMicTrimmer/" + QCoreApplication::applicationVersion().toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(kTimeoutMs);
    QNetworkReply *reply = mNam.get(req);
    QTimer::singleShot(int(deadline.remainingTime()), reply, [reply] { reply->abort(); }); // total limit; the transfer timeout is "no data"
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 got, qint64 total) {
        if (got > kMaxBytes || total > kMaxBytes)
            reply->abort(); // finished() below reports it
    });
    connect(reply, &QNetworkReply::finished, this, [=] {
        reply->deleteLater();
        const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (status.toInt() / 100 == 3) {
            const QByteArray location = reply->rawHeader("Location");
            const QUrl next = url.resolved(QUrl::fromEncoded(location));
            if (location.isEmpty() || redirects >= kMaxRedirects || !redirectAllowed(url, next))
                return fail(original, tr("redirect refused"));
            return start(original, next, destFile, redirects + 1, deadline);
        }
        if (reply->error() != QNetworkReply::NoError || status.toInt() != 200)
            return fail(original, reply->errorString());
        const QByteArray body = reply->readAll();
        if (body.size() > kMaxBytes)
            return fail(original, tr("too large"));
        QBuffer buf;
        buf.setData(body);
        buf.open(QIODevice::ReadOnly);
        QImageReader reader(&buf);
        const QByteArray fmt = reader.format();
        const QSize size = reader.size(); // from the header, before any decoding
        if ((fmt != "png" && fmt != "jpeg") || !size.isValid() || size.width() > kMaxDimension
            || size.height() > kMaxDimension || reader.read().isNull())
            return fail(original, tr("not a usable image"));
        QDir().mkpath(QFileInfo(destFile).path());
        QSaveFile out(destFile);
        if (!out.open(QIODevice::WriteOnly) || out.write(body) != body.size() || !out.commit())
            return fail(original, tr("cannot save"));
        mInFlight.remove(original);
        mDone.insert(original);
        emit fetched(original);
    });
}
