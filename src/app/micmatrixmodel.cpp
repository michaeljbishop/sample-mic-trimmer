#include "micmatrixmodel.h"
#include <QFont>
#include "corelib.h"
#include "coreutil.h"

using namespace smt;

MicMatrixModel::MicMatrixModel(Core *core, QObject *parent)
    : QAbstractItemModel(parent)
    , mCore(core)
{
    rebuild();
    connect(core, &Core::profileChanged, this, [this] {
        beginResetModel();
        rebuild();
        endResetModel();
    });
    connect(core, &Core::micEnabledChanged, this, [this](const MicSignalGroupKey &k, bool) { refresh(k); });
    connect(core, &Core::micCompletenessChanged, this, [this](const MicSignalGroupKey &k, Completeness) { refresh(k); });
    connect(core, &Core::spaceRequiredChanged, this, [this](const MicSignalGroupKey &k, size_t) { refresh(k); });
}

// Tabs in order of first appearance among the families (an empty tab = the family's name); mics in family order.
void MicMatrixModel::rebuild()
{
    const ProductProfile &profile = mCore->profile();
    mSections = profile.groups;
    mGroups.clear();
    mPos.clear();
    QHash<QString, int> byTab;
    for (const FamilyDef &family : profile.families) {
        const QString tab = family.tab.isEmpty() ? family.name : family.tab;
        if (!byTab.contains(tab)) {
            byTab.insert(tab, int(mGroups.size()));
            mGroups.append({tab, {}});
        }
        Group &g = mGroups[byTab.value(tab)];
        for (const MicDef &mic : profile.mics)
            if (mic.family == family.key) {
                mPos.insert(mic.token, {byTab.value(tab), int(g.mics.size())});
                g.mics.append(mic);
            }
    }
}

QModelIndex MicMatrixModel::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || column < 0 || column >= columnCount())
        return {};
    if (!parent.isValid())
        return row < mGroups.size() ? createIndex(row, column, quintptr(0)) : QModelIndex();
    if (parent.internalId() != 0 || parent.column() != 0 || parent.row() >= mGroups.size())
        return {};
    return row < mGroups.at(parent.row()).mics.size() ? createIndex(row, column, quintptr(parent.row() + 1)) : QModelIndex();
}

QModelIndex MicMatrixModel::parent(const QModelIndex &child) const
{
    if (!child.isValid() || child.internalId() == 0)
        return {};
    return createIndex(int(child.internalId()) - 1, 0, quintptr(0));
}

int MicMatrixModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return int(mGroups.size());
    if (parent.internalId() != 0 || parent.column() != 0)
        return 0;
    return int(mGroups.at(parent.row()).mics.size());
}

int MicMatrixModel::columnCount(const QModelIndex &) const
{
    return sectionCount() + 2;
}

const MicDef *MicMatrixModel::micAt(const QModelIndex &index) const
{
    if (!index.isValid() || index.internalId() == 0)
        return nullptr;
    return &mGroups.at(int(index.internalId()) - 1).mics.at(index.row());
}

MicSignalGroupKey MicMatrixModel::keyOf(const MicDef &mic, int column) const
{
    return {mic.token, mSections.at(column - 1).key};
}

Qt::CheckState MicMatrixModel::cellState(const MicDef &mic, int column) const
{
    if (mic.locked)
        return Qt::Checked;
    const MicSignalGroupKey key = keyOf(mic, column);
    if (!mCore->isMicEnabled(key))
        return Qt::Unchecked;
    return mCore->micCompleteness(key) == Completeness::Partial ? Qt::PartiallyChecked : Qt::Checked;
}

Qt::CheckState MicMatrixModel::rowState(const MicDef &mic) const
{
    int wanted = 0; // partly present counts as wanted: Apply completes it
    for (int c = 1; c <= sectionCount(); ++c)
        wanted += cellState(mic, c) != Qt::Unchecked;
    return wanted == 0 ? Qt::Unchecked : wanted == sectionCount() ? Qt::Checked : Qt::PartiallyChecked;
}

size_t MicMatrixModel::selectedSize(const MicDef &mic) const
{
    size_t total = 0;
    for (int c = 1; c <= sectionCount(); ++c)
        if (cellState(mic, c) != Qt::Unchecked)
            total += mCore->spaceRequired(keyOf(mic, c));
    return total;
}

