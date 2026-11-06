#include "smt.h"
#include <QDebug>

namespace smt {

bool isDatabasePath(const QString &relPath)
{
    // Only section DBs (under Samples/) are patched; any other .db is copied like any file.
    return relPath.endsWith(".db", Qt::CaseInsensitive) && relPath.startsWith("Samples/", Qt::CaseInsensitive);
}

Classified classify(const ProductProfile &profile, const QString &relPath)
{
    Classified c;
    if (isDatabasePath(relPath)) {
        c.kind = FileKind::Database;
        return c;
    }
    if (!relPath.endsWith(".spitfire", Qt::CaseInsensitive))
        return c; // only sample files can be mics, whatever a loose profile regex matches
    if (!relPath.startsWith(profile.samplesDir + '/'))
        return c;
    const QStringList parts = relPath.split('/');
    const auto m = profile.sampleFile.match(parts.last());
    if (!m.hasMatch())
        return c;
    c.micToken = m.captured("mic").toUpper();
    const QString capture = m.captured("group");
    const QString dir = parts.size() >= 2 ? parts[parts.size() - 2] : QString();
    for (const GroupDef &g : profile.groups) {
        if (g.byDir ? g.match == dir : (!capture.isEmpty() && g.match.compare(capture, Qt::CaseInsensitive) == 0)) {
            c.key.group = g.key;
            break;
        }
    }
    if (profile.mic(c.micToken.toLower()))
        c.key.mic = c.micToken.toLower();
    if (!c.key.mic.isEmpty() && !c.key.group.isEmpty())
        c.kind = FileKind::Mic;
    return c;
}

QDebug operator<<(QDebug debug, const MicSignalGroupKey &c)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << '(' << c.mic << "/" << c.group << ')';
    return debug;
}

void forAllMicSignalGroupKeys(const ProductProfile &profile, const std::function<void(const MicSignalGroupKey &)> &f)
{
    for (const auto &mic : profile.mics)
        for (const auto &group : profile.groups)
            f({mic.token, group.key});
}

} // namespace smt
