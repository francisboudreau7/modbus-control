#pragma once

#include "SampleBatch.h"

#include <QDateTime>
#include <QHash>
#include <QList>

#include <utility>

namespace mb {

/// Column store of recorded samples. Each appended SampleBatch becomes one row
/// (timestamp + one value per known tag). Missing or non-Good samples are stored as
/// NaN. When full, the oldest row is evicted (ring buffer).
///
/// Rows are addressed by index 0..size()-1, 0 = oldest row still held. Row times are
/// non-decreasing (append() ignores a batch older than the last row).
/// Not thread-safe; used from the GUI thread.
class Recorder
{
public:
    explicit Recorder(qsizetype capacity = 72000); ///< 2 h at 100 ms
    qsizetype capacity() const;
    void setCapacity(qsizetype capacity); ///< also clears

    void clear(); ///< removes all rows and tags; keeps capacity and start time

    /// Wall-clock time corresponding to t = 0.
    QDateTime startTime() const;
    void setStartTime(const QDateTime &start);

    void append(const SampleBatch &batch);

    qsizetype size() const;
    /// Total rows evicted since the last clear(); row i has absolute sequence number
    /// evictedCount() + i. Lets the plot append incrementally.
    quint64 evictedCount() const;

    /// Tag ids seen since the last clear(), in order of first appearance. A tag first
    /// seen after older rows exist has NaN in those rows.
    QList<TagId> tagIds() const;

    double timeAt(qsizetype row) const;
    /// NaN if the tag is unknown or had no Good sample in that row.
    double valueAt(qsizetype row, TagId tag) const;

    /// Index of the last row with time <= t, or -1 if none.
    qsizetype rowAtOrBefore(double t) const;
    /// Value of the last row at or before t (may be NaN). Used by the cursors.
    double sampleAt(TagId tag, double t) const;
    /// Half-open row range [first, last) whose times lie within [t1, t2] (t1 <= t2).
    /// Empty range -> first == last.
    std::pair<qsizetype, qsizetype> rowRange(double t1, double t2) const;

private:
    qsizetype physicalIndex(qsizetype row) const;

    qsizetype m_capacity;
    qsizetype m_head = 0;  ///< physical index of row 0
    qsizetype m_size = 0;
    quint64 m_evicted = 0;
    QDateTime m_start;
    QList<double> m_times;                 ///< ring, size m_capacity once allocated
    QList<TagId> m_tagOrder;
    QHash<TagId, QList<double>> m_values;  ///< ring per tag, same indexing as m_times
};

} // namespace mb
