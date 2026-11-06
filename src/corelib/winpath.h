#ifndef WINPATH_H
#define WINPATH_H

#include <QString>
#include <QStringList>

// Pure string helpers for Windows-shaped paths ("C:\x\y", "C:/x/y", "//server/share/x", "\\?\UNC\server\share\x",
// "\\?\C:\x"). They compile and run on every platform, so Windows path logic is unit-tested on macOS; only the
// callers decide when to use them (kIsWindows, or a test passing true). Nothing here touches the file system.
// UNVERIFIED on a real Windows machine.
namespace smt::winpath {

#ifdef Q_OS_WIN
constexpr bool kIsWindows = true;
#else
constexpr bool kIsWindows = false;
#endif

// Forward slashes, "\\?\" and "\\.\" prefixes removed ("\\?\UNC\srv\share" becomes "//srv/share"), repeated and "."
// segments dropped, a trailing slash dropped except on a root ("C:/", "/"). ".." is kept as it is, nothing is
// resolved. No case change.
QString normalize(const QString &path);

// normalize() lower-cased: what two paths are compared by (Windows file names are case-insensitive).
QString key(const QString &path);

bool isRoot(const QString &path); // "C:/", "C:", "/", "//server/share"

QString leafName(const QString &path); // last segment of normalize(path); "" for a root

// dir + "/" + name without doubling the slash of a root ("C:/" + "x" = "C:/x").
QString join(const QString &dir, const QString &name);

// Any ".." segment when both "/" and "\" separate segments (backslashIsSeparator) or only "/".
bool hasDotDotSegment(const QString &path, bool backslashIsSeparator);

enum class Relation { Unrelated, Same, AInsideB, BInsideA };
Relation relate(const QString &a, const QString &b); // by key(): case-insensitive, either separator

// The two lists name the same paths in the same order, by key().
bool samePathLists(const QStringList &a, const QStringList &b);

QString toNative(const QString &path); // "/" to "\"

} // namespace smt::winpath

#endif // WINPATH_H
