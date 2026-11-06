#include <QtTest>
#include "configerrornotifier.h"

class TstConfigErrorNotifier : public QObject
{
    Q_OBJECT
private slots:
    void dedupeSuppressesRepeat()
    {
        ConfigErrorNotifier n;
        QVERIFY(n.shouldShow("bad A"));
        QVERIFY(!n.shouldShow("bad A"));
        QVERIFY(n.shouldShow("bad B"));
    }
    void rearmAfterCleanLoad()
    {
        ConfigErrorNotifier n;
        QVERIFY(n.shouldShow("bad A"));
        n.rearm(); // Core::configurationLoaded
        QVERIFY(n.shouldShow("bad A"));
    }
    void rearmOnDestinationChange()
    {
        ConfigErrorNotifier n;
        QVERIFY(n.shouldShow("bad A"));
        n.rearm(); // Core::destinationDirectoryPathChanged
        QVERIFY(n.shouldShow("bad A"));
    }
    void sameFolderSkippedOnlyWhenNoError()
    {
        ConfigErrorNotifier n;
        QVERIFY(!n.shouldSetDestination("/a/b/", "/a/b"));
        QVERIFY(n.shouldSetDestination("/a/c", "/a/b"));
        QVERIFY(n.shouldShow("bad"));
        QVERIFY(n.shouldSetDestination("/a/b", "/a/b")); // re-choosing picks up a fixed file
        n.rearm();
        QVERIFY(!n.shouldSetDestination("/a/b", "/a/b"));
    }
};

QTEST_GUILESS_MAIN(TstConfigErrorNotifier)
#include "tst_configerrornotifier.moc"
