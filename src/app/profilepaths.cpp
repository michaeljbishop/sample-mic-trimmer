#include "profilepaths.h"

#include <QSettings>

namespace {
// Only BBCSO existed when the global keys were written.
const QString kLegacyProfileId = "name.mbishop.smtprofile.bbcso";
const QString kSource = "SourceDirectory"; // also the legacy global key
const QString kDestination = "BackupDirectory"; // also the legacy global key
const QString kLastProfile = "LastProfile";

// Dots and slashes in an id must not become key paths in any QSettings backend.
QString key(const QString &id, const QString &name)
{
    QString safe = id;
    safe.replace('%', "%25").replace('/', "%2F").replace('.', "%2E");
    return "Profiles/" + safe + "/" + name;
}

QString read(const QString &id, const QString &name)
{
    QSettings s;
    const QString k = key(id, name);
    if (s.contains(k))
        return s.value(k).toString();
    return id == kLegacyProfileId ? s.value(name).toString() : QString();
}
} // namespace

QString ProfilePaths::source(const QString &profileId) const
{
    return read(profileId, kSource);
}

QString ProfilePaths::destination(const QString &profileId) const
{
    return read(profileId, kDestination);
}

void ProfilePaths::setSource(const QString &profileId, const QString &path)
{
    QSettings().setValue(key(profileId, kSource), path);
}

void ProfilePaths::setDestination(const QString &profileId, const QString &path)
{
    QSettings().setValue(key(profileId, kDestination), path);
}

QString ProfilePaths::lastProfile() const
{
    return QSettings().value(kLastProfile).toString();
}

void ProfilePaths::setLastProfile(const QString &id)
{
    QSettings().setValue(kLastProfile, id);
}
