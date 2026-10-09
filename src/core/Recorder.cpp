#include "Recorder.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace mb {

namespace {

double nan()
{
    return std::numeric_limits<double>::quiet_NaN();
}

} // namespace

Recorder::Recorder(qsizetype capacity)
    : m_capacity(std::max<qsizetype>(1, capacity))
{
}

qsizetype Recorder::capacity() const
{
    return m_capacity;
}

void Recorder::setCapacity(qsizetype capacity)
{
    m_capacity = std::max<qsizetype>(1, capacity);
    clear();
}

void Recorder::clear()
{
    m_head = 0;
    m_size = 0;
    m_evicted = 0;
    m_times.clear();
    m_tagOrder.clear();
    m_values.clear();
}

QDateTime Recorder::startTime() const
{
    return m_start;
}

void Recorder::setStartTime(const QDateTime &start)
{
    m_start = start;
}

void Recorder::append(const SampleBatch &batch)
{
    if (m_size > 0 && batch.t < timeAt(m_size - 1))
        return;

    if (m_times.isEmpty())
        m_times.resize(m_capacity);

    if (m_size == m_capacity) {
        // Full: drop the oldest row; its physical slot is reused for the new row.
        m_head = (m_head + 1) % m_capacity;
        ++m_evicted;
        --m_size;
    }

    const qsizetype idx = physicalIndex(m_size);
    m_times[idx] = batch.t;

    for (TagId id : std::as_const(m_tagOrder))
        m_values[id][idx] = nan();

    for (const TagSample &sample : batch.samples) {
        if (!m_values.contains(sample.tagId)) {
            m_tagOrder.append(sample.tagId);
            m_values.insert(sample.tagId, QList<double>(m_capacity, nan()));
        }
        if (sample.quality == Quality::Good)
            m_values[sample.tagId][idx] = sample.value;
    }

    ++m_size;
}

qsizetype Recorder::size() const
{
    return m_size;
}

quint64 Recorder::evictedCount() const
{
    return m_evicted;
}

QList<TagId> Recorder::tagIds() const
{
    return m_tagOrder;
}

double Recorder::timeAt(qsizetype row) const
{
    if (row < 0 || row >= m_size)
        return nan();
    return m_times[physicalIndex(row)];
}

double Recorder::valueAt(qsizetype row, TagId tag) const
{
    const auto it = m_values.constFind(tag);
    if (it == m_values.constEnd() || row < 0 || row >= m_size)
        return nan();
    return it.value()[physicalIndex(row)];
}

qsizetype Recorder::rowAtOrBefore(double t) const
{
    // First row with time > t, minus one.
    qsizetype lo = 0;
    qsizetype hi = m_size;
    while (lo < hi) {
        const qsizetype mid = lo + (hi - lo) / 2;
        if (timeAt(mid) > t)
            hi = mid;
        else
            lo = mid + 1;
    }
    return lo - 1;
}

double Recorder::sampleAt(TagId tag, double t) const
{
    const qsizetype row = rowAtOrBefore(t);
    if (row < 0)
        return nan();
    return valueAt(row, tag);
}

std::pair<qsizetype, qsizetype> Recorder::rowRange(double t1, double t2) const
{
    if (t1 > t2)
        return {0, 0};

    // Lower bound over logical rows: first row whose time satisfies pred.
    auto firstRowWhere = [this](auto pred) {
        qsizetype lo = 0;
        qsizetype hi = m_size;
        while (lo < hi) {
            const qsizetype mid = lo + (hi - lo) / 2;
            if (pred(timeAt(mid)))
                hi = mid;
            else
                lo = mid + 1;
        }
        return lo;
    };

    const qsizetype first = firstRowWhere([t1](double time) { return time >= t1; });
    const qsizetype last = firstRowWhere([t2](double time) { return time > t2; });
    return {first, last};
}

qsizetype Recorder::physicalIndex(qsizetype row) const
{
    return (m_head + row) % m_capacity;
}

} // namespace mb
