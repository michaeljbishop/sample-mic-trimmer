#ifndef MICMATRIXMODEL_H
#define MICMATRIXMODEL_H

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QPair>
#include "profile.h"
#include "smt.h"

namespace smt {
class Core;
}

// The mic selection as one tree: top-level rows are the profile's tabs (groups of families), their
// children the mics. Column 0 is the mic name, 1..N the profile's sections (one checkbox + size each),
// the last one the row total. Only setData(CheckStateRole) and toggleColumn() change anything, and
// only through Core::setMicEnabled; the model follows Core's signals.
class MicMatrixModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Roles {
        ColumnTotalRole = Qt::UserRole + 1, // headerData: bytes selected in the column
        SizeTextRole // section cells: the formatted size alone, without "partly present"
    };

    explicit MicMatrixModel(smt::Core *core, QObject *parent = nullptr);

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Section column 1..N: a dash (some off) ticks the unlocked mics that are off; a ticked column (partly present counts) unticks them all.
    void toggleColumn(int column);
    Qt::CheckState columnCheckState(int column) const; // over the unlocked mics

    int sectionCount() const { return int(mSections.size()); }
    int totalColumn() const { return sectionCount() + 1; }

private:
    struct Group
    {
        QString name;
        QList<smt::MicDef> mics;
    };
    void rebuild();
    const smt::MicDef *micAt(const QModelIndex &index) const; // null for a group row
    smt::MicSignalGroupKey keyOf(const smt::MicDef &mic, int column) const;
    Qt::CheckState cellState(const smt::MicDef &mic, int column) const;
    Qt::CheckState rowState(const smt::MicDef &mic) const;
    size_t selectedSize(const smt::MicDef &mic) const;
    void setRow(const smt::MicDef &mic, bool enable);
    void refresh(const smt::MicSignalGroupKey &key);

    smt::Core *mCore;
    QList<smt::GroupDef> mSections;
    QList<Group> mGroups;
    QHash<QString, QPair<int, int>> mPos; // mic token -> {group row, mic row}
};

#endif // MICMATRIXMODEL_H
