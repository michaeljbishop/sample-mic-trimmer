#pragma once
#include <QString>

namespace smt {

// Returns "" when `dest` is a safe place to write a trimmed copy of `source`,
// otherwise a human-readable refusal. The tool deletes files in dest, so any
// overlap with source (same, inside, containing, via symlinks, firmlinks, or a second mount of
// the same share or volume, by statfs mount source) is refused. Reads only, never writes.
QString checkSourceAndDestination(const QString &source, const QString &dest);

// "" unless <source>/.smt.lock or <source>/<a trailing part of dest>/.smt.lock exists: an
// apply's destination lock seen from the source means the source (or a folder in it) is, or
// was after a crash, used as a destination by some other path. Reads only.
QString checkSourceLock(const QString &source, const QString &dest = {});

// "" unless `source` is a Spitfire product folder ("Spitfire Audio - <product>") and `dest` has a
// different folder name: the Spitfire app's Repair/Locate refuses a library folder whose name
// differs from the original ("not a valid library"). Pure string check.
QString checkDestinationName(const QString &source, const QString &dest);

// The folder to use when the user picks `chosen` as the destination: `chosen` itself when it already has the name the
// source needs (see checkDestinationName) or when the source is no Spitfire product folder, else a folder with that
// name inside it, `chosen/<name>`. Pure string work: nothing is searched, renamed or created (the executor creates
// the folder when an apply passes the checks); the result still goes through every check.
QString resolveDestinationFolder(const QString &source, const QString &chosen);

} // namespace smt
