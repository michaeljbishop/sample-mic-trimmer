// copybench <srcDir> <dstDir> [bufferMB] [concurrency] [both|copier|qfile]
// Copies every file under srcDir into dstDir/copybench-copier (copyFiles) and/or
// dstDir/copybench-qfile (QFile::copy, single stream) and prints MB/s for each.
// The second run in "both" reads a warm cache; for fair numbers run each mode separately.
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include "copier.h"

using namespace smt;

static void report(QTextStream &out, const char *what, qint64 bytes, qint64 ms)
{
    out << what << ": " << bytes / (1 << 20) << " MB in " << ms / 1000.0 << " s = "
        << (ms > 0 ? bytes / 1048576.0 / (ms / 1000.0) : 0.0) << " MB/s\n";
    out.flush();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    QTextStream out(stdout);
    if (args.size() < 3) {
        out << "usage: copybench <srcDir> <dstDir> [bufferMB] [concurrency] [both|copier|qfile]\n";
        return 2;
    }
    const QDir src(args[1]);
    CopyOptions opts;
    if (args.size() > 3)
        opts.bufferBytes = args[3].toLongLong() << 20;
    if (args.size() > 4)
        opts.concurrency = args[4].toInt();
    const QString mode = args.size() > 5 ? args[5] : QStringLiteral("both");

    QStringList rels;
    qint64 total = 0;
    QDirIterator it(src.path(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo fi = it.nextFileInfo();
        rels << src.relativeFilePath(fi.filePath());
        total += fi.size();
    }
    out << rels.size() << " files, " << total / (1 << 20) << " MB; buffer " << (opts.bufferBytes >> 20)
        << " MB, concurrency " << opts.concurrency << "\n";

    if (mode != QStringLiteral("qfile")) {
        QDir dst(QDir(args[2]).filePath(QStringLiteral("copybench-copier")));
        dst.removeRecursively();
        QList<CopyJob> jobs;
        for (const QString &rel : rels)
            jobs.append({src.filePath(rel), dst.filePath(rel), QFileInfo(src.filePath(rel)).size(), {}});
        std::atomic_bool cancel{false};
        QString err;
        QElapsedTimer t;
        t.start();
        if (!copyFiles(jobs, opts, {}, cancel, &err)) {
            out << "copyFiles failed: " << err << "\n";
            return 1;
        }
        report(out, "copyFiles", total, t.elapsed());
    }
    if (mode != QStringLiteral("copier")) {
        QDir dst(QDir(args[2]).filePath(QStringLiteral("copybench-qfile")));
        dst.removeRecursively();
        QElapsedTimer t;
        t.start();
        for (const QString &rel : rels) {
            QDir().mkpath(QFileInfo(dst.filePath(rel)).absolutePath());
            if (!QFile::copy(src.filePath(rel), dst.filePath(rel))) {
                out << "QFile::copy failed: " << rel << "\n";
                return 1;
            }
        }
        report(out, "QFile::copy", total, t.elapsed());
    }
    return 0;
}
