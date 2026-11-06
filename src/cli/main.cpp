// smt: a thin command-line front end over smt::Core. All decisions (scanning, analysis,
// planning, safety checks, applying) are made in corelib; this file only parses arguments,
// prints what corelib reports and maps the outcome to an exit code.
#include <QtGlobal> // before the first Q_OS_* test below
#include <csignal>
#include <cstdio>
#ifndef Q_OS_WIN
#include <unistd.h>
#endif
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSocketNotifier>
#include <QTimer>
#include "analysis.h"
#include "corelib.h"
#include "coreutil.h"
#include "pathsafety.h"
#include "spitfiresettings.h"

using namespace smt;

namespace {

#ifndef Q_OS_WIN
int gSigintPipe[2] = {-1, -1};

void onSigint(int)
{
    const char c = 1;
    (void)!::write(gSigintPipe[1], &c, 1); // async-signal-safe; the event loop does the rest
}
#else
// Windows (UNVERIFIED): no pipe(2) or sigaction; the handler only sets a flag that a timer reads.
volatile std::sig_atomic_t gSigintSeen = 0;

void onSigint(int)
{
    gSigintSeen = 1;
}
#endif

// Flushed per line: under a pipe stdout is fully buffered and progress would arrive late.
void print(FILE *f, const QString &text)
{
    std::fputs(text.toLocal8Bit().constData(), f);
    std::fflush(f);
}

QString statusName(PlanStep::Status s)
{
    switch (s) {
    case PlanStep::Status::Pending: return QStringLiteral("Pending");
    case PlanStep::Status::Running: return QStringLiteral("Running");
    case PlanStep::Status::Done: return QStringLiteral("Done");
    case PlanStep::Status::Failed: return QStringLiteral("Failed");
    }
    return {};
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    // Same name/organization as the GUI, so both share the source scan cache.
    QCoreApplication::setApplicationName(kApplicationName);
    QCoreApplication::setOrganizationName(kOrganizationName);
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Makes a reduced copy of a sample library (see --profile) with only the mic positions selected in "
        "<project>/project.smt (all mics are kept if that file does not exist).\n\n"
        "  analyze  -s <source>               list the mic positions in the library\n"
        "  plan     -s <source> -p <project>  print what apply would do; changes nothing\n"
        "  apply    -s <source> -p <project>  print the plan; with -y, run it\n"
        "  use source|trimmed -s <source> -p <project>\n"
        "           point the Spitfire app and plugins at the source or at the trimmed copy\n"
        "           (rewrites only the profile's entries in Spitfire.properties, keeping a .smt-backup)");
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption source({"s", "source"}, "The installed library (only read).", "source");
    const QCommandLineOption project({"p", "project"}, "The trimmed copy to create or update.", "project");
    const QCommandLineOption yes({"y", "yes"}, "apply: run the plan instead of only printing it.");
    const QCommandLineOption rescan("rescan", "Rescan the source instead of using the cached scan.");
    const QCommandLineOption properties("properties", "use: the Spitfire.properties to rewrite (default: the user's).",
                                        "file", defaultSpitfireProperties());
    const QCommandLineOption profileOption("profile",
        "analyze, plan, apply: the product profile, an id (or bbcso / ewc) or a path to a .smtprofile folder "
        "(default: chosen from the source folder name, else BBCSO).", "id|path");
    parser.addOptions({source, project, yes, rescan, properties, profileOption});
    parser.addPositionalArgument("command", "analyze, plan, apply or use.");

    const auto usage = [&](const QString &why) {
        print(stderr, why + "\n\n" + parser.helpText());
        return 2;
    };
    if (!parser.parse(QCoreApplication::arguments()))
        return usage(parser.errorText());
    if (parser.isSet("help"))
        parser.showHelp(0);
    if (parser.isSet("version"))
        parser.showVersion();

