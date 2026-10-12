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
/// Encoding: a 32-bit tag with encodingVerified false shows a warning icon (DecorationRole,
/// QIcon from the style's SP_MessageBoxWarning) on its Name cell, and the Name ToolTipRole reads
/// "Encoding not confirmed (float32 CDAB). Right-click > Detect encoding... to check it; writes
/// are blocked until it is confirmed." Changing ByteOrder by hand to a different value (setData)
/// sets encodingVerified to true (the user chose it); editing Type leaves it as it is.
/// A 16-bit tag with encodingVerified false (flagged as probably half of a 32-bit value) shows the
/// same icon; its Name ToolTipRole is its encoding note (setEncodingNote) or, without one, "This
/// register may be half of a 32-bit value. Right-click > Detect encoding... to check it; writes
/// ask first."
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

    /// Stable key of a column for project files: "name", "area", "address", "type",
    /// "byteOrder", "gain", "offset", "unit", "writable", "plot", "analyze", "axis", "color",
    /// "raw", "value", "writeMin", "writeMax", "quickA", "quickB", "quality". Empty for an
    /// invalid column.
    static QString columnKey(int column);
    /// Column of a key (exact, case-sensitive match), or -1.
    static int columnFromKey(const QString &key);
    /// Latest scaled value of the tag when its last sample was Good, else nullopt. Editing
    /// Area, Address, Type, ByteOrder, Gain or Offset forgets the tag's live value.
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

    /// Sets a 32-bit tag's type (Int32, UInt32 or Float32) and byte order and its
    /// encodingVerified flag. Returns false (nothing changes) for an unknown id, a 16-bit tag or
    /// a 16-bit type. When type or order change, forgets the live value (like an edit). Emits
    /// dataChanged for the row and tagsEdited() when anything changed; returns true.
    bool setEncoding(TagId id, ValueType type, mb::ByteOrder order, bool verified);
    /// Sets only encodingVerified (the live value is kept: decoding is unchanged). Emits
    /// dataChanged for the Name cell and tagsEdited() when it changed. False for an unknown id.
    bool setEncodingVerified(TagId id, bool verified);
    /// Tooltip note of a flagged tag (not saved; empty = the default text). Setting
    /// encodingVerified to true, or any edit of the tag's Area/Address/Type, clears it. Emits
    /// dataChanged for the Name cell only. False for an unknown id.
    bool setEncodingNote(TagId id, const QString &note);
    QString encodingNote(TagId id) const;
    /// Moves and retypes a tag in one step (the "Change to float32 CDAB at HR 110" fix): sets
    /// address, type, byte order and encodingVerified, clears the note and forgets the live value.
    /// False (nothing changes) for an unknown id or when the tag would not fit the address space.
    /// Emits dataChanged for the row and tagsEdited() when anything changed.
    bool setLayout(TagId id, quint16 address, ValueType type, mb::ByteOrder order, bool verified);

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
    QHash<TagId, QString> m_encodingNotes; ///< see setEncodingNote
    TagId m_nextId = 1;
    int m_addressBase = 0;
};

} // namespace mb
