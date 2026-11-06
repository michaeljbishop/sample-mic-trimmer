#pragma once
#include <functional>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include "profile.h"

namespace smt {

struct MicSignalGroupKey
{
    QString mic;   // lower-case token, e.g. "tr01"
    QString group; // group key, e.g. "brass"
};

typedef QHash<QString, QHash<QString, bool>> MicStatuses; // mic -> group -> enabled

QDebug operator<<(QDebug debug, const MicSignalGroupKey &c);

inline bool operator==(const MicSignalGroupKey &a, const MicSignalGroupKey &b)
{
    return a.mic == b.mic && a.group == b.group;
}
inline size_t qHash(const MicSignalGroupKey &key, size_t seed)
{
    return qHashMulti(seed, key.mic, key.group);
}

enum class FileKind { Mic, Database, Other };
struct Classified
{
    FileKind kind = FileKind::Other;
    QString micToken;      // upper-case mic text found even when not a listed mic
    MicSignalGroupKey key; // mic set only when listed, group whenever one matches; both set for Mic
};
Classified classify(const ProductProfile &profile, const QString &relPath);
bool isDatabasePath(const QString &relPath);
void forAllMicSignalGroupKeys(const ProductProfile &profile, const std::function<void(const MicSignalGroupKey &)> &f);

struct FileInfo
{
    QString path;
    size_t size;
    QDateTime modificationTime;
    QDateTime creationTime;
    QDateTime metadataModificationTime;
    QFile::Permissions permissions;
};

}; // namespace smt
