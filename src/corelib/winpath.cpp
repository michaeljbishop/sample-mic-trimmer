#include "winpath.h"

namespace smt::winpath {

QString normalize(const QString &path)
{
    QString p = path;
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    bool unc = false;
    if (p.startsWith(QLatin1String("//?/")) || p.startsWith(QLatin1String("//./"))) {
        p.remove(0, 4);
        if (p.startsWith(QLatin1String("UNC/"), Qt::CaseInsensitive)) {
            p.remove(0, 4);
            unc = true;
        }
    } else if (p.startsWith(QLatin1String("//")) && !p.startsWith(QLatin1String("///"))) {
        unc = true;
        p.remove(0, 2);
    }
    QStringList out;
    for (const QString &seg : p.split(QLatin1Char('/'), Qt::SkipEmptyParts))
        if (seg != QLatin1String("."))
            out << seg;
    const bool absolute = p.startsWith(QLatin1Char('/'));
    if (unc)
        return QLatin1String("//") + out.join(QLatin1Char('/'));
    if (out.size() == 1 && out.first().size() == 2 && out.first().at(1) == QLatin1Char(':')) // "C:" is the drive root
        return out.first() + QLatin1Char('/');
    if (!out.isEmpty() && out.first().size() == 2 && out.first().at(1) == QLatin1Char(':'))
        return out.join(QLatin1Char('/'));
    return (absolute ? QStringLiteral("/") : QString()) + out.join(QLatin1Char('/'));
}

QString key(const QString &path)
{
    return normalize(path).toLower();
}

bool isRoot(const QString &path)
{
    const QString n = normalize(path);
    if (n == QLatin1String("/"))
        return true;
    if (n.size() == 3 && n.at(1) == QLatin1Char(':') && n.at(2) == QLatin1Char('/'))
        return true;
    if (n.startsWith(QLatin1String("//"))) // //server/share
        return n.mid(2).split(QLatin1Char('/')).size() <= 2;
    return false;
}

QString leafName(const QString &path)
{
    if (isRoot(path))
        return {};
    const QString n = normalize(path);
    return n.mid(n.lastIndexOf(QLatin1Char('/')) + 1);
}

QString join(const QString &dir, const QString &name)
{
    const QString d = normalize(dir);
    return d.endsWith(QLatin1Char('/')) ? d + name : d + QLatin1Char('/') + name;
}

bool hasDotDotSegment(const QString &path, bool backslashIsSeparator)
{
    QString p = path;
    if (backslashIsSeparator)
        p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return p.split(QLatin1Char('/')).contains(QLatin1String(".."));
}

Relation relate(const QString &a, const QString &b)
{
    const QString ka = key(a), kb = key(b);
    if (ka == kb)
        return Relation::Same;
    const auto inside = [](const QString &x, const QString &dir) {
        return x.startsWith(dir.endsWith(QLatin1Char('/')) ? dir : dir + QLatin1Char('/'));
    };
    if (inside(ka, kb))
        return Relation::AInsideB;
    if (inside(kb, ka))
        return Relation::BInsideA;
    return Relation::Unrelated;
}

bool samePathLists(const QStringList &a, const QStringList &b)
{
    if (a.size() != b.size())
        return false;
    for (qsizetype i = 0; i < a.size(); ++i)
        if (key(a[i]) != key(b[i]))
            return false;
    return true;
}

QString toNative(const QString &path)
{
    QString p = path;
    p.replace(QLatin1Char('/'), QLatin1Char('\\'));
    return p;
}

} // namespace smt::winpath