    const QStringList args = parser.positionalArguments();
    const QString command = args.value(0);
    if (command != "use" && (args.size() != 1 || !QStringList{"analyze", "plan", "apply"}.contains(command)))
        return usage(args.isEmpty() ? QStringLiteral("No command given.") : "Unknown command: " + args.join(' '));
    // An empty path would start no scan, so the CLI would wait forever.
    if (parser.value(source).trimmed().isEmpty() || (command != "analyze" && parser.value(project).trimmed().isEmpty()))
        return usage(command == "analyze" ? QStringLiteral("analyze needs a non-empty -s.")
                                          : command + " needs non-empty -s and -p.");
    // Trailing '/' dropped so "lib/" and "lib" share the scan cache. Not cleanPath (nor
    // QFileInfo::absoluteFilePath, which cleans): collapsing ".." would hide it from Core,
    // which refuses it because a symlink before ".." can point anywhere.
    const auto cleanAbsolute = [&](const QCommandLineOption &o) {
        QString path = parser.value(o);
        if (QDir::isRelativePath(path))
            path = QDir::currentPath() + '/' + path;
        while (path.size() > 1 && path.endsWith('/'))
            path.chop(1);
        return path;
    };

    // Outlive the connections below, which run inside a.exec().
    Core core;
    Plan plan;
    bool cancelling = false;
    QElapsedTimer sinceProgress;
    QObject::connect(&core, &Core::configurationError, &a, [](const QString &message) { print(stderr, message + "\n"); });

    // Before setSourceDir, which would otherwise suggest a profile from the folder name.
    if (parser.isSet(profileOption)) {
        const QString value = QDir::cleanPath(parser.value(profileOption));
        if (QFileInfo(value).isDir() && value.endsWith(".smtprofile")) {
            QString error;
            const auto loaded = loadProfile(QDir(value).absolutePath(), &error);
            if (!loaded)
                return usage(error);
            core.setProfile(*loaded);
        } else {
            const QString id = value == "bbcso" ? kBbcsoProfileId : value == "ewc" ? kEwcProfileId : value;
            if (!core.setProfileById(id)) {
                QStringList ids;
                for (const ProfileEntry &e : core.availableProfiles())
                    if (e.valid())
                        ids << e.id;
                return usage("Unknown profile: " + value + "\nValid profiles: " + ids.join(", ") + " (or bbcso, ewc)");
            }
        }
    }
    const auto profileLine = [&] { return "Profile: " + core.profile().name + " (" + core.profile().id + ")\n"; };

    if (command == "use") {
        if (args.size() != 2 || !QStringList{"source", "trimmed"}.contains(args[1]))
            return usage("use needs source or trimmed.");
        if (parser.value(source).trimmed().isEmpty() || parser.value(project).trimmed().isEmpty())
            return usage("use needs non-empty -s and -p.");
        const QString src = QDir::cleanPath(QDir(parser.value(source)).absolutePath());
        const QString dst = QDir::cleanPath(QDir(parser.value(project)).absolutePath());
        const bool toTrimmed = args[1] == "trimmed";
        // Without --profile the source folder name picks it (no scan runs for "use"), else BBCSO.
        if (!parser.isSet(profileOption))
            if (const ProfileEntry *e = suggestProfile(core.availableProfiles(), QFileInfo(src).fileName()))
                core.setProfileById(e->id);
        QString error = toTrimmed ? checkDestinationName(src, dst) : QString();
        RepointResult r;
        if (error.isEmpty())
            r = repointLibrary(parser.value(properties), core.profile(), toTrimmed ? dst : src);
        else
            r.error = error;
        if (!r.ok()) {
            print(stderr, r.error + "\n");
            return 1;
        }
        print(stdout, profileLine() + "Spitfire " + (r.written ? "now uses" : "already used") + " the " + args[1] + " library ("
                          + (toTrimmed ? dst : src) + ") for: " + r.products.join(", ")
                          + (r.written ? "\nReload the plugin in your DAW." : "\nAlready up to date; nothing was written.")
                          + (r.backup.isEmpty() ? QString() : "\nOriginal settings (first backup) kept in: " + r.backup)
                          + "\n");
        return 0;
    }

