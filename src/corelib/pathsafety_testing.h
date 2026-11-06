#pragma once
// Test-only hooks of pathsafety: lets tests simulate a second mount of one share.
#include <QString>
#include <functional>
#include <optional>

namespace smt::testing {

// Where a canonical path lives: the mount source (statfs f_mntfromname, e.g. "//user@server/Share"
// or "/dev/disk3s5") and the path below that mount's root, without leading or trailing '/'.
struct MountId
{
    QString from;
    QString rel;
};
using MountIdProvider = std::function<std::optional<MountId>(const QString &canonicalPath)>;

void setMountIdProvider(MountIdProvider provider); // {} restores the real one
std::optional<MountId> realMountId(const QString &canonicalPath); // statfs; nullopt if unsupported

// A directory's identity that a file server keeps the same across mounts: inode and birth time.
// dev tells the mounts apart (same-dev identity is the equivalent() check's job).
struct StatId
{
    quint64 dev = 0, ino = 0;
    qint64 birthSec = 0, birthNsec = 0;
};
using StatIdProvider = std::function<std::optional<StatId>(const QString &path)>;

void setStatIdProvider(StatIdProvider provider); // {} restores the real one
// stat(); nullopt unless a directory with a non-zero birth time (macOS/BSD only)
std::optional<StatId> realStatId(const QString &path);

// Windows: where a path really is. The final path (GetFinalPathNameByHandle) resolves mapped drive letters, subst
// drives, junctions and symlinks, so two spellings of one folder, "Z:\\Lib" and "\\\\server\\share\\Lib", end up equal.
// nullopt when it cannot be told (always, off Windows); on Windows that makes the check refuse.
using FinalPathProvider = std::function<std::optional<QString>(const QString &path)>;
void setFinalPathProvider(FinalPathProvider provider); // {} restores the real one
std::optional<QString> realFinalPath(const QString &path); // pathsafety_win.cpp; nullopt elsewhere

// A folder's identity on Windows: the volume serial number and the 64-bit file index from
// GetFileInformationByHandle (open with FILE_FLAG_BACKUP_SEMANTICS).
struct FileId
{
    quint64 volume = 0, index = 0;
    bool operator==(const FileId &o) const { return volume == o.volume && index == o.index; }
};
using FileIdProvider = std::function<std::optional<FileId>(const QString &path)>;
void setFileIdProvider(FileIdProvider provider); // {} restores the real one
std::optional<FileId> realFileId(const QString &path); // pathsafety_win.cpp; nullopt elsewhere

// The path-string rules for a given platform flavour, so Windows-shaped inputs are tested on any machine. The
// public checkDestinationName() / resolveDestinationFolder() call these with the build's own flavour.
QString checkDestinationNameFor(bool windows, const QString &source, const QString &dest);
QString resolveDestinationFolderFor(bool windows, const QString &source, const QString &chosen);

} // namespace smt::testing
