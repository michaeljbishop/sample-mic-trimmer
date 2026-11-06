#pragma once
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include "corelib.h"
#include "profile.h"
#include "profileregistry.h"

// Every unlocked mic off for every group (a starting state for tests).
inline void disableAllMics(smt::Core &core)
{
    smt::forAllMicSignalGroupKeys(core.profile(), [&](const smt::MicSignalGroupKey &k) { core.setMicEnabled(k, false); });
}

// No built-in profile ships a quick set; this copies a built-in bundle (compiled-in resources) into dir and adds
// "Close, Tree, Ambient, Outriggers" (families close/tree/ambient/outriggers) to its mics.json, so the quick-set
// machinery stays tested.
inline smt::ProductProfile profileWithQuickSet(const QString &builtInId, QTemporaryDir &dir)
{
    const smt::ProductProfile src = smt::builtInProfile(builtInId);
    const QString dst = dir.path() + "/Quick.smtprofile";
    QDirIterator it(src.bundlePath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString from = it.next();
        const QString to = dst + from.mid(src.bundlePath.size());
        if (!QDir().mkpath(QFileInfo(to).path()) || !QFile::copy(from, to))
            qFatal("profileWithQuickSet: cannot copy %s", qPrintable(from));
        QFile::setPermissions(to, QFile::ReadOwner | QFile::WriteOwner);
    }
    const QString micsPath = dst + "/Contents/Resources/mics.json";
    QFile f(micsPath);
    if (!f.open(QIODevice::ReadWrite))
        qFatal("profileWithQuickSet: cannot open %s", qPrintable(micsPath));
    QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    o["quickSets"] = QJsonArray{QJsonObject{{"name", "Close, Tree, Ambient, Outriggers"},
                                            {"families", QJsonArray{"close", "tree", "ambient", "outriggers"}}}};
    const QByteArray out = QJsonDocument(o).toJson();
    f.seek(0);
    f.resize(0);
    if (f.write(out) != out.size())
        qFatal("profileWithQuickSet: cannot write %s", qPrintable(micsPath));
    f.close();
    QString error;
    const auto p = smt::loadProfile(dst, &error);
    if (!p)
        qFatal("profileWithQuickSet: %s", qPrintable(error));
    return *p;
}
