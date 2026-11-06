#pragma once

#include <QUrl>
#include <optional>
#include <QList>
#include <QString>
#include "profile.h"

namespace smt {

struct ProfileEntry {
    QString id, name, path;
    std::optional<ProductProfile> profile;
    QString error;
    bool builtIn = false;
    bool valid() const { return profile.has_value(); }
};

// macOS: <app dir>/../Resources/Profiles when it exists (inside a .app); elsewhere <app dir>/Profiles when it
// exists; else empty (the compiled-in profiles are used).
QString bundledProfilesDir();

// Built-ins (bundledProfilesDir(), else the compiled-in ":/profiles") first, then the *.smtprofile folders in userDir (may not exist).
// A valid user profile with a built-in's id replaces it; an invalid one is its own entry (id = folder name).
QList<ProfileEntry> loadProfiles(const QString &userDir);
// The seam under loadProfiles: an empty builtInDir means the compiled-in resources.
QList<ProfileEntry> loadProfilesFrom(const QString &builtInDir, const QString &userDir);

inline const QString kBbcsoProfileId = QStringLiteral("name.mbishop.smtprofile.bbcso");
inline const QString kEwcProfileId = QStringLiteral("name.mbishop.smtprofile.ewc");
// A compiled-in profile; aborts if it is missing (an empty profile would classify every file as Other).
ProductProfile builtInProfile(const QString &id);

QString userProfilesDir(); // <app data dir>/Profiles, $SMT_APP_DATA_DIR aware like the scan cache

// First valid entry whose folderPattern matches, else nullptr.
const ProfileEntry *suggestProfile(const QList<ProfileEntry> &entries, const QString &libraryFolderName);

// The first existing file among the image's sources, in order, else "". `library:` paths are relative to
// libraryRoot ('*' = exactly one folder level); `file:` paths relative to <bundlePath>/Contents/Resources, never outside it.
// `cache:<name>` is a plain file name in cacheDir (the Spitfire app's image cache; extension-less images are fine).
// `url:<https url>` is the downloaded copy <urlCacheDir>/imageUrlCacheName(url), if it exists inside urlCacheDir.
QString resolveProfileImage(const ProductProfile &profile, const QString &imageName, const QString &libraryRoot,
                            const QString &cacheDir, const QString &urlCacheDir);

QString imageUrlCacheName(const QUrl &url); // sha1 hex of the url string
QString profileImageUrlCacheDir();          // <app data dir>/ImageCache, $SMT_APP_DATA_DIR aware
// The `url:` sources of the image not downloaded yet and ahead of any source that already resolves.
QList<QUrl> pendingImageUrls(const ProductProfile &profile, const QString &imageName, const QString &libraryRoot,
                             const QString &cacheDir, const QString &urlCacheDir);

} // namespace smt
