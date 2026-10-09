#pragma once

#include "SampleBatch.h"
#include "Tag.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QList>

namespace mb {

/// Table model over the project's tags plus their live values.
///
/// Columns and roles:
/// - Name, Unit: DisplayRole/EditRole QString.
/// - Area: DisplayRole/EditRole "Holding" | "Input" (see toString(Area)).
/// - Address: int 0..65535.  Type: "int16" | "uint16".  Gain (!= 0), Offset: double.
/// - Writable, Plot: CheckStateRole only (Writable is disabled for input registers).
/// - Color: DisplayRole "#RRGGBB", DecorationRole QColor, EditRole "#RRGGBB".
/// - Raw: read-only, "0x%04X (%u)" or empty.  Quality: read-only text, ForegroundRole
///   colour (Good green, Unknown grey, others red); ToolTipRole holds the last error text.
/// - Value: scaled value as text (6 significant digits) or empty. Editable only when the
///   tag isWritable(): setData() on it does NOT change the model; it emits
///   writeRequested(id, value) and returns true.
/// Invalid edits (bad address, gain 0, unparsable numbers) make setData() return false.
class TagTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        Name, Area, Address, Type, Gain, Offset, Unit, Writable, Plot, Color, Raw, Value, Quality,
        ColumnCount
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

signals:
    /// Any configuration change: add, remove, duplicate, setData on a config column.
    /// Not emitted for live values or setTags().
    void tagsEdited();
    /// The user entered a new value for a writable tag.
    void writeRequested(mb::TagId id, double scaledValue);

private:
    // Implementation-defined; the task may change the private section freely.
    struct Live {
        bool valid = false;
        quint16 raw = 0;
        double value = 0.0;
        mb::Quality quality = mb::Quality::Unknown;
        QString status;
    };
    QList<Tag> m_tags;
    QHash<TagId, Live> m_live;
    TagId m_nextId = 1;
};

} // namespace mb
