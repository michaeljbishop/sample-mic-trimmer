#include <QApplication>
#include <QDir>
#include <QSettings>
#include "corelib.h"
#include "mainwindow.h"
#include "darktheme.h"
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QCoreApplication::setApplicationName(smt::kApplicationName);
    QCoreApplication::setApplicationVersion("0.1");
    QCoreApplication::setOrganizationName(smt::kOrganizationName);

    // Isolated runs (tests): CFPreferences ignores $HOME, so keep the settings in that dir as INI.
    if (const QString dir = qEnvironmentVariable("SMT_APP_DATA_DIR"); !dir.isEmpty()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir(dir).absolutePath());
    }

    applyDarkTheme(app);

    MainWindow w;
    w.show();
    return app.exec();
}
