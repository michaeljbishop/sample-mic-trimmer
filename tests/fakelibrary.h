#pragma once
#include <QDir>
#include <QDirIterator>
#include "profileregistry.h"
#include "smt.h"
#include "testlib.h"

// BBCSO group key ("brass") -> DB names. Woodwinds has two DBs (V020 lists every file,
// V115 only the odd instruments), like the real library.
inline QStringList fakeDbNames(const QString &group)
{
    if (group == "brass") return {"BBCSO_BRASS_V020.db"};
    if (group == "percussion") return {"BBBCSO_PERC_v020.db"};
    if (group == "strings") return {"BBCSO_STRINGS_v022.db"};
    if (group == "woodwinds") return {"BBCSO_WOODS_V020.db", "BBCSO_WOODS_V115.db"};
    return {};
}

// "Samples/_Brass" for "brass": the BBCSO profile's folder for the group.
inline QString fakeSectionDir(const QString &group)
{
    const smt::ProductProfile p = smt::builtInProfile(smt::kBbcsoProfileId);
    const smt::GroupDef *g = p.group(group);
    return g ? "Samples/" + g->match : QString();
}

// Creates Samples/_<Section>/INSTR<n>_<TOKEN>_01.spitfire for every token and BBCSO group key, one non-mic
// IR1.spitfire per section, and the section DB(s) listing the files. Returns root.
inline QString makeFakeLibrary(const QString &root, const QStringList &micTokens,
                               const QStringList &sections, int instrumentsPerSection,
                               qint64 fileSize)
{
    for (const auto &s : sections) {
        const QString dir = root + "/" + fakeSectionDir(s);
        QDir().mkpath(dir);
        QStringList all{"IR1.spitfire"}, odd{"IR1.spitfire"};
        for (int n = 1; n <= instrumentsPerSection; ++n)
            for (const auto &t : micTokens) {
                const QString name = QString("INSTR%1_%2_01.spitfire").arg(n).arg(t);
                all << name;
                if (n % 2)
                    odd << name;
            }
        for (const auto &name : all)
            writeFile(dir + "/" + name, fileSize);
        const QStringList dbs = fakeDbNames(s);
        for (int i = 0; i < dbs.size(); ++i)
            makeDb(dir + "/" + dbs[i], i == 0 ? all : odd);
    }
    return root;
}

// Every file under root, relative '/' paths, as the scanner would report them.
inline QList<smt::FileInfo> scanTree(const QString &root)
{
    QList<smt::FileInfo> out;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo fi = it.nextFileInfo();
        smt::FileInfo f{};
        f.path = QDir(root).relativeFilePath(fi.filePath());
        f.size = size_t(fi.size());
        f.modificationTime = fi.lastModified();
        out << f;
    }
    return out;
}
