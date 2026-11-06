#pragma once
#include <QString>
#include <QStringList>
#include <functional>
#include "profile.h"

namespace smt {

// Where the Spitfire app and plugins keep their settings: macOS ~/Music/Spitfire Audio/Settings (verified). The
// location on Windows is UNVERIFIED, so it is resolved in one place (spitfireSettingsDir): the environment variable
// SMT_SPITFIRE_SETTINGS_DIR when set (any platform), else on Windows the first of Music/Spitfire Audio/Settings and
// Documents/Spitfire Audio/Settings that holds a Spitfire.properties (fallback: the Music one).
QString spitfireSettingsDir();

// <settings dir>/Spitfire.properties, where the Spitfire app and plugins keep the folders of every installed library
// (see docs/SPITFIRE-SYSTEM.md).
QString defaultSpitfireProperties();

// <settings dir>/.ImageCache, the Spitfire app's own cache of product pictures (read-only for us).
QString spitfireImageCacheDir();


struct RepointResult
{
    QStringList products; // the profile's entries that now point at the library folder
    bool written = false; // the file was rewritten (false: it already pointed there)
    QString backup;       // the backup of the user's original file, "" when none exists
    QString error;        // "" on success; nothing was written then
    bool ok() const { return error.isEmpty(); }
};

// Points the profile's entries in `propertiesFile` at `libraryRoot`: for every entry and key the profile lists, the
// key becomes [libraryRoot + "/" + sub, ...] ("" = libraryRoot itself), whatever it listed before. Entries and keys
// the profile does not list, and other products, are untouched. Only that file is written (atomically; it is
// re-serialised, see docs/SPITFIRE-SYSTEM.md), and nothing at all when it would not change. Before the first write
// a copy is saved as <file>.smt-backup; an existing backup is never overwritten, so it keeps the user's original.
// An error, with nothing written, if the profile lists no entries, `libraryRoot` is the file system root, not a
// folder or its name does not match the profile's folderPattern (a different product's library), the file cannot be
// read as the expected JSON, or an entry the profile lists is missing from it.
RepointResult repointLibrary(const QString &propertiesFile, const ProductProfile &profile, const QString &libraryRoot);

namespace testing {
// The pure rule behind spitfireSettingsDir(): `envOverride` when not empty; off Windows the first candidate; on
// Windows the first candidate that has a Spitfire.properties (per `hasProperties`), else the first.
QString pickSpitfireSettingsDir(const QString &envOverride, const QStringList &candidates, bool windows,
                                const std::function<bool(const QString &settingsDir)> &hasProperties);
// repointLibrary() with the path rules of a platform flavour (Windows: native separators written, both separators
// and no case read, drive and UNC roots refused), so they are tested on any machine.
RepointResult repointLibraryFor(bool windows, const QString &propertiesFile, const ProductProfile &profile,
                                const QString &libraryRoot);
} // namespace testing

} // namespace smt