QVariant MicMatrixModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.column() >= columnCount())
        return {};
    const MicDef *mic = micAt(index);
    const int col = index.column();
    if (!mic) { // group row: a bold label spanning the row
        const Group &g = mGroups.at(index.row());
        if (role == Qt::DisplayRole && col == 0)
            return g.name;
        if (role == Qt::FontRole) {
            QFont f;
            f.setBold(true);
            return f;
        }
        return {};
    }
    if (col == 0) {
        switch (role) {
        case Qt::DisplayRole: return mic->locked ? tr("%1 (always on)").arg(mic->name) : mic->name;
        case Qt::ToolTipRole:
            return mic->locked ? tr("Always on") : tr("Click to include/exclude all sections");
        case Qt::CheckStateRole: return rowState(*mic);
        }
        return {};
    }
    if (col == totalColumn()) {
        if (role == Qt::DisplayRole)
            return formatByteSize(selectedSize(*mic));
        if (role == Qt::TextAlignmentRole)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        return {};
    }
    const Qt::CheckState state = cellState(*mic, col);
    switch (role) {
    case Qt::DisplayRole: {
        QString text = formatByteSize(mCore->spaceRequired(keyOf(*mic, col)));
        return state == Qt::PartiallyChecked ? tr("%1 partly present").arg(text) : text;
    }
    case Qt::CheckStateRole: return state;
    case SizeTextRole: return formatByteSize(mCore->spaceRequired(keyOf(*mic, col)));
    case Qt::ToolTipRole:
        if (mic->locked)
            return tr("Always on");
        if (state == Qt::PartiallyChecked)
            return tr("Only partly present in the Reduced Mic Installation; Apply will complete it. Untick to remove it.");
        return state == Qt::Checked ? tr("Click to exclude") : tr("Click to include");
    }
    return {};
}

Qt::ItemFlags MicMatrixModel::flags(const QModelIndex &index) const
{
    const MicDef *mic = micAt(index);
    if (!index.isValid() || !mic || index.column() == totalColumn())
        return index.isValid() ? Qt::ItemIsEnabled : Qt::NoItemFlags; // group and total cells: no checkbox
    // Not user-tristate: a click on a partly checked cell unticks it (see setData).
    return mic->locked ? Qt::ItemFlags() : Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
}

void MicMatrixModel::setRow(const MicDef &mic, bool enable)
{
    for (int c = 1; c <= sectionCount(); ++c)
        mCore->setMicEnabled(keyOf(mic, c), enable); // a locked mic is ignored by Core
}

bool MicMatrixModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    const MicDef *mic = micAt(index);
    if (role != Qt::CheckStateRole || !mic || mic->locked || index.column() == totalColumn())
        return false;
    const Qt::CheckState incoming = value.value<Qt::CheckState>();
    if (index.column() == 0) {
        // A dash ticks the missing sections; a ticked row (partly present counts as ticked) unticks them all.
        setRow(*mic, incoming == Qt::Checked && rowState(*mic) != Qt::Checked);
        return true;
    }
    // The view proposes Checked for a partly checked cell; that must untick it.
    const bool enable = incoming == Qt::Checked && cellState(*mic, index.column()) == Qt::Unchecked;
    mCore->setMicEnabled(keyOf(*mic, index.column()), enable);
    return true;
}

void MicMatrixModel::toggleColumn(int column)
{
    if (column < 1 || column > sectionCount())
        return;
    // A dash ticks the missing mics (partly present ones stay as they are); a ticked column unticks them all.
    const bool enable = columnCheckState(column) != Qt::Checked;
    for (const Group &g : std::as_const(mGroups))
        for (const MicDef &mic : g.mics)
            mCore->setMicEnabled(keyOf(mic, column), enable); // a locked mic is ignored by Core
}

Qt::CheckState MicMatrixModel::columnCheckState(int column) const
{
    bool any = false, all = true;
    for (const Group &g : mGroups)
        for (const MicDef &mic : g.mics) {
            if (mic.locked)
                continue;
            const Qt::CheckState s = cellState(mic, column);
            any |= s != Qt::Unchecked;
            all &= s != Qt::Unchecked;
        }
    return all ? Qt::Checked : any ? Qt::PartiallyChecked : Qt::Unchecked;
}

QVariant MicMatrixModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || section < 0 || section >= columnCount())
        return {};
    if (role == Qt::DisplayRole) {
        if (section == 0)
            return tr("Microphone");
        return section == totalColumn() ? tr("Row total") : mSections.at(section - 1).name;
    }
    if (section == 0 || section == totalColumn())
        return {};
    if (role == Qt::ToolTipRole)
        return tr("Click to toggle the whole column");
    if (role == Qt::CheckStateRole)
        return columnCheckState(section);
    if (role == ColumnTotalRole) {
        size_t total = 0;
        for (const Group &g : mGroups)
            for (const MicDef &mic : g.mics)
                if (cellState(mic, section) != Qt::Unchecked)
                    total += mCore->spaceRequired(keyOf(mic, section));
        return qulonglong(total);
    }
    return {};
}

void MicMatrixModel::refresh(const MicSignalGroupKey &key)
{
    const auto pos = mPos.constFind(key.mic);
    int col = -1;
    for (int i = 0; i < mSections.size(); ++i)
        if (mSections.at(i).key == key.group)
            col = i + 1;
    if (pos == mPos.cend() || col < 0)
        return;
    const QModelIndex group = this->index(pos->first, 0);
    emit dataChanged(this->index(pos->second, 0, group), this->index(pos->second, totalColumn(), group));
    emit headerDataChanged(Qt::Horizontal, col, col);
}
