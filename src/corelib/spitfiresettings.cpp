#include "spitfiresettings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include "winpath.h"

namespace smt {

namespace testing {

QString pickSpitfireSettingsDir(const QString &envOverride, const QStringList &candidates, bool windows,
                                const std::function<bool(const QString &)> &hasProperties)
{
    if (!envOverride.isEmpty())
        return envOverride;
    if (windows)
        for (const QString &c : candidates)
            if (hasProperties(c))
                return c;
    return candidates.value(0);
}

} // namespace testing

QString spitfireSettingsDir()
{
    const QString env = qEnvironmentVariable("SMT_SPITFIRE_SETTINGS_DIR");
#ifdef Q_OS_WIN
    const QString tail = QStringLiteral("/Spitfire Audio/Settings");
    const QStringList candidates{QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + tail,
                                 QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + tail};
#else
    const QStringList candidates{QDir::homePath() + "/Music/Spitfire Audio/Settings"};
#endif
    return testing::pickSpitfireSettingsDir(env, candidates, winpath::kIsWindows, [](const QString &dir) {
        return QFileInfo::exists(dir + "/Spitfire.properties");
    });
}

QString defaultSpitfireProperties()
{
    return spitfireSettingsDir() + "/Spitfire.properties";
}

QString spitfireImageCacheDir()
{
    return spitfireSettingsDir() + "/.ImageCache";
}

RepointResult repointLibrary(const QString &propertiesFile, const ProductProfile &profile, const QString &libraryRoot)
{
    return testing::repointLibraryFor(winpath::kIsWindows, propertiesFile, profile, libraryRoot);
}

RepointResult testing::repointLibraryFor(bool windows, const QString &propertiesFile, const ProductProfile &profile,
                                         const QString &libraryRoot)
{
    RepointResult r;
    const QString to = windows ? winpath::normalize(libraryRoot) : QDir::cleanPath(libraryRoot);
    if (profile.spitfireEntries.isEmpty()) {
        r.error = "This profile does not describe its Spitfire settings entries.";
        return r;
    }
    if (libraryRoot.trimmed().isEmpty() || !QFileInfo(to).isDir()) {
        r.error = to + " is not a folder.";
        return r;
    }
    const QString leaf = windows ? winpath::leafName(to) : to.section('/', -1);
    const bool isRoot = windows ? winpath::isRoot(to) : to == QLatin1String("/");
    if (isRoot || !profile.folderPattern.match(leaf).hasMatch()) {
        QString pattern = profile.folderPattern.pattern();
        const bool prefix = pattern.startsWith(QLatin1Char('^'));
        if (prefix)
            pattern.remove(0, 1);
        r.error = (isRoot ? to : leaf) + " does not look like a " + profile.name
                  + " library (the folder name must " + (prefix ? "start with \"" : "match \"") + pattern
                  + "\"); choose the matching profile or folder.";
        return r;
    }
    QFile in(propertiesFile);
    if (!in.open(QIODevice::ReadOnly)) {
        r.error = "Cannot read " + propertiesFile + ": " + in.errorString();
        return r;
    }
    const QByteArray original = in.readAll();
    in.close();
    QJsonParseError parseError;
    const QJsonObject before = QJsonDocument::fromJson(original, &parseError).object();
    if (parseError.error != QJsonParseError::NoError || before.isEmpty()) {
        r.error = propertiesFile + " is not the expected JSON object; left unchanged.";
        return r;
    }

    QJsonObject root = before;
    for (const SpitfireEntry &e : profile.spitfireEntries) {
        if (!root[e.name].isObject()) {
            r.error = "Spitfire.properties has no entry \"" + e.name + "\" (install the product in the Spitfire app first); nothing changed.";
            r.products.clear();
            return r;
        }
        QJsonObject entry = root[e.name].toObject();
        for (auto it = e.keys.cbegin(); it != e.keys.cend(); ++it) {
            QStringList wanted;
            for (const QString &sub : it.value())
                wanted << (sub.isEmpty() ? to : windows ? winpath::join(to, sub) : to + '/' + sub);
            if (windows) { // already right under either separator and any case: leave what the file has
                QStringList have;
                for (const QJsonValue &v : entry[it.key()].toArray())
                    have << v.toString();
                if (winpath::samePathLists(have, wanted))
                    continue;
                for (QString &w : wanted)
                    w = winpath::toNative(w); // the Spitfire app on Windows writes backslashes
            }
            QJsonArray paths;
            for (const QString &w : std::as_const(wanted))
                paths.append(w);
            entry[it.key()] = paths;
        }
        root[e.name] = entry;
        r.products << e.name;
    }
    const QString backup = propertiesFile + ".smt-backup";
    if (root == before) { // already pointing there: no write
        if (QFileInfo::exists(backup))
            r.backup = backup;
        return r;
    }

    // Backup first, only when there is none yet: the first one holds the user's original paths and a failed or
    // interrupted write must never lose them.
    if (!QFileInfo::exists(backup)) {
        QSaveFile backupFile(backup);
        if (!backupFile.open(QIODevice::WriteOnly) || backupFile.write(original) != original.size() || !backupFile.commit()) {
            r.error = "Cannot write the backup " + backup + "; nothing changed.";
            r.products.clear();
            return r;
        }
    }
    r.backup = backup;
    QSaveFile out(propertiesFile);
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
        || !out.commit()) {
        r.error = "Cannot write " + propertiesFile + ": " + out.errorString();
        r.products.clear();
        return r;
    }
    r.written = true;
    return r;
}

} // namespace smt
