#pragma once
#include <QDir>
#include <QString>

// MainWindow's rules for Core::configurationError: show each distinct message once until re-armed
// (destination changed or loaded cleanly); re-choosing the current folder only matters while an
// error is active, since Core then reloads project.smt, and otherwise would drop unsaved toggles.
class ConfigErrorNotifier
{
public:
    bool shouldShow(const QString &message)
    {
        if (message == mShown)
            return false;
        mShown = message;
        return true;
    }
    void rearm() { mShown.clear(); }
    bool shouldSetDestination(const QString &chosen, const QString &current) const
    {
        return QDir::cleanPath(chosen) != QDir::cleanPath(current) || !mShown.isEmpty();
    }

private:
    QString mShown; // non-empty while a config error is active
};
