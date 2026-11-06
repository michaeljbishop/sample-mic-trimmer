#include <QCryptographicHash>
#include "profileregistry.h"
#include "corelib.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace smt {

namespace {

void addFrom(QList<ProfileEntry> &out, const QString &dir, bool builtIn)
{
    const auto folders = QDir(dir).entryInfoList({QStringLiteral("*.smtprofile")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto &fi : folders) {
        ProfileEntry e;
        e.path = fi.filePath();
        e.builtIn = builtIn;
        e.profile = loadProfile(e.path, &e.error);
        e.id = e.valid() ? e.profile->id : fi.completeBaseName();
        e.name = e.valid() ? e.profile->name : fi.completeBaseName();
        bool replaced = false;
        if (e.valid() && !builtIn) {
            for (auto &old : out)
                if (old.id == e.id && old.valid()) {
                    old = e;
                    replaced = true;
                    break;
                }
        }
        if (replaced)
            continue;
        out.append(e);
    }
}

} // namespace

QString bundledProfilesDir()
{
    if (!QCoreApplication::instance()) // no application object: cannot be in a bundle, use the compiled-in resources
        return QString();
#ifdef Q_OS_MACOS
    const QDir d(QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/Profiles"));
#else
    // Windows (UNVERIFIED) and others: a Profiles folder next to the executable (the build and the install put it there).
    // Only the app and the CLI use it: the tests sit in the same folder and must keep the compiled-in profiles.
    if (QCoreApplication::applicationName() != kApplicationName)
        return QString();
    const QDir d(QCoreApplication::applicationDirPath() + QStringLiteral("/Profiles"));
#endif
    return d.exists() ? d.canonicalPath() : QString();
}

QList<ProfileEntry> loadProfiles(const QString &userDir)
{
    return loadProfilesFrom(bundledProfilesDir(), userDir);
}

QList<ProfileEntry> loadProfilesFrom(const QString &builtInDir, const QString &userDir)
{
    QList<ProfileEntry> out;
    addFrom(out, builtInDir.isEmpty() ? QStringLiteral(":/profiles") : builtInDir, true);
    if (!userDir.isEmpty())
        addFrom(out, userDir, false);
    return out;
}

ProductProfile builtInProfile(const QString &id)
{
    for (const auto &e : loadProfiles({}))
        if (e.id == id && e.valid())
            return *e.profile;
    qFatal("built-in profile %s missing", qPrintable(id));
    return {}; // not reached: qFatal aborts (MSVC does not know it is noreturn)
}

namespace {
QString appDataBase()
{
    const QString env = qEnvironmentVariable("SMT_APP_DATA_DIR");
    return env.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : env;
}
} // namespace

QString userProfilesDir()
{
    return QDir(appDataBase()).absoluteFilePath(QStringLiteral("Profiles"));
}

const ProfileEntry *suggestProfile(const QList<ProfileEntry> &entries, const QString &libraryFolderName)
{
    for (const auto &e : entries)
        if (e.valid() && e.profile->folderPattern.match(libraryFolderName).hasMatch())
            return &e;
    return nullptr;
}

namespace {

// Walks `parts` below `dir`; a "*" part is any one sub-folder. Returns the first existing file.
QString walk(const QString &dir, const QStringList &parts, int i)
{
    if (i == parts.size() - 1) {
        const QFileInfo fi(dir + QLatin1Char('/') + parts[i]);
        return parts[i] != QLatin1String("*") && fi.isFile() ? fi.filePath() : QString();
    }
    if (parts[i] != QLatin1String("*"))
        return walk(dir + QLatin1Char('/') + parts[i], parts, i + 1);
    const auto subs = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &sub : subs) {
        const QString hit = walk(dir + QLatin1Char('/') + sub, parts, i + 1);
        if (!hit.isEmpty())
            return hit;
    }
    return {};
}

} // namespace

namespace {

QString resolveSource(const ProductProfile &profile, const QString &src, const QString &libraryRoot, const QString &cacheDir,
                      const QString &urlCacheDir)
{
    const QString resources = QDir(profile.bundlePath + QStringLiteral("/Contents/Resources")).canonicalPath();
    // A downloaded or cached file counts only when it really sits inside its folder (a symlink must not leave it).
    auto inside = [](const QString &dir, const QString &name) -> QString {
        const QFileInfo fi(dir + QLatin1Char('/') + name);
        const QString real = fi.canonicalFilePath();
        return !real.isEmpty() && real.startsWith(QDir(dir).canonicalPath() + QLatin1Char('/')) && fi.isFile() ? fi.filePath() : QString();
    };
    if (src.startsWith(QLatin1String("library:"))) {
        const QStringList parts = src.mid(8).split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (libraryRoot.isEmpty() || parts.isEmpty() || parts.contains(QStringLiteral("..")))
            return {};
        return walk(libraryRoot, parts, 0);
    }
    if (src.startsWith(QLatin1String("cache:"))) {
        const QString name = src.mid(6);
        return cacheDir.isEmpty() || !isPlainCacheName(name) ? QString() : inside(cacheDir, name);
    }
    if (src.startsWith(QLatin1String("url:"))) {
        return urlCacheDir.isEmpty() ? QString() : inside(urlCacheDir, imageUrlCacheName(QUrl(src.mid(4))));
    }
    if (src.startsWith(QLatin1String("file:")) && !resources.isEmpty() && !profile.bundlePath.isEmpty()) {
        const QString path = QDir::cleanPath(profile.bundlePath + QStringLiteral("/Contents/Resources/") + src.mid(5));
        const QString real = QFileInfo(path).canonicalFilePath();
        if (!real.isEmpty() && real.startsWith(resources + QLatin1Char('/')) && QFileInfo(real).isFile())
            return path;
    }
    return {};
}

} // namespace

QString imageUrlCacheName(const QUrl &url)
{
    return QString::fromLatin1(QCryptographicHash::hash(url.toString().toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString profileImageUrlCacheDir()
{
    return QDir(appDataBase()).absoluteFilePath(QStringLiteral("ImageCache"));
}

QString resolveProfileImage(const ProductProfile &profile, const QString &imageName, const QString &libraryRoot,
                            const QString &cacheDir, const QString &urlCacheDir)
{
    for (const QString &src : profile.images.value(imageName)) {
        const QString hit = resolveSource(profile, src, libraryRoot, cacheDir, urlCacheDir);
        if (!hit.isEmpty())
            return hit;
    }
    return {};
}

QList<QUrl> pendingImageUrls(const ProductProfile &profile, const QString &imageName, const QString &libraryRoot,
                             const QString &cacheDir, const QString &urlCacheDir)
{
    QList<QUrl> out;
    for (const QString &src : profile.images.value(imageName)) {
        if (!resolveSource(profile, src, libraryRoot, cacheDir, urlCacheDir).isEmpty())
            break; // a better source already provides the picture
        if (src.startsWith(QLatin1String("url:")))
            out.append(QUrl(src.mid(4)));
    }
    return out;
}

} // namespace smt
