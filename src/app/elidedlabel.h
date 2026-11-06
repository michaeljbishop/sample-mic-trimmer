#ifndef ELIDEDLABEL_H
#define ELIDEDLABEL_H

#include <QLabel>

class ElidedLabel : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText)
    Q_PROPERTY(bool isElided READ isElided)
    Q_PROPERTY(
        Qt::TextElideMode elideMode READ elideMode WRITE setElideMode NOTIFY elideModeChanged FINAL)
public:
    explicit ElidedLabel(QWidget *parent = nullptr);
    explicit ElidedLabel(const QString &text, QWidget *parent = nullptr);

    void setText(const QString &text);
    const QString &text() const { return content; }
    bool isElided() const { return elided; }

    Qt::TextElideMode elideMode() const;
    void setElideMode(Qt::TextElideMode newElideMode);

protected:
    void paintEvent(QPaintEvent *event) override;

signals:
    void elisionChanged(bool elided);

    void elideModeChanged(Qt::TextElideMode elideMode);

private:
    bool elided;
    QString content;
    Qt::TextElideMode mElideMode;
};
#endif // ELIDEDLABEL_H
