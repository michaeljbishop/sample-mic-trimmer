#include "fsutil.h"
#include <QFile>
#include <cerrno>
#include <cstring>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace smt::fs {

#ifndef Q_OS_WIN // the Windows versions are in fsutil_win.cpp

std::filesystem::path path(const QString &p)
{
    return std::filesystem::path(QFile::encodeName(p).constData());
}

bool replaceFile(const QString &from, const QString &to, QString *error)
{
    std::error_code ec;
    std::filesystem::rename(path(from), path(to), ec);
    if (ec && error)
        *error = QString::fromStdString(ec.message());
    return !ec;
}

bool syncDescriptor(int fd, QString *error)
{
#ifdef Q_OS_UNIX
    if (::fsync(fd) != 0) {
        if (error)
            *error = QString::fromLocal8Bit(strerror(errno));
        return false;
    }
#else
    Q_UNUSED(fd);
    Q_UNUSED(error);
#endif
    return true;
}

bool syncFile(const QString &path, QString *error)
{
#ifdef Q_OS_UNIX
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return syncDescriptor(f.handle(), error);
#else
    Q_UNUSED(path);
    Q_UNUSED(error);
    return true;
#endif
}

#endif // !Q_OS_WIN

} // namespace smt::fs
