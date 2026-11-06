#pragma once
#include <QtTest>
#include <QFileInfo>

// Tests of symbolic links, mounts, firmlinks, POSIX permissions and hard links only make sense on macOS/BSD/Unix.
// On Windows they are skipped, never deleted (the Windows side has its own pure-function tests: tst_winpath, the
// Windows-flavoured cases in tst_pathsafety and tst_spitfiresettings).
#ifdef Q_OS_WIN
#define SMT_UNIX_ONLY() QSKIP("macOS/BSD/Unix only")
#else
#define SMT_UNIX_ONLY() (void)0
#endif

// NTFS updates a directory's modification time lazily, so a "nothing changed" snapshot compares directory mtimes on
// macOS/Unix only (names and file sizes and mtimes still are compared everywhere).
inline qint64 snapshotMtime(const QFileInfo &fi)
{
#ifdef Q_OS_WIN
    if (fi.isDir())
        return 0;
#endif
    return fi.lastModified().toMSecsSinceEpoch();
}
