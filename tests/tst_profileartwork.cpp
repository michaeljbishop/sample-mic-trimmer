#include <QtTest>
#include <QImage>
#include "profileartwork.h"

class TstProfileArtwork : public QObject
{
    Q_OBJECT
    static bool png(const QString &path, int w, int h)
    {
        QImage img(w, h, QImage::Format_RGB32);
        img.fill(Qt::red);
        return img.save(path);
    }

private slots:
    void labelHidesWithoutImage()
    {
        QTemporaryDir tmp;
        QVERIFY(png(tmp.path() + "/a.png", 200, 200));

        QWidget parent;
        ProfileArtwork label(&parent);
        parent.show();
        label.setImageFile(tmp.path() + "/a.png");
        QVERIFY(label.isVisible());
        label.setImageFile(QString());
        QVERIFY(label.isHidden());
        label.setImageFile(tmp.path() + "/a.png");
        label.setImageFile(tmp.path() + "/missing.png");
        QVERIFY(label.isHidden());
    }

    void largeImageIsScaledDownKeepingAspect()
    {
        QTemporaryDir tmp;
        QVERIFY(png(tmp.path() + "/big.png", 1000, 800));
        ProfileArtwork label;
        label.setImageFile(tmp.path() + "/big.png");
        const QSizeF logical = label.pixmap().deviceIndependentSize();
        QVERIFY(logical.width() <= ProfileArtwork::kMaxSize + 0.5 && logical.height() <= ProfileArtwork::kMaxSize);
        QVERIFY(qAbs(logical.width() - ProfileArtwork::kMaxSize) < 1.0);
        QVERIFY(qAbs(logical.width() / logical.height() - 1000.0 / 800.0) < 0.02);
    }

    void smallImageIsNotEnlarged()
    {
        QTemporaryDir tmp;
        QVERIFY(png(tmp.path() + "/small.png", 100, 50));
        ProfileArtwork label;
        label.setImageFile(tmp.path() + "/small.png");
        QCOMPARE(label.pixmap().deviceIndependentSize().toSize(), QSize(100, 50));
    }
};

QTEST_MAIN(TstProfileArtwork)
#include "tst_profileartwork.moc"
