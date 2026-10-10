#pragma once

#include "SampleBatch.h"
#include "Tag.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QList>

#include <optional>

namespace mb {

/// Table model over the project's tags plus their live values.
///
/// Columns and roles:
/// - Name, Unit: DisplayRole/EditRole QString.
/// - Area: DisplayRole/EditRole "Holding" | "Input" (see toString(Area)).
/// - Address: int 0..65535. A 32-bit tag must end at or below 65535 (address <= 65534).
/// - Type: DisplayRole/EditRole "int16" | "uint16" | "int32" | "uint32" | "float32".
///   Changing the type is rejected if the tag would not fit the address space.
/// - ByteOrder: DisplayRole/EditRole "ABCD" | "CDAB" | "BADC" | "DCBA". For 16-bit tags the
///   display is an em dash, the cell is not editable and ToolTipRole explains why.
/// - Gain (!= 0), Offset: double.
/// - Writable, Plot, Analyze: CheckStateRole only (Writable is disabled for input registers).
///   Analyze toggles Tag::analyzed (any tag, any area); like Plot it emits tagsEdited().
///   ToolTipRole on its header: "Show this tag in the Analysis tab".
/// - Axis: DisplayRole/EditRole "Left" | "Right" (toString(YAxis), case-insensitive on
///   input); any tag; emits tagsEdited().
/// - QuickA, QuickB: Tag::quickA/quickB. DisplayRole: the value (6 significant digits) or
///   empty; EditRole: the value as double or an empty string. setData with an empty/blank
///   string clears it, a finite number (or numeric string) sets it, anything else returns
///   false; emits tagsEdited(). Editable only when the tag isWritable(); for other tags the
///   cell is not editable and its ToolTipRole says "Quick writes need a writable holding
///   register". Header tooltip: "Preset value for the Quick write dock".
///   A value outside the tag's write limits is refused (setData returns false).
/// - WriteMin, WriteMax (headers "Min", "Max"): Tag::writeMin/writeMax, same roles, parsing,
///   clearing and editability as QuickA/QuickB. setData refuses a Min above the Max (or a Max
///   below the Min). Header tooltips: "Lowest value that may be written (empty = no limit)" /
///   "Highest value that may be written (empty = no limit)". Changing a limit does not change
///   Quick A/B values (writes outside the limits are refused when sent).
/// - Color: DisplayRole "#RRGGBB", DecorationRole QColor, EditRole "#RRGGBB".
/// - Raw: read-only, "0x%04X (%u)" for 16-bit tags, "0x%08X" for 32-bit tags, or empty.
///   Quality: read-only text, ForegroundRole colour (Good green, Unknown grey, others red);
///   ToolTipRole holds the last error text.
/// - Value: scaled value as text (6 significant digits, 7 for float32) or empty. Editable
///   only when the tag isWritable(): setData() on it does NOT change the model; it emits
///   writeRequested(id, value) and returns true.
/// Invalid edits (bad address, gain 0, unparsable numbers) make setData() return false.
class TagTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        Name, Area, Address, Type, ByteOrder, Gain, Offset, Unit, Writable, Plot, Analyze, Axis,
        Color, Raw, Value, WriteMin, WriteMax, QuickA, QuickB, Quality, ColumnCount
    };

    explicit TagTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool removeRows(int row, int count, const QModelIndex &parent = {}) override;

    /// Replaces all tags (model reset) and clears live values. nextTagId is raised to at
    /// least max(id) + 1.
    void setTags(const QList<Tag> &tags, TagId nextTagId);
    QList<Tag> tags() const;
    TagId nextTagId() const;
    const Tag &tagAt(int row) const;
    int rowOf(TagId id) const; ///< -1 if absent
    /// Latest scaled value of the tag when its last sample was Good, else nullopt.
    std::optional<double> goodValue(TagId id) const;

    /// Appends a copy of `prototype` with a fresh id. An empty name becomes "Tag <id>".
    /// Returns the new row.
    int addTag(const Tag &prototype = Tag());
    /// Appends a copy of row `row` with a fresh id, name suffixed " (copy)". Returns the new row.
    int duplicateTag(int row);

    /// Updates Raw/Value/Quality of the tags in the batch; emits dataChanged only for
    /// those cells (never a reset, so selection and editors survive).
    void updateSamples(const SampleBatch &batch);
    /// Clears Raw/Value and sets Quality to Unknown for every tag.
    void clearLiveValues();
    /// Sets the Quality tooltip text of one tag (e.g. last write/read error).
    void setStatusText(TagId id, const QString &text);

    /// 0 (default) or 1. The Address column shows and accepts tag.address + base (edit range
    /// base..65535+base, a 32-bit tag at most 65534+base); its header reads "Address" for 0
    /// and "Register" for 1, with a tooltip explaining the numbering. Changing the base emits
    /// headerDataChanged + dataChanged for the column, not tagsEdited (tags are unchanged).
    void setAddressBase(int base);
    int addressBase() const;

signals:
    /// Any configuration change: add, remove, duplicate, setData on a config column.
    /// Not emitted for live values or setTags().
    void tagsEdited();
    /// The user entered a new value for a writable tag.
    void writeRequested(mb::TagId id, double scaledValue);

private:
    /// dataChanged for the live columns (Raw..Value, Quality) of rows first..last.
    void emitLiveChanged(int firstRow, int lastRow, const QList<int> &roles);
    // Implementation-defined; the task may change the private section freely.
    struct Live {
        bool valid = false;
        quint32 raw = 0;
        double value = 0.0;
        mb::Quality quality = mb::Quality::Unknown;
        QString status;
    };
    QList<Tag> m_tags;
    QHash<TagId, Live> m_live;
    TagId m_nextId = 1;
    int m_addressBase = 0;
};

} // namespace mb
