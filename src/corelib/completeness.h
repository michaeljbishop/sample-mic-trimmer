#pragma once
#include <QHash>
#include <QList>
#include "smt.h"

namespace smt {

// Empty: no file of the key in the destination (or the source has none). Partial: some, but not
// every source file is there with the same size. Complete: every source file is there.
enum class Completeness { Empty, Partial, Complete };
typedef QHash<QString, QHash<QString, Completeness>> MicCompleteness; // mic -> group -> state

// An explicit state for every {mic, group} key of the profile. With an empty source list (not
// scanned) any destination file of a key makes it Complete (no Partial without source info).
// Raw facts: locked mics are not forced here.
MicCompleteness completeMics(const ProductProfile &profile, const QList<FileInfo> &source, const QList<FileInfo> &dest);

} // namespace smt