    if (command == "analyze") {
        QObject::connect(&core, &Core::sourceScanFinished, &a, [&] {
            print(stdout, profileLine());
            const Analysis analysis = analyzeLibrary(core.profile(), core.sourceDir(), core.sourceFileInfos());
            print(stdout, analysis.toText());
            if (analysis.dbsFound.isEmpty())
                print(stderr, "No Spitfire databases (.db) found in " + core.sourceDir() + "\n");
            QCoreApplication::exit(analysis.dbsFound.isEmpty() ? 1 : 0);
        }, Qt::SingleShotConnection);
    } else {
        QObject::connect(&core, &Core::applyStepChanged, &a, [&](int i, PlanStep::Status s, const QString &error) {
            print(stdout, QString("[%1/%2] %3: %4%5\n")
                              .arg(i + 1)
                              .arg(plan.steps.size())
                              .arg(statusName(s), plan.steps.value(i).describe(), error.isEmpty() ? "" : " - " + error));
        });
        QObject::connect(&core, &Core::applyProgress, &a, [&](qint64 done, qint64 total, double bytesPerSecond) {
            if (sinceProgress.isValid() && sinceProgress.elapsed() < 1000 && done < total)
                return; // at most one progress line a second
            sinceProgress.start();
            print(stdout, formatProgressLine(done, total, bytesPerSecond) + "\n");
        });
        QObject::connect(&core, &Core::applyFinished, &a, [](bool success, const QString &message) {
            print(success ? stdout : stderr, (success ? "Apply finished. " : "Apply failed. ") + message + "\n");
            QCoreApplication::exit(success ? 0 : 1);
        });

        // Single shot: Core rescans the destination after an apply and emits scanFinished again.
        QObject::connect(&core, &Core::scanFinished, &a, [&] {
            plan = core.buildPlan();
            print(stdout, profileLine() + "Source: " + core.sourceDir() + "\nDestination: " + core.destinationDirectoryPath()
                              + "\nMic selection: " + core.configurationFilePath() + "\n" + plan.toText());
            if (plan.canRun() && !plan.isEmpty() && !plan.hasFileChanges())
                print(stdout, "No file changes; databases will be refreshed only.\n");
            if (!plan.canRun())
                return QCoreApplication::exit(1);
            if (command == "plan")
                return QCoreApplication::exit(0);
            if (!parser.isSet(yes)) {
                print(stdout, "Dry run: nothing was changed. Re-run with -y to apply.\n");
                return QCoreApplication::exit(0);
            }
            print(stdout, "Quit the Spitfire plugin/host before applying.\n");

            // Ctrl-C cancels between files, which keeps the library consistent.
            const auto cancelNow = [&] {
                if (cancelling)
                    return;
                cancelling = true;
                print(stderr, "Cancelling after the current file...\n");
                core.cancelApply();
            };
#ifndef Q_OS_WIN
            if (::pipe(gSigintPipe) == 0) {
                auto *notifier = new QSocketNotifier(gSigintPipe[0], QSocketNotifier::Read, &a);
                QObject::connect(notifier, &QSocketNotifier::activated, &a, [&, cancelNow] {
                    char c;
                    (void)!::read(gSigintPipe[0], &c, 1);
                    cancelNow();
                });
                struct sigaction sa = {};
                sa.sa_handler = onSigint;
                sa.sa_flags = SA_RESTART;
                sigemptyset(&sa.sa_mask);
                sigaction(SIGINT, &sa, nullptr);
            }
#else
            std::signal(SIGINT, onSigint);
            auto *poll = new QTimer(&a);
            QObject::connect(poll, &QTimer::timeout, &a, [cancelNow] {
                if (gSigintSeen)
                    cancelNow();
            });
            poll->start(100);
#endif
            core.startApply(plan); // a refusal still ends in applyFinished(false, why)
        }, Qt::SingleShotConnection);

        core.setDestinationDirectoryPath(cleanAbsolute(project));
    }

    core.setSourceDir(cleanAbsolute(source));
    if (parser.isSet(rescan))
        core.rescanSource();
    return a.exec();
}
