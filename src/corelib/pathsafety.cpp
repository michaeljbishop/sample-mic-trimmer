#include "pathsafety.h"
#include "pathsafety_testing.h"
#include "fsutil.h"
#include "winpath.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <filesystem>
#if defined(Q_OS_MACOS) || defined(Q_OS_BSD4)
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/param.h>
#endif

namespace smt {

namespace testing {

std::optional<MountId> realMountId(const QString &canonicalPath)
{
#if defined(Q_OS_MACOS) || defined(Q_OS_BSD4)
    struct statfs fs;
    if (::statfs(QFile::encodeName(canonicalPath).constData(), &fs) != 0)
        return std::nullopt;
    const QString from = QFile::decodeName(fs.f_mntfromname);
    const QString on = QFile::decodeName(fs.f_mntonname);
    if (from.isEmpty())
        return std::nullopt;
    QString rel;
    if (on == QStringLiteral("/") || canonicalPath == on || canonicalPath.startsWith(on + '/'))
        rel = canonicalPath.mid(on == QStringLiteral("/") ? 0 : on.size());
    else if (on == QStringLiteral("/System/Volumes/Data"))
        rel = canonicalPath; // a Data-volume firmlink (/Users, /private/var, ...): same path below the volume
    else
        return std::nullopt;
    while (rel.startsWith('/'))
        rel.remove(0, 1);
    while (rel.endsWith('/'))
        rel.chop(1);
    return MountId{from, rel};
#else
    Q_UNUSED(canonicalPath);
    return std::nullopt;
#endif
}

static MountIdProvider gProvider;
void setMountIdProvider(MountIdProvider provider)
{
    gProvider = std::move(provider);
}

std::optional<StatId> realStatId(const QString &path)
{
#if defined(Q_OS_MACOS) || defined(Q_OS_BSD4)
    struct stat st;
    if (::stat(QFile::encodeName(path).constData(), &st) != 0 || !S_ISDIR(st.st_mode))
        return std::nullopt;
    if (st.st_birthtimespec.tv_sec == 0 && st.st_birthtimespec.tv_nsec == 0)
        return std::nullopt; // no birth time reported: no identity to compare
    return StatId{quint64(st.st_dev), quint64(st.st_ino), qint64(st.st_birthtimespec.tv_sec),
                  qint64(st.st_birthtimespec.tv_nsec)};
#else
    Q_UNUSED(path);
    return std::nullopt;
#endif
}

static StatIdProvider gStatProvider;
void setStatIdProvider(StatIdProvider provider)
{
    gStatProvider = std::move(provider);
}

static FinalPathProvider gFinalProvider;
void setFinalPathProvider(FinalPathProvider provider)
{
    gFinalProvider = std::move(provider);
}

static FileIdProvider gFileIdProvider;
void setFileIdProvider(FileIdProvider provider)
{
    gFileIdProvider = std::move(provider);
}

#ifndef Q_OS_WIN // the Windows versions are in pathsafety_win.cpp
std::optional<QString> realFinalPath(const QString &) { return std::nullopt; }
std::optional<FileId> realFileId(const QString &) { return std::nullopt; }
#endif

} // namespace testing

namespace {

// "" unless source and dest (its existing ancestor plus a not-yet-created tail) are the same
// share or volume location reached through another mount, e.g. /Volumes/Share and
// /Volumes/Share-1. Reads only (statfs).
QString checkMountIdentity(const QString &sourceCanonical, const QString &ancestorCanonical, const QString &tail)
{
    const auto id = [](const QString &path) {
        return testing::gProvider ? testing::gProvider(path) : testing::realMountId(path);
    };
    const auto s = id(sourceCanonical), a = id(ancestorCanonical);
    if (!s || !a || s->from.isEmpty() || s->from.compare(a->from, Qt::CaseInsensitive) != 0)
        return {};
    // "/rel/" on both sides, so a prefix test is '/'-bounded ("/A/" is not a prefix of "/A2/").
    const auto norm = [](QString p) {
        while (p.startsWith('/'))
            p.remove(0, 1);
        while (p.endsWith('/'))
            p.chop(1);
        return p.isEmpty() ? QStringLiteral("/") : '/' + p + '/';
    };
    const QString sr = norm(s->rel), dr = norm(a->rel + tail);
    if (dr.startsWith(sr, Qt::CaseInsensitive) || sr.startsWith(dr, Qt::CaseInsensitive))
        return QStringLiteral("The destination and the source are the same share/volume location (mounted twice?) "
                              "on %1. Choose a destination outside the source library.")
            .arg(s->from);
    return {};
}

// "" unless the destination's nearest existing ancestor is the same server directory as the
// source, a folder in it, or (if the destination exists) one of its parents, seen through
// another mount: different st_dev, same inode and birth time. Catches a second mount whose
// mount source is spelled differently (IP vs host name, another user, a subfolder mount).
// Reads only (stat).
QString checkStatIdentity(const QString &sourceCanonical, const QString &ancestorCanonical, bool destExists)
{
    const auto id = [](const QString &path) {
        return testing::gStatProvider ? testing::gStatProvider(path) : testing::realStatId(path);
    };
    const auto a = id(ancestorCanonical);
    if (!a)
        return {};
    const auto same = [&a](const std::optional<testing::StatId> &b) {
        return b && b->dev != a->dev && b->ino == a->ino && b->birthSec == a->birthSec
               && b->birthNsec == a->birthNsec;
    };
    const QString why = QStringLiteral("The destination looks like the source library%1 reached through another "
                                       "mount or path (same folder identity: inode and creation time). Choose "
                                       "another destination.");
    if (same(id(sourceCanonical)))
        return why.arg(destExists ? QString() : QStringLiteral(" (a new folder in it)"));
    // The ancestor may be a folder inside the source: try <source>/<each trailing part of it>.
    const QStringList parts = ancestorCanonical.split('/', Qt::SkipEmptyParts);
    for (qsizetype i = parts.size() - 1; i >= 0; --i) {
        const QString inSource = sourceCanonical + '/' + parts.mid(i).join('/');
        if (QFileInfo(inSource).isDir() && same(id(inSource)))
            return why.arg(QStringLiteral(" (a folder in it)"));
    }
    if (destExists) // the destination may contain the source
        for (QString p = QFileInfo(sourceCanonical).path();; p = QFileInfo(p).path()) {
            if (same(id(p)))
                return why.arg(QStringLiteral("'s parent folder %1").arg(p));
            if (QFileInfo(p).isRoot())
                break;
        }
    return {};
}

// Windows: "" unless the final paths (mapped drive, subst drive, junction and UNC spellings resolved) show that the
// destination is the source, inside it, or contains it. Cannot be told (no final path) refuses: over-refusing is the
// safe error. Runs on Windows or when a test injects a provider; a no-op elsewhere.
QString checkFinalPaths(const QString &sourceCanonical, const QString &ancestorCanonical, const QString &tail, bool destExists)
{
    if (!testing::gFinalProvider && !winpath::kIsWindows)
        return {};
    const auto finalOf = [](const QString &path) {
        return testing::gFinalProvider ? testing::gFinalProvider(path) : testing::realFinalPath(path);
    };
    const auto sf = finalOf(sourceCanonical), af = finalOf(ancestorCanonical);
    if (!sf || !af)
        return QStringLiteral("The real location of the source or the destination folder could not be determined, so "
                              "the check that they do not overlap cannot be made. Choose another destination.");
    const QString dest = tail.isEmpty() ? *af : winpath::join(*af, tail);
    switch (winpath::relate(*sf, dest)) {
    case winpath::Relation::Same:
        return QStringLiteral("The destination is the same folder as the source (reached by another path).");
    case winpath::Relation::BInsideA:
        return QStringLiteral("The destination is inside the source folder (reached by another path).");
    case winpath::Relation::AInsideB:
        return destExists ? QStringLiteral("The source folder is inside the destination (reached by another path), "
                                           "which would be overwritten.")
                          : QString();
    case winpath::Relation::Unrelated:
        break;
    }
    return {};
}

} // namespace

QString checkSourceAndDestination(const QString &source, const QString &dest)
{
    if (source.isEmpty() || !QFileInfo(source).isDir())
        return QStringLiteral("The source library folder does not exist or is not a folder.");
    if (dest.trimmed().isEmpty())
        return QStringLiteral("No destination folder was given.");

    // Nearest existing ancestor of dest (dest itself if it exists); the rest is a
    // not-yet-created tail, resolved lexically onto the canonical ancestor.
    // ".." is refused outright: cleanPath would collapse it before symlinks are
    // resolved ("lnk/../out" may really be inside the source). Over-refuses benign "..".
    if (winpath::hasDotDotSegment(dest, winpath::kIsWindows))
        return QStringLiteral("The destination path must not contain '..'.");
    const QString abs = QDir::cleanPath(QFileInfo(dest).absoluteFilePath());
    QString existing = abs;
    while (!QFileInfo::exists(existing)) {
        if (QFileInfo(existing).isSymLink())
            return QStringLiteral("The destination is a broken symbolic link.");
        const QString parent = QFileInfo(existing).path();
        if (parent == existing)
            return QStringLiteral("The destination has no existing parent folder.");
        existing = parent;
    }
    const QFileInfo ancestor(existing);
    if (!ancestor.isDir())
        return QStringLiteral("The destination (or a parent of it) is a file, not a folder.");
    if (!ancestor.isWritable())
        return QStringLiteral("The destination folder is not writable.");

    if (ancestor.canonicalFilePath().isEmpty())
        return QStringLiteral("The destination folder could not be resolved.");

    // Trailing '/' so "/a/bc" is not treated as inside "/a/b". Case-insensitive:
    // macOS volumes usually are, and over-refusing is the safe error.
    const QString s = QFileInfo(source).canonicalFilePath() + '/';
    const QString d = QDir::cleanPath(ancestor.canonicalFilePath() + '/' + abs.mid(existing.size())) + '/';
    if (d.compare(s, Qt::CaseInsensitive) == 0)
        return QStringLiteral("The destination is the same folder as the source.");
    if (d.startsWith(s, Qt::CaseInsensitive))
        return QStringLiteral("The destination is inside the source folder.");
    if (s.startsWith(d, Qt::CaseInsensitive))
        return QStringLiteral("The source folder is inside the destination, which would be overwritten.");

    // Same checks by file identity: another path to the same folder (a firmlink such as
    // /System/Volumes/Data/..., a bind mount) keeps a different canonical path.
    const auto same = [](const QString &a, const QString &b) {
        if (testing::gFileIdProvider || winpath::kIsWindows) { // Windows: volume serial number + file index
            const auto ia = testing::gFileIdProvider ? testing::gFileIdProvider(a) : testing::realFileId(a);
            const auto ib = testing::gFileIdProvider ? testing::gFileIdProvider(b) : testing::realFileId(b);
            return ia && ib && *ia == *ib;
        }
        std::error_code ec;
        return std::filesystem::equivalent(fs::path(a), fs::path(b), ec);
    };
    const QString sourceCanonical = QFileInfo(source).canonicalFilePath();
    for (QString a = ancestor.canonicalFilePath();; a = QFileInfo(a).path()) {
        if (same(a, sourceCanonical))
            return a == ancestor.canonicalFilePath() && existing == abs
                       ? QStringLiteral("The destination is the same folder as the source (reached by another path).")
                       : QStringLiteral("The destination is inside the source folder (reached by another path).");
        if (QFileInfo(a).isRoot())
            break;
    }
    if (existing == abs)
        for (QString a = QFileInfo(source).canonicalFilePath(); !QFileInfo(a).isRoot(); a = QFileInfo(a).path())
            if (same(a, abs))
                return QStringLiteral("The source folder is inside the destination (reached by another path), "
                                      "which would be overwritten.");
    const QString byFinalPath = checkFinalPaths(QFileInfo(source).canonicalFilePath(), ancestor.canonicalFilePath(),
                                                abs.mid(existing.size()), existing == abs);
    if (!byFinalPath.isEmpty())
        return byFinalPath;
    const QString byMount = checkMountIdentity(QFileInfo(source).canonicalFilePath(), ancestor.canonicalFilePath(),
                                               abs.mid(existing.size()));
    if (!byMount.isEmpty())
        return byMount;
    return checkStatIdentity(QFileInfo(source).canonicalFilePath(), ancestor.canonicalFilePath(), existing == abs);
}

QString checkSourceLock(const QString &source, const QString &dest)
{
    // <source>/.smt.lock, then <source>/<each trailing part of dest>/.smt.lock: a destination
    // that is a folder inside the source by another path has its lock show up there.
    QStringList candidates{source + QStringLiteral("/.smt.lock")};
    const QStringList parts = QDir::cleanPath(dest).split('/', Qt::SkipEmptyParts);
    for (qsizetype i = parts.size() - 1; i >= 0; --i)
        candidates << source + '/' + parts.mid(i).join('/') + QStringLiteral("/.smt.lock");
    for (const QString &lock : candidates)
        if (QFileInfo::exists(lock))
            return QStringLiteral("%1 exists: the destination seems to be the source library reached by another "
                                  "path (e.g. a second network mount). Choose another destination. If no apply is "
                                  "running, and the source really is a different folder, delete that stale lock file.")
                .arg(lock);
    return {};
}

namespace testing {

namespace {
// Last folder name: the Windows flavour reads either separator, drive roots and UNC shares, and compares names
// without case; the Unix flavour is what always ran on macOS.
QString leafOf(bool windows, const QString &p)
{
    return windows ? winpath::leafName(p) : QDir::cleanPath(p).section('/', -1);
}
bool sameName(bool windows, const QString &a, const QString &b)
{
    return windows ? a.compare(b, Qt::CaseInsensitive) == 0 : a == b;
}
} // namespace

QString checkDestinationNameFor(bool windows, const QString &source, const QString &dest)
{
    const QString want = leafOf(windows, source);
    if (!want.startsWith("Spitfire Audio - ") || sameName(windows, leafOf(windows, dest), want))
        return {};
    return QString("The destination folder must be named \"%1\", like the source: the Spitfire app "
                   "refuses a library folder with another name. Choose the destination again (the app then "
                   "creates a folder with that name inside the one you pick); on the command line pass the "
                   "folder with that name as -p.")
        .arg(want);
}

QString resolveDestinationFolderFor(bool windows, const QString &source, const QString &chosen)
{
    const QString want = leafOf(windows, source);
    if (source.isEmpty() || chosen.isEmpty() || !want.startsWith("Spitfire Audio - ")
        || sameName(windows, leafOf(windows, chosen), want))
        return chosen;
    return windows ? winpath::join(chosen, want) : QDir::cleanPath(chosen + '/' + want);
}

} // namespace testing

QString checkDestinationName(const QString &source, const QString &dest)
{
    return testing::checkDestinationNameFor(winpath::kIsWindows, source, dest);
}

QString resolveDestinationFolder(const QString &source, const QString &chosen)
{
    return testing::resolveDestinationFolderFor(winpath::kIsWindows, source, chosen);
}

} // namespace smt
