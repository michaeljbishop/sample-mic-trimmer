// Windows side of pathsafety_testing.h: where a folder really is. Builds and is exercised by the tests on a GitHub
// Windows runner (.github/workflows/build.yml); the real mapped-drive, junction and UNC cases are not covered there.
#include "pathsafety_testing.h"
#include <QDir>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vector>

namespace smt::testing {

namespace {

// A handle to a folder or file for asking about it: no data access, shared with everybody. A folder needs
// FILE_FLAG_BACKUP_SEMANTICS to be opened at all.
HANDLE openForQuery(const QString &path)
{
    const std::wstring w = QDir::toNativeSeparators(path).toStdWString();
    return CreateFileW(w.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS, nullptr);
}

} // namespace

std::optional<QString> realFinalPath(const QString &path)
{
    const HANDLE h = openForQuery(path);
    if (h == INVALID_HANDLE_VALUE)
        return std::nullopt;
    std::optional<QString> result;
    const DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    const DWORD needed = GetFinalPathNameByHandleW(h, nullptr, 0, flags); // length including the terminator
    if (needed > 0) {
        std::vector<wchar_t> buf(needed);
        const DWORD written = GetFinalPathNameByHandleW(h, buf.data(), needed, flags); // length without it
        if (written > 0 && written < needed)
            result = QString::fromWCharArray(buf.data(), int(written));
    }
    CloseHandle(h);
    return result;
}

std::optional<FileId> realFileId(const QString &path)
{
    const HANDLE h = openForQuery(path);
    if (h == INVALID_HANDLE_VALUE)
        return std::nullopt;
    BY_HANDLE_FILE_INFORMATION info = {};
    std::optional<FileId> result;
    if (GetFileInformationByHandle(h, &info))
        result = FileId{quint64(info.dwVolumeSerialNumber),
                        (quint64(info.nFileIndexHigh) << 32) | quint64(info.nFileIndexLow)};
    CloseHandle(h);
    return result;
}

} // namespace smt::testing
