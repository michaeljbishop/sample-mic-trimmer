#include "analysis.h"
#include "coreutil.h"
#include "librarydb.h"
#include <QDir>
#include <QHash>
#include <QMap>
#include <QSet>

namespace smt {

Analysis analyzeLibrary(const ProductProfile &profile, const QString &sourceRoot, const QList<FileInfo> &scanned)
{
    Analysis out;
    QHash<QString, qint64> sizes;
    for (const FileInfo &f : scanned) {
        sizes.insert(f.path, qint64(f.size));
        if (isDatabasePath(f.path)) out.dbsFound << f.path;
    }
    out.dbsFound.sort();

    // (group index, token) -> summary; the map keeps the output ordered. A set de-duplicates names across DBs.
    QMap<QPair<int, QString>, MicSummary> mics;
    QSet<QString> seen, unknown, missing, noMic;
    for (const QString &db : std::as_const(out.dbsFound)) {
        QString err;
        const QStringList names = librarydb::containerNames(QDir(sourceRoot).filePath(db), &err);
        if (!err.isEmpty()) {
            out.warnings << "Could not read " + db + ": " + err;
            continue;
        }
        const int slash = db.lastIndexOf('/');
        const QString dir = slash < 0 ? QString() : db.left(slash + 1);
        for (const QString &name : names) {
            const QString path = dir + name;
            const Classified c = classify(profile, path);
            if (c.kind != FileKind::Mic && path.endsWith(".spitfire", Qt::CaseInsensitive) && !noMic.contains(path)) {
                noMic.insert(path);
                ++out.noMicFiles;
                if (out.noMicExamples.size() < 3) out.noMicExamples << name;
            }
            // No group: not a sample of a known section/voice group (e.g. a DB outside the section folders).
            if (c.micToken.isEmpty() || c.key.group.isEmpty() || seen.contains(path)) continue;
            seen.insert(path);
            const GroupDef *group = profile.group(c.key.group);
            MicSummary &m = mics[{int(group - profile.groups.constData()), c.micToken}];
            if (m.token.isEmpty()) {
                m.token = c.micToken;
                m.group = group->key;
                m.groupName = group->name;
                m.known = c.kind == FileKind::Mic;
                m.mic = c.key.mic;
                m.name = m.known ? profile.mic(c.key.mic)->name : c.micToken;
                if (!m.known) unknown.insert(c.micToken);
            }
            ++m.fileCount;
            if (auto it = sizes.constFind(path); it != sizes.constEnd())
                m.bytes += it.value();
            else
                missing.insert(path);
        }
    }
    out.mics = mics.values();
    out.unknownTokens = QStringList(unknown.cbegin(), unknown.cend());
    out.unknownTokens.sort();
    out.filesMissingFromDisk = QStringList(missing.cbegin(), missing.cend());
    out.filesMissingFromDisk.sort();
    return out;
}

QString Analysis::toText() const
{
    QStringList lines;
    lines << QString("Databases: %1").arg(dbsFound.size());
    QMap<QString, int> unknownFiles;
    for (const MicSummary &m : mics) {
        if (!m.known) {
            unknownFiles[m.token] += m.fileCount;
            continue;
        }
        lines << QString("%1 / %2 (%3): %4 files, %5")
                     .arg(m.groupName, m.name, m.token)
                     .arg(m.fileCount)
                     .arg(formatByteSize(size_t(m.bytes)));
    }
    for (const QString &w : warnings) lines << "Warning: " + w;
    if (!unknownFiles.isEmpty()) {
        QStringList codes;
        for (auto it = unknownFiles.cbegin(); it != unknownFiles.cend(); ++it)
            codes << QString("%1 (%2 files)").arg(it.key()).arg(it.value());
        lines << "Unknown mic codes (kept, never removed): " + codes.join(", ");
    }
    if (noMicFiles > 0)
        lines << QString("%1 sample files match no mic/group of this profile (kept): e.g. %2")
                     .arg(noMicFiles).arg(noMicExamples.join(", "));
    if (!filesMissingFromDisk.isEmpty()) {
        lines << QString("%1 files listed in databases are missing from disk (source may already be trimmed):")
                     .arg(filesMissingFromDisk.size());
        for (const QString &p : filesMissingFromDisk) lines << "  " + p;
    }
    return lines.join('\n') + '\n';
}

} // namespace smt
