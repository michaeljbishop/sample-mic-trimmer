#ifndef FSUTIL_H
#define FSUTIL_H

#include <QString>
#include <filesystem>

// The few file-system primitives whose behaviour differs between macOS/Unix and Windows. The Unix side is the code
// that always ran on macOS; the Windows side (fsutil_win.cpp) is UNVERIFIED: no Windows machine was available.
namespace smt::fs {

// A std::filesystem path for a Qt path. Windows: the wide string (the narrow one would be read in the ANSI code
// page); elsewhere: the local 8-bit encoding, as before.
std::filesystem::path path(const QString &p);

// Atomically replaces `to` with `from` (which must be on the same volume). rename(2) does that on Unix; Windows
// rename fails when the target exists, so MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) is used,
// retried a few times on sharing violations (virus scanners, a plugin that has the file open). On failure *error
// holds the system's message.
bool replaceFile(const QString &from, const QString &to, QString *error);

// Flushes a file's data to disk: fsync(2) / FlushFileBuffers. `fd` is QFile::handle() of a file open for writing.
bool syncDescriptor(int fd, QString *error);

// The same for a file known by path (opened just for this; read-only on Unix, for writing on Windows).
bool syncFile(const QString &path, QString *error);

} // namespace smt::fs

#endif // FSUTIL_H
