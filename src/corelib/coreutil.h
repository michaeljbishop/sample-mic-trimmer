#ifndef COREUTIL_H
#define COREUTIL_H
#include <QLocale>
#include <QString>
#include <optional>
#include <QStringList>

inline QString formatByteSize(size_t bytes)
{
    static const QStringList units = {"bytes", "KB", "MB", "GB", "TB", "PB", "EB"};
    double size = static_cast<double>(bytes);
    int unitIndex = 0;

    while (size >= 1024.0 && unitIndex < units.size() - 1) {
        size /= 1024.0;
        ++unitIndex;
    }

    // Use 0 decimal places for bytes, 1 decimal for others
    return unitIndex == 0 ? QString::number(static_cast<size_t>(size)) + " " + units[unitIndex]
                          : QString("%1 %2").arg(size, 0, 'f', 1).arg(units[unitIndex]);
}

// The GUI footer: "Installation size: 12.3 GB (45.6 GB space remaining)"; without a destination just the size,
// when the destination is too small "(Needs 1.0 GB more space)".
inline QString installationSizeText(size_t installationBytes, std::optional<qint64> remainingBytes)
{
    QString text = QStringLiteral("Installation size: ") + formatByteSize(installationBytes);
    if (remainingBytes)
        text += *remainingBytes < 0 ? QStringLiteral(" (Needs %1 more space)").arg(formatByteSize(size_t(-*remainingBytes)))
                                    : QStringLiteral(" (%1 space remaining)").arg(formatByteSize(size_t(*remainingBytes)));
    return text;
}

// The footer's state: the free space left after Apply, unknown until a destination is known (and again once it is
// cleared, so a stale figure never sits next to a new size).
struct InstallationFooter
{
    std::optional<qint64> remaining;
    QString text(size_t installationBytes) const { return installationSizeText(installationBytes, remaining); }
    bool needsMoreSpace() const { return remaining && *remaining < 0; }
};

// What Apply would do now, for the button: "Apply (+1.2 GB / \u2212300.0 MB)", "Apply (update databases)", "No changes".
struct PendingChange
{
    bool known = false;    // false while a scan runs, or before anything was computed: plain "Apply", disabled
    bool refused = false;  // the plan cannot run (overlap, name, lock, config, no destination, not enough space, ...)
    QString firstRefusal;  // why, for the tooltip
    bool hasSteps = false; // any step at all, database rebuilds included (they resume an interrupted apply)
    bool any = false;      // some file would be copied or deleted
    qint64 add = 0, remove = 0;
};

inline QString applyButtonText(const PendingChange &c)
{
    if (!c.known)
        return QStringLiteral("Apply");
    if (c.any)
        return QStringLiteral("Apply (+%1 / \u2212%2)").arg(formatByteSize(size_t(c.add)), formatByteSize(size_t(c.remove)));
    if (c.hasSteps)
        return QStringLiteral("Apply (update databases)");
    return c.refused ? QStringLiteral("Apply") : QStringLiteral("No changes");
}

// The widest text applyButtonText() can give, for sizing the button once.
inline PendingChange longestPendingChange()
{
    PendingChange c;
    c.known = c.any = c.hasSteps = true;
    c.add = c.remove = qint64(1023.9 * 1024 * 1024 * 1024); // "1023.9 GB"
    return c;
}

// Apply needs a settled state (no scan running) and a plan that can run and has a step.
inline bool applyEnabled(const PendingChange &c)
{
    return c.known && !c.refused && c.hasSteps;
}

// Which scans run and how far they are; sizes -1 = unknown.
struct ScanStatus
{
    bool source = false, dest = false;
    bool preparing = false; // not scanning, but reading the destination's configuration or working out what is complete
    qint64 sourceFiles = 0, sourceExpected = -1, destFiles = 0, destExpected = -1;
    bool any() const { return source || dest; }
    bool busy() const { return source || dest || preparing; }
};

// "Scanning Source… 42%", "Scanning Source… 12,345 files", "Scanning Reduced Mic Installation…"; both = combined.
// The percentage stays below 100 until the scan has finished.
inline QString scanStatusText(const ScanStatus &s)
{
    auto part = [](const QString &what, qint64 files, qint64 expected) {
        QString t = what + QStringLiteral("\u2026");
        if (expected > 0)
            t += QStringLiteral(" %1%").arg(qMin<qint64>(99, files * 100 / expected));
        else if (files > 0)
            t += QStringLiteral(" %1 files").arg(QLocale().toString(files));
        return t;
    };
    QStringList parts;
    if (s.source)
        parts << part(QStringLiteral("Source"), s.sourceFiles, s.sourceExpected);
    if (s.dest)
        parts << part(QStringLiteral("Reduced Mic Installation"), s.destFiles, s.destExpected);
    if (parts.isEmpty())
        return s.preparing ? QStringLiteral("Preparing\u2026") : QString();
    return QStringLiteral("Scanning ") + parts.join(QStringLiteral(" \u00b7 "));
}

// The main window's title, "Sample Mic Trimmer \u2014 <profile>".
inline QString mainWindowTitle(const QString &appName, const QString &profileName)
{
    return profileName.isEmpty() ? appName : appName + QStringLiteral(" \u2014 ") + profileName;
}

namespace smt {

// "1.2 GB / 4.0 GB (30%)  85.0 MB/s  ETA 0:33"; shared by the GUI and the CLI.
inline QString formatProgressLine(qint64 done, qint64 total, double bytesPerSecond)
{
    const qint64 percent = total > 0 ? done * 100 / total : 100;
    QString eta = QStringLiteral("--");
    if (bytesPerSecond > 0) {
        const qint64 s = qint64(double(qMax<qint64>(total - done, 0)) / bytesPerSecond);
        eta = s >= 3600 ? QString("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'))
                        : QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
    }
    return QString("%1 / %2 (%3%)  %4/s  ETA %5")
        .arg(formatByteSize(size_t(done)), formatByteSize(size_t(total)))
        .arg(percent)
        .arg(formatByteSize(size_t(bytesPerSecond)), eta);
}

} // namespace smt

#endif // COREUTIL_H
