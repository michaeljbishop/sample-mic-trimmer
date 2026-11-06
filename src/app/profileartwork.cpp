#include "profileartwork.h"
#include <QPixmap>

ProfileArtwork::ProfileArtwork(QWidget *parent)
    : QLabel(parent)
{
    setAlignment(Qt::AlignTop | Qt::AlignLeft);
    hide();
}

void ProfileArtwork::setImageFile(const QString &path)
{
    QPixmap pix(path);
    if (path.isEmpty() || pix.isNull()) {
        clear();
        hide();
        return;
    }
    const QSize box(kMaxSize, kMaxSize);
    if (pix.width() > kMaxSize || pix.height() > kMaxSize) { // only ever scaled down
        const qreal dpr = devicePixelRatioF();
        const QSize fit = pix.size().scaled(box, Qt::KeepAspectRatio);
        pix = pix.scaled((fit * dpr).boundedTo(pix.size()), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        pix.setDevicePixelRatio(pix.width() / qreal(fit.width()));
    }
    setPixmap(pix);
    show();
}
