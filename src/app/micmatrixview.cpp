#include "micmatrixview.h"
#include <QEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyledItemDelegate>
#include "coreutil.h"
#include "micmatrixcolors.h"
#include "micmatrixmodel.h"

namespace {
constexpr int kTextPad = 8;         // text padding inside a cell
// A click anywhere in a cell toggles it (the stock delegate only reacts on the checkbox itself).
class CellDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setHeight(s.height() + 8); // air between rows
        if (index.column() == index.model()->columnCount() - 1)
            s.setWidth(s.width() + 2 * kTextPad); // the padding paint() leaves around the total
        return s;
    }
    // The cell state is the whole cell's fill, no checkbox: Qt's macOS style draws no item-view check
    // boxes (and puts QStyle's checkbox at the widget's corner), so nothing here goes through QStyle.
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        using namespace MatrixColors;
        const QVariant stateData = index.data(Qt::CheckStateRole);
        if (!stateData.isValid()) { // group rows and the row total, drawn here so they match the fixed dark panel
            const bool isGroup = !index.parent().isValid();
            QRect r = opt.rect;
            p->save();
            if (isGroup)
                p->fillRect(opt.rect, group);
            QFont f = opt.font;
            f.setBold(isGroup);
            p->setFont(f);
            p->setPen(isGroup ? groupText : totalText);
            p->drawText(r.adjusted(kTextPad, 0, -kTextPad, 0), (isGroup ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter,
                        QFontMetrics(f).elidedText(index.data().toString(), Qt::ElideRight, r.width() - 2 * kTextPad));
            p->restore();
            return;
        }
        const auto state = stateData.value<Qt::CheckState>();
        const bool locked_ = !index.flags().testFlag(Qt::ItemIsEnabled);
        QColor fill = off, text = index.column() > 0 ? offSize : offText;
        bool bold = false;
        if (locked_) {
            fill = locked;
            text = lockedText;
        } else if (index.column() == 0) { // the row: full when everything is wanted, amber if some of it is partly present
            bool partlyPresent = false;
            for (int c = 1; c < index.model()->columnCount() - 1; ++c)
                partlyPresent |= index.siblingAtColumn(c).data(Qt::CheckStateRole).value<Qt::CheckState>() == Qt::PartiallyChecked;
            text = offText;
            if (state == Qt::Checked) {
                fill = partlyPresent ? partly : on;
                text = partlyPresent ? partlyText : onText;
            } else if (state == Qt::PartiallyChecked) {
                fill = half;
                text = halfText;
            }
        } else if (state == Qt::Checked) {
            fill = on;
            text = onText;
            bold = true;
        } else if (state == Qt::PartiallyChecked) {
            fill = partly;
            text = partlyText;
        }
        const QRect r = opt.rect.adjusted(1, 1, -1, -1);
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(Qt::NoPen);
        p->setBrush(fill);
        p->drawRoundedRect(r, 3, 3);
        QFont font = opt.font;
        font.setWeight(bold ? QFont::DemiBold : QFont::Normal);
        p->setPen(text);
        p->setFont(font);
        const QFontMetrics fm(font);
        const QRect tr = r.adjusted(kTextPad, 0, -kTextPad, 0);
        // Too narrow for "<size> partly present": "<size> partly", then the size alone; the size never goes first.
        QString label = index.data().toString();
        const QString size = index.data(MicMatrixModel::SizeTextRole).toString();
        if (state == Qt::PartiallyChecked && index.column() > 0 && fm.horizontalAdvance(label) > tr.width())
            label = size + QLatin1Char(' ') + QObject::tr("partly");
        if (index.column() > 0 && fm.horizontalAdvance(label) > tr.width())
            label = size;
        p->drawText(tr, Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(label, Qt::ElideRight, tr.width()));
        if (opt.state & (QStyle::State_Selected | QStyle::State_HasFocus)) { // the current cell: 2px, white outside, dark inside
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(ringOuter, 1));
            p->drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
            p->setPen(QPen(ringInner, 1));
            p->drawRoundedRect(QRectF(r).adjusted(1.5, 1.5, -1.5, -1.5), 2, 2);
        }
        p->restore();
    }
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override
    {
        const bool checkable = index.flags().testFlag(Qt::ItemIsUserCheckable) && index.flags().testFlag(Qt::ItemIsEnabled);
        if (checkable && event->type() == QEvent::MouseButtonRelease
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            // The model turns Checked for a partly checked cell into an untick.
            const auto now = index.data(Qt::CheckStateRole).value<Qt::CheckState>();
            return model->setData(index, now == Qt::Checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
        }
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }
};
} // namespace

MicMatrixHeader::MicMatrixHeader(MicMatrixModel *model, QWidget *parent)
    : QHeaderView(Qt::Horizontal, parent)
    , mModel(model)
{
    setHighlightSections(false);
    connect(this, &QHeaderView::sectionClicked, this, [this](int i) {
        if (hasCheckBox(i))
            mModel->toggleColumn(i);
    });
}

