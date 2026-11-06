#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "coreutil.h"
#include "configerrornotifier.h"
#include "smt.h"
#include "workrelay.h"
#include <QUrl>
#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
class Ui_MainWindow;

QT_END_NAMESPACE
class QResizeEvent;
namespace smt {
class Core;
}
class MicMatrixView;
class ElidedLabel;
class ImageFetcher;
class QAction;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // What the Choose… button does with the folder the user picked (public so a test can drive it without the dialog).
    void chooseDestinationFolder(const QString &picked);

protected:
    void closeEvent(QCloseEvent *) override;

private:
    bool switchProfile(const QString &id, bool loadSource = true); // Core switch + remember as last profile
    void rebuildProfileUi();
    void updateProfileControlsEnabled();
    void updateApplyEnabled();
    void showApplyState(const PendingChange &pending, bool preparing);
    void updateArtwork();
    void applyArtwork(quint64 token, const QString &shown, const QString &urlDir, const QList<QUrl> &pending, const QString &credit);
    void updateEmptyState(); // hint instead of the matrix, legend and footer while there is no source content
    void setPathLabel(ElidedLabel *label, const QString &path);
    void updateFooter(); // "Installation size: X (Y space remaining)"
    Ui_MainWindow *ui = nullptr;
    smt::Core *mCore = nullptr;
    MicMatrixView *matrixView = nullptr;
    QLabel *mEmptyHint = nullptr;
    QString mHintChoose, mHintScanning;
    quint64 mPendingRequest = 0, mClickedPlan = 0; // the latest Core::requestPendingChange() / click plan tokens
    InstallationFooter mFooter; // free space after Apply; none until a destination is known
    ImageFetcher *mFetcher = nullptr;
    std::shared_ptr<WorkRelay> mRelay; // carries finished background jobs (the artwork lookup) back here
    quint64 mArtworkToken = 0;
    QAction *mDownloadImages = nullptr;
    ConfigErrorNotifier mConfigErrors;
    bool mSourceScanned = false, mApplying = false, mApplyUpdatePending = false; // gate the Rescan source button
};

#endif // MAINWINDOW_H
