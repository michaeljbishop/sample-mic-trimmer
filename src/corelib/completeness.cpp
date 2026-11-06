#include "completeness.h"

namespace smt {

MicCompleteness completeMics(const ProductProfile &profile, const QList<FileInfo> &source, const QList<FileInfo> &dest)
{
    // classify() only accepts <samples dir>/...spitfire, so staging, .part, project.smt, the lock
    // and .DS_Store never count.
    QHash<QString, qint64> destSizes;
    QHash<MicSignalGroupKey, int> destCount;
    for (const FileInfo &f : dest) {
        const Classified c = classify(profile, f.path);
        if (c.kind != FileKind::Mic)
            continue;
        destSizes.insert(f.path, qint64(f.size));
        ++destCount[c.key];
    }
    QHash<MicSignalGroupKey, int> total, present;
    for (const FileInfo &f : source) {
        const Classified c = classify(profile, f.path);
        if (c.kind != FileKind::Mic)
            continue;
        ++total[c.key];
        present[c.key] += destSizes.value(f.path, -1) == qint64(f.size);
    }

    MicCompleteness result;
    forAllMicSignalGroupKeys(profile, [&](const MicSignalGroupKey &key) {
        Completeness c = Completeness::Empty;
        if (source.isEmpty())
            c = destCount.value(key) > 0 ? Completeness::Complete : Completeness::Empty;
        else if (total.value(key) > 0)
            c = present.value(key) == total.value(key) ? Completeness::Complete
                : (present.value(key) > 0 || destCount.value(key) > 0) ? Completeness::Partial
                                                                       : Completeness::Empty;
        result[key.mic][key.group] = c;
    });
    return result;
}

} // namespace smt