bool MicMatrixHeader::hasCheckBox(int i) const
{
    return i >= 1 && i <= mModel->sectionCount();
}

QSize MicMatrixHeader::sectionSizeFromContents(int i) const
{
    QSize s = QHeaderView::sectionSizeFromContents(i);
    s.setHeight(fontMetrics().height() * 2 + 10); // name and selected total
    if (i == mModel->totalColumn()) { // the heading is inset like the numbers and must still fit
        QFont bold = font();
        bold.setBold(true);
        s.setWidth(qMax(s.width(),
                        QFontMetrics(bold).horizontalAdvance(mModel->headerData(i, Qt::Horizontal).toString())
                            + 2 * kTextPad));
    }
    return s;
}

void MicMatrixHeader::paintSection(QPainter *painter, const QRect &rect, int i) const
{
    using namespace MatrixColors;
    // Two-tone rule like the cells: all wanted = light, some = half, none (and name/total headings) = dark.
    const bool tinted = hasCheckBox(i);
    const auto state = tinted ? mModel->headerData(i, Qt::Horizontal, Qt::CheckStateRole).value<Qt::CheckState>()
                              : Qt::Unchecked;
    const QColor fill = state == Qt::Checked ? on : state == Qt::PartiallyChecked ? half : off;
    const QColor text = state == Qt::Checked ? onText : state == Qt::PartiallyChecked ? halfText : offText;
    painter->save();
    painter->fillRect(rect, base);
    painter->fillRect(rect.adjusted(1, 1, -1, -1), fill);
    const int lineH = fontMetrics().height();
    const int textLeft = rect.left() + kTextPad, textW = rect.width() - 12;
    QFont bold = font();
    bold.setBold(true);
    painter->setFont(bold);
    painter->setPen(text);
    const QString name = mModel->headerData(i, Qt::Horizontal).toString();
    if (!tinted) {
        // The total heading ends where the numbers below it end.
        const int w = i == 0 ? textW : rect.width() - 2 * kTextPad;
        painter->drawText(QRect(textLeft, rect.top(), w, rect.height()),
                          (i == 0 ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter,
                          fontMetrics().elidedText(name, Qt::ElideRight, w));
        painter->restore();
        return;
    }
    painter->drawText(QRect(textLeft, rect.top() + 5, textW, lineH), Qt::AlignLeft | Qt::AlignVCenter,
                      fontMetrics().elidedText(name, Qt::ElideRight, textW));
    painter->setFont(font());
    painter->drawText(QRect(textLeft, rect.top() + 5 + lineH, textW, lineH), Qt::AlignLeft | Qt::AlignVCenter,
                      formatByteSize(mModel->headerData(i, Qt::Horizontal, MicMatrixModel::ColumnTotalRole).toULongLong()));
    painter->restore();
}

MicMatrixView::MicMatrixView(MicMatrixModel *model, QWidget *parent)
    : QTreeView(parent)
{
    setHeader(new MicMatrixHeader(model, this));
    QPalette pal = palette(); // fixed dark panel: no white gaps whatever the system appearance
    pal.setColor(QPalette::Base, MatrixColors::base);
    setPalette(pal);
    setModel(model);
    header()->setSectionsClickable(true); // QTreeView resets it when the model is set
    setItemDelegate(new CellDelegate(this));
    setRootIsDecorated(false);
    setItemsExpandable(false); // groups stay open
    setSelectionBehavior(SelectItems); // the keyboard moves cell to cell
    setAlternatingRowColors(false);
    setUniformRowHeights(true);
    setSelectionMode(SingleSelection); // NoSelection stops the keyboard from moving the current cell on macOS
    setIndentation(0);
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int c = 1; c <= model->sectionCount(); ++c)
        header()->setSectionResizeMode(c, QHeaderView::Stretch);
    header()->setSectionResizeMode(model->totalColumn(), QHeaderView::ResizeToContents);
    connect(model, &QAbstractItemModel::modelReset, this, [this, model] {
        // A profile switch changes the section count.
        for (int c = 1; c < model->columnCount(); ++c)
            header()->setSectionResizeMode(c, c == model->totalColumn() ? QHeaderView::ResizeToContents : QHeaderView::Stretch);
        layoutGroups();
    });
    layoutGroups();
}

int MicMatrixView::totalTextRightInset() const
{
    return width() - (viewport()->geometry().right() + 1) + kTextPad;
}

void MicMatrixView::resizeEvent(QResizeEvent *event)
{
    QTreeView::resizeEvent(event);
    emit totalTextRightInsetChanged();
}

void MicMatrixView::layoutGroups()
{
    expandAll();
    for (int r = 0; r < model()->rowCount(); ++r)
        setFirstColumnSpanned(r, QModelIndex(), true);
}
