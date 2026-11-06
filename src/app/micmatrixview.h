#ifndef MICMATRIXVIEW_H
#define MICMATRIXVIEW_H

#include <QHeaderView>
#include <QTreeView>

class MicMatrixModel;

// Section headers with a tri-state checkbox, the section name and, below it, the bytes selected in the
// column; a click forwards to MicMatrixModel::toggleColumn().
class MicMatrixHeader : public QHeaderView
{
    Q_OBJECT
public:
    MicMatrixHeader(MicMatrixModel *model, QWidget *parent = nullptr);
    bool hasCheckBox(int logicalIndex) const;

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
    QSize sectionSizeFromContents(int logicalIndex) const override;

private:
    MicMatrixModel *mModel;
};

// The tree configured as a table: group rows span all columns, sections have equal width.
class MicMatrixView : public QTreeView
{
    Q_OBJECT
public:
    explicit MicMatrixView(MicMatrixModel *model, QWidget *parent = nullptr);
    // Distance from the view's right edge to where the Row total column's numbers end (scrollbar, the
    // frame and padding): a footer with this right margin lines up under those numbers.
    int totalTextRightInset() const;

signals:
    void totalTextRightInsetChanged();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void layoutGroups();
};

#endif // MICMATRIXVIEW_H
