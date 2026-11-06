#pragma once

#include <optional>
#include <QHash>
#include <QMap>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QUrl>
#include <QStringList>

namespace smt {

// A `cache:` image source: a plain file name, so it can never leave the cache folder.
inline bool isPlainCacheName(const QString &n)
{
    return !n.isEmpty() && !n.startsWith(QLatin1Char('.')) && !n.contains(QLatin1Char('/')) && !n.contains(QLatin1String(".."));
}

// A `url:` image source or `credit`: https on www.spitfireaudio.com only, no userinfo/port/"..", at most 300 chars.
inline bool isAllowedImageUrl(const QString &s)
{
    const QUrl u(s, QUrl::StrictMode);
    return s.size() <= 300 && u.isValid() && u.scheme() == QLatin1String("https")
        && u.host().toLower() == QLatin1String("www.spitfireaudio.com") && u.userInfo().isEmpty() && u.port() == -1
        && u.path().startsWith(QLatin1Char('/')) && !u.path().split(QLatin1Char('/')).contains(QLatin1String(".."));
}

struct MicDef {
    QString token; // lower-case, e.g. "tr01"
    QString name;
    QString family;
    bool locked = false;
    QString note;
};

struct FamilyDef {
    QString key, name, tab; // tab empty = the family's own name
};

struct QuickSet {
    QString name;
    QStringList families;
};

struct GroupDef {
    QString key, name, match;
    bool byDir = false;
};

// One entry of Spitfire's own Spitfire.properties that belongs to this product: for each key ("config", "patches",
// "presets", "samples", ...) the sub-paths below the library folder it should list ("" = the folder itself).
struct SpitfireEntry {
    QString name;
    QMap<QString, QStringList> keys;
};

struct ProductProfile {
    QString id, name, version;
    QRegularExpression folderPattern, sampleFile;
    QString samplesDir;
    QList<MicDef> mics;
    QList<FamilyDef> families;
    QList<QuickSet> quickSets;
    QList<GroupDef> groups;
    QHash<QString, QStringList> images;
    QList<SpitfireEntry> spitfireEntries; // empty = the profile does not describe them (no "Change installation location")
    QHash<QString, QString> imageCredits; // image name -> https page the picture belongs to
    QString notes, bundlePath;

    const MicDef *mic(const QString &token) const;
    const GroupDef *group(const QString &key) const;
};

// Loads a macOS-bundle profile (filesystem or ":/" resource path). On failure returns nullopt with a one-line reason.
std::optional<ProductProfile> loadProfile(const QString &bundlePath, QString *error);

} // namespace smt
