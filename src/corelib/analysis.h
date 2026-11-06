#pragma once
#include "smt.h"
#include <QList>
#include <QString>
#include <QStringList>

namespace smt {

struct MicSummary
{
    QString mic;       // profile token, e.g. "tr01"; empty when unknown
    QString group;     // group key, e.g. "brass"
    QString token;     // upper-case, e.g. "TR01"
    QString name;      // mic name, or the raw token when unknown
    QString groupName; // e.g. "Brass"
    int fileCount = 0;
    qint64 bytes = 0;
    bool known = false;
};

struct Analysis
{
    QList<MicSummary> mics; // sorted by profile group order, then token
    QStringList unknownTokens;
    int noMicFiles = 0; // DB-listed .spitfire files that are no mic of the profile (kept)
    QStringList noMicExamples; // up to 3 names
    QStringList dbsFound;
    QStringList filesMissingFromDisk; // DB entries with no scanned file: the source may already be trimmed
    QStringList warnings;             // e.g. a .db that could not be opened
    QString toText() const;
};

// Read-only on the source (opens .db files read-only).
Analysis analyzeLibrary(const ProductProfile &profile, const QString &sourceRoot, const QList<FileInfo> &scanned);

} // namespace smt
