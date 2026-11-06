// Windows side of fsutil.h. Builds and is exercised by the tests on a GitHub Windows runner (see .github/workflows/build.yml); never run on a real Spitfire setup.
#include "fsutil.h"
#include <QDir>
#include <QFile>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>

namespace smt::fs {

namespace {

QString systemMessage(DWORD code)
{
    return QString::fromStdString(std::error_code(int(code), std::system_category()).message());
}

} // namespace

std::filesystem::path path(const QString &p)
{
    return std::filesystem::path(p.toStdWString());
}

bool replaceFile(const QString &from, const QString &to, QString *error)
{
    const std::wstring wfrom = QDir::toNativeSeparators(from).toStdWString();
    const std::wstring wto = QDir::toNativeSeparators(to).toStdWString();
    DWORD code = ERROR_SUCCESS;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (MoveFileExW(wfrom.c_str(), wto.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
        code = GetLastError();
        // A sharing violation or access denied is often transient (a virus scanner, a plugin reading the file).
        if (code != ERROR_SHARING_VIOLATION && code != ERROR_ACCESS_DENIED && code != ERROR_LOCK_VIOLATION)
            break;
        Sleep(50);
    }
    if (error)
        *error = systemMessage(code);
    return false;
}

bool syncDescriptor(int fd, QString *error)
{
    const intptr_t handle = _get_osfhandle(fd);
    if (handle == -1 || !FlushFileBuffers(reinterpret_cast<HANDLE>(handle))) {
        if (error)
            *error = systemMessage(GetLastError());
        return false;
    }
    return true;
}

bool syncFile(const QString &path, QString *error)
{
    const std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();
    const HANDLE h = CreateFileW(wpath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (error)
            *error = systemMessage(GetLastError());
        return false;
    }
    const bool ok = FlushFileBuffers(h) != 0;
    const DWORD code = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(h);
    if (!ok && error)
        *error = systemMessage(code);
    return ok;
}

} // namespace smt::fs
