#include "profile.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QXmlStreamReader>

namespace smt {

const MicDef *ProductProfile::mic(const QString &token) const
{
    const QString t = token.toLower();
    for (const auto &m : mics)
        if (m.token == t)
            return &m;
    return nullptr;
}

const GroupDef *ProductProfile::group(const QString &key) const
{
    for (const auto &g : groups)
        if (g.key == key)
            return &g;
    return nullptr;
}

namespace {

struct Failure { QString msg; };

// A path inside Contents/Resources/: relative, no ".." escape.
bool insideResources(const QString &rel)
{
    if (rel.isEmpty() || QDir::isAbsolutePath(rel) || rel.startsWith(QLatin1Char(':')))
        return false;
    const QString c = QDir::cleanPath(rel);
    return c != QLatin1String("..") && !c.startsWith(QLatin1String("../"));
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw Failure{QStringLiteral("cannot read %1: %2").arg(path, f.errorString())};
    return f.readAll();
}

QMap<QString, QString> readPlist(const QString &path)
{
    QXmlStreamReader xml(readFile(path));
    QMap<QString, QString> kv;
    QString key;
    while (xml.readNextStartElement()) {
        const auto name = xml.name();
        if (name == QLatin1String("key")) {
            key = xml.readElementText();
        } else if (name == QLatin1String("string") && !key.isEmpty()) {
            kv[key] = xml.readElementText();
            key.clear();
        } // plist/dict and anything else: descend
    }
    if (xml.hasError())
        throw Failure{QStringLiteral("Info.plist: %1").arg(xml.errorString())};
    return kv;
}

QJsonObject readPart(const QString &resources, const QMap<QString, QString> &plist, const QString &key, bool required)
{
    const QString rel = plist.value(key);
    if (rel.isEmpty()) {
        if (required)
            throw Failure{QStringLiteral("Info.plist lacks %1").arg(key)};
        return {};
    }
    if (!insideResources(rel))
        throw Failure{QStringLiteral("%1: path '%2' leaves Contents/Resources").arg(key, rel)};
    QJsonParseError pe;
    const auto doc = QJsonDocument::fromJson(readFile(resources + QLatin1Char('/') + rel), &pe);
    if (!doc.isObject())
        throw Failure{QStringLiteral("%1: %2").arg(rel, pe.error != QJsonParseError::NoError ? pe.errorString()
                                                                                         : QStringLiteral("not a JSON object"))};
    return doc.object();
}

QRegularExpression compile(const QString &pattern, const QString &what)
{
    QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
    if (!re.isValid())
        throw Failure{QStringLiteral("%1 is not a valid regular expression: %2").arg(what, re.errorString())};
    return re;
}

ProductProfile load(const QString &bundlePath)
{
    ProductProfile p;
    p.bundlePath = bundlePath;
    const QString resources = bundlePath + QStringLiteral("/Contents/Resources");
    const auto plist = readPlist(bundlePath + QStringLiteral("/Contents/Info.plist"));

    if (!plist.contains(QStringLiteral("SMTProfileFormat")))
        throw Failure{QStringLiteral("Info.plist lacks SMTProfileFormat")};
    if (plist.value(QStringLiteral("SMTProfileFormat")).toInt() != 1)
        throw Failure{QStringLiteral("profile format '%1' needs a newer app").arg(plist.value(QStringLiteral("SMTProfileFormat")))};
    p.id = plist.value(QStringLiteral("CFBundleIdentifier"));
    p.name = plist.value(QStringLiteral("CFBundleName"));
    p.version = plist.value(QStringLiteral("CFBundleShortVersionString"));
    if (p.id.isEmpty() || p.name.isEmpty())
        throw Failure{QStringLiteral("Info.plist lacks CFBundleIdentifier or CFBundleName")};

    const auto lib = readPart(resources, plist, QStringLiteral("SMTProfileLibrary"), true);
    p.folderPattern = compile(lib["folderPattern"].toString(), QStringLiteral("folderPattern"));
    p.sampleFile = compile(lib["sampleFile"].toString(), QStringLiteral("sampleFile"));
    p.samplesDir = QDir::cleanPath(lib["samplesDir"].toString());
    if (lib["folderPattern"].toString().isEmpty())
        throw Failure{QStringLiteral("library.json lacks folderPattern")};
    if (!insideResources(p.samplesDir))
        throw Failure{QStringLiteral("samplesDir '%1' must be a relative path inside the library").arg(p.samplesDir)};
    if (!p.sampleFile.namedCaptureGroups().contains(QStringLiteral("mic")))
        throw Failure{QStringLiteral("sampleFile needs a (?<mic>...) group")};

    const auto micsPart = readPart(resources, plist, QStringLiteral("SMTProfileMics"), true);
    QSet<QString> familyKeys;
    for (const auto &v : micsPart["families"].toArray()) {
        const auto o = v.toObject();
        if (o["key"].toString().isEmpty() || familyKeys.contains(o["key"].toString()))
            throw Failure{QStringLiteral("family key '%1' is empty or duplicate").arg(o["key"].toString())};
        p.families.append({o["key"].toString(), o["name"].toString(), o["tab"].toString()});
        familyKeys.insert(p.families.last().key);
    }
    QSet<QString> tokens;
    for (const auto &v : micsPart["mics"].toArray()) {
        const auto o = v.toObject();
        MicDef m{o["token"].toString().toLower(), o["name"].toString(), o["family"].toString(),
                 o["locked"].toBool(), o["note"].toString()};
        if (m.token.isEmpty())
            throw Failure{QStringLiteral("a mic has no token")};
        if (tokens.contains(m.token))
            throw Failure{QStringLiteral("duplicate mic token '%1'").arg(m.token)};
        if (!familyKeys.contains(m.family))
            throw Failure{QStringLiteral("mic '%1' names unknown family '%2'").arg(m.token, m.family)};
        tokens.insert(m.token);
        p.mics.append(m);
    }
    for (const auto &v : micsPart["quickSets"].toArray()) {
        const auto o = v.toObject();
        QuickSet q{o["name"].toString(), {}};
        for (const auto &f : o["families"].toArray()) {
            if (!familyKeys.contains(f.toString()))
                throw Failure{QStringLiteral("quick set '%1' names unknown family '%2'").arg(q.name, f.toString())};
            q.families.append(f.toString());
        }
        p.quickSets.append(q);
    }

    const auto groupsPart = readPart(resources, plist, QStringLiteral("SMTProfileGroups"), true);
    int dirGroups = 0;
    for (const auto &v : groupsPart["groups"].toArray()) {
        const auto o = v.toObject();
        GroupDef g{o["key"].toString(), o["name"].toString(), o["match"].toString(), false};
        if (g.key.isEmpty() || p.group(g.key))
            throw Failure{QStringLiteral("group key '%1' is empty or duplicate").arg(g.key)};
        const QString by = o["by"].toString();
        if (!by.isEmpty() && by != QLatin1String("dir"))
            throw Failure{QStringLiteral("group '%1': unknown by '%2'").arg(g.key, by)};
        g.byDir = by == QLatin1String("dir");
        dirGroups += g.byDir;
        if (g.match.isEmpty())
            throw Failure{QStringLiteral("group '%1' has an empty match").arg(g.key)};
        p.groups.append(g);
    }
    if (dirGroups != 0 && dirGroups != p.groups.size())
        throw Failure{QStringLiteral("groups mix by-folder and by-name matching")};
    if (dirGroups == 0 && !p.groups.isEmpty() && !p.sampleFile.namedCaptureGroups().contains(QStringLiteral("group")))
        throw Failure{QStringLiteral("sampleFile needs a (?<group>...) group to match groups by name")};

    const auto images = readPart(resources, plist, QStringLiteral("SMTProfileImages"), false);
    for (auto it = images.begin(); it != images.end(); ++it) {
        QStringList sources;
        for (const auto &s : it.value().toObject()["sources"].toArray()) {
            const QString src = s.toString();
            const bool isFile = src.startsWith(QLatin1String("file:"));
            const bool isCache = src.startsWith(QLatin1String("cache:"));
            const bool isUrl = src.startsWith(QLatin1String("url:"));
            if (isCache ? !isPlainCacheName(src.mid(6)) :
                isUrl ? !isAllowedImageUrl(src.mid(4)) :
                isFile ? !insideResources(src.mid(5))
                       : !src.startsWith(QLatin1String("library:")) || !insideResources(src.mid(8)))
                throw Failure{QStringLiteral("image '%1': bad source '%2'").arg(it.key(), src)};
            sources.append(src);
        }
        p.images[it.key()] = sources;
        if (const QString credit = it.value().toObject()["credit"].toString(); !credit.isEmpty()) {
            if (!isAllowedImageUrl(credit))
                throw Failure{QStringLiteral("image '%1': bad credit '%2'").arg(it.key(), credit)};
            p.imageCredits[it.key()] = credit;
        }
    }

    const auto spitfire = readPart(resources, plist, QStringLiteral("SMTProfileSpitfire"), false);
    const auto entries = spitfire["entries"].toArray();
    if (entries.size() > 64)
        throw Failure{QStringLiteral("spitfire.json: more than 64 entries")};
    QSet<QString> entryNames;
    for (const auto &v : entries) {
        const auto o = v.toObject();
        SpitfireEntry e;
        e.name = o["name"].toString();
        if (e.name.isEmpty())
            throw Failure{QStringLiteral("spitfire.json: an entry has no name")};
        if (entryNames.contains(e.name))
            throw Failure{QStringLiteral("spitfire.json: entry '%1' appears twice").arg(e.name)};
        entryNames.insert(e.name);
        const auto keys = o["keys"].toObject();
        if (keys.isEmpty() || keys.size() > 16)
            throw Failure{QStringLiteral("spitfire.json: entry '%1' needs 1 to 16 keys").arg(e.name)};
        for (auto it = keys.begin(); it != keys.end(); ++it) {
            const auto arr = it.value().toArray();
            if (it.key().isEmpty() || !it.value().isArray() || arr.isEmpty() || arr.size() > 16)
                throw Failure{QStringLiteral("spitfire.json: '%1' key '%2' needs a name and 1 to 16 sub-paths").arg(e.name, it.key())};
            QStringList subs;
            for (const auto &sv : arr) {
                const QString sub = sv.toString();
                // "" is the library folder itself; otherwise relative segments, none empty, "." or ".."
                bool bad = !sv.isString() || sub.contains(QLatin1Char('\\'));
                if (!bad && !sub.isEmpty())
                    for (const QString &seg : sub.split(QLatin1Char('/')))
                        bad |= seg.isEmpty() || seg == QLatin1String(".") || seg == QLatin1String("..");
                if (bad)
                    throw Failure{QStringLiteral("spitfire.json: '%1' key '%2': bad sub-path '%3'").arg(e.name, it.key(), sub)};
                subs << sub;
            }
            e.keys.insert(it.key(), subs);
        }
        p.spitfireEntries.append(e);
    }

    if (plist.contains(QStringLiteral("SMTProfileNotes"))) {
        const QString rel = plist.value(QStringLiteral("SMTProfileNotes"));
        if (!insideResources(rel))
            throw Failure{QStringLiteral("SMTProfileNotes: path '%1' leaves Contents/Resources").arg(rel)};
        p.notes = QString::fromUtf8(readFile(resources + QLatin1Char('/') + rel));
    }
    return p;
}

} // namespace

std::optional<ProductProfile> loadProfile(const QString &bundlePath, QString *error)
{
    try {
        return load(bundlePath);
    } catch (const Failure &f) {
        if (error)
            *error = f.msg;
        return std::nullopt;
    }
}

} // namespace smt
