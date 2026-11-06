#pragma once

#include <QString>

// Source and destination folder per profile, plus the last used profile, in QSettings.
class ProfilePaths
{
public:
    QString source(const QString &profileId) const;
    QString destination(const QString &profileId) const;
    void setSource(const QString &profileId, const QString &path);
    void setDestination(const QString &profileId, const QString &path);
    QString lastProfile() const;
    void setLastProfile(const QString &id);
};
