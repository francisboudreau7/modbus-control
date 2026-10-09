#include "Recorder.h"

#include <QtTest>

#include <utility>

using namespace mb;

namespace {

TagSample good(TagId id, double value)
{
    TagSample s;
    s.tagId = id;
    s.value = value;
    s.quality = Quality::Good;
    return s;
}

TagSample bad(TagId id, Quality quality)
{
    TagSample s;
    s.tagId = id;
    s.value = 999.0; // must never be stored
    s.quality = quality;
    return s;
}

SampleBatch batch(double t, QList<TagSample> samples)
{
    SampleBatch b;
    b.t = t;
    b.samples = std::move(samples);
    return b;
}

} // namespace

class TstRecorder : public QObject
{
    Q_OBJECT
private slots:
    void emptyRecorder();
    void appendAndReadBack();
    void nonGoodSamplesAreNaN();
    void missingTagIsNaN();
    void lateTagIsNaNInEarlierRows();
    void outOfOrderBatchIgnored();
    void equalTimestampsAllowed();
    void wraparoundKeepsLastRows();
    void rowAtOrBefore();
    void sampleAt();
    void rowRange();
    void rowRangeAfterWraparound();
    void clearKeepsCapacityAndStartTime();
    void setCapacityClears();
    void nonPositiveCapacityBecomesOne();
    void startTimeGetSet();
    void largeRingWithManyAppends();
};

void TstRecorder::emptyRecorder()
{
    Recorder r(5);
    QCOMPARE(r.size(), qsizetype(0));
    QCOMPARE(r.capacity(), qsizetype(5));
    QCOMPARE(r.evictedCount(), quint64(0));
    QVERIFY(r.tagIds().isEmpty());
    QCOMPARE(r.rowAtOrBefore(0.0), qsizetype(-1));
    QVERIFY(qIsNaN(r.sampleAt(1, 0.0)));
    QVERIFY(qIsNaN(r.timeAt(0)));
    QVERIFY(qIsNaN(r.valueAt(0, 1)));
    const auto range = r.rowRange(0.0, 10.0);
    QCOMPARE(range.first, qsizetype(0));
    QCOMPARE(range.second, qsizetype(0));
}

void TstRecorder::appendAndReadBack()
{
    Recorder r(10);
    r.append(batch(0.0, {good(1, 1.5)}));
    r.append(batch(0.1, {good(1, 2.5), good(2, 7.0)}));

    QCOMPARE(r.size(), qsizetype(2));
    QCOMPARE(r.timeAt(0), 0.0);
    QCOMPARE(r.timeAt(1), 0.1);
    QCOMPARE(r.valueAt(0, 1), 1.5);
    QCOMPARE(r.valueAt(1, 1), 2.5);
    QCOMPARE(r.valueAt(1, 2), 7.0);
    QVERIFY(r.tagIds() == QList<TagId>({1, 2}));
    QCOMPARE(r.evictedCount(), quint64(0));
}

void TstRecorder::nonGoodSamplesAreNaN()
{
    Recorder r(10);
    r.append(batch(0.0, {good(1, 1.0), bad(2, Quality::Timeout), bad(3, Quality::Unknown)}));
    r.append(batch(1.0, {bad(1, Quality::Exception), good(2, 4.0), bad(3, Quality::Error)}));

    QCOMPARE(r.valueAt(0, 1), 1.0);
    QVERIFY(qIsNaN(r.valueAt(0, 2)));
    QVERIFY(qIsNaN(r.valueAt(0, 3)));
    QVERIFY(qIsNaN(r.valueAt(1, 1)));
    QCOMPARE(r.valueAt(1, 2), 4.0);
    QVERIFY(qIsNaN(r.valueAt(1, 3)));
    // Tags are still registered even when their samples were not Good.
    QVERIFY(r.tagIds() == QList<TagId>({1, 2, 3}));
}

void TstRecorder::missingTagIsNaN()
{
    Recorder r(10);
    r.append(batch(0.0, {good(1, 1.0), good(2, 2.0)}));
    r.append(batch(1.0, {good(1, 3.0)}));

    QCOMPARE(r.valueAt(1, 1), 3.0);
    QVERIFY(qIsNaN(r.valueAt(1, 2)));
    QCOMPARE(r.valueAt(0, 2), 2.0);
}

void TstRecorder::lateTagIsNaNInEarlierRows()
{
    Recorder r(10);
    r.append(batch(0.0, {good(1, 1.0)}));
    r.append(batch(1.0, {good(1, 2.0)}));
    r.append(batch(2.0, {good(3, 30.0), good(2, 20.0)}));

    QVERIFY(qIsNaN(r.valueAt(0, 2)));
    QVERIFY(qIsNaN(r.valueAt(1, 2)));
    QVERIFY(qIsNaN(r.valueAt(0, 3)));
    QVERIFY(qIsNaN(r.valueAt(1, 3)));
    QCOMPARE(r.valueAt(2, 2), 20.0);
    QCOMPARE(r.valueAt(2, 3), 30.0);
    QCOMPARE(r.valueAt(1, 1), 2.0);
    // Order of first appearance: 1 from the first batch, then 3 before 2 as in the batch.
    QVERIFY(r.tagIds() == QList<TagId>({1, 3, 2}));
}

void TstRecorder::outOfOrderBatchIgnored()
{
    Recorder r(10);
    r.append(batch(1.0, {good(1, 1.0)}));
    r.append(batch(0.5, {good(1, 99.0), good(2, 99.0)}));

    QCOMPARE(r.size(), qsizetype(1));
    QCOMPARE(r.timeAt(0), 1.0);
    QCOMPARE(r.valueAt(0, 1), 1.0);
    QVERIFY(r.tagIds() == QList<TagId>({1}));
}

void TstRecorder::equalTimestampsAllowed()
{
    Recorder r(10);
    r.append(batch(1.0, {good(1, 1.0)}));
    r.append(batch(1.0, {good(1, 2.0)}));

    QCOMPARE(r.size(), qsizetype(2));
    QCOMPARE(r.timeAt(0), 1.0);
    QCOMPARE(r.timeAt(1), 1.0);
    QCOMPARE(r.valueAt(1, 1), 2.0);
}

void TstRecorder::wraparoundKeepsLastRows()
{
    Recorder r(3);
    r.append(batch(0.0, {good(1, 0.0), good(2, 100.0)})); // tag 2 only in the first row
    r.append(batch(1.0, {good(1, 10.0)}));
    r.append(batch(2.0, {good(1, 20.0)}));
    r.append(batch(3.0, {good(1, 30.0)}));
    r.append(batch(4.0, {good(1, 40.0)}));

    QCOMPARE(r.size(), qsizetype(3));
    QCOMPARE(r.evictedCount(), quint64(2));
    QCOMPARE(r.timeAt(0), 2.0);
    QCOMPARE(r.timeAt(1), 3.0);
    QCOMPARE(r.timeAt(2), 4.0);
    QCOMPARE(r.valueAt(0, 1), 20.0);
    QCOMPARE(r.valueAt(1, 1), 30.0);
    QCOMPARE(r.valueAt(2, 1), 40.0);
    // Tag 2 was seen before the eviction, so it is still listed, but its rows are NaN.
    QVERIFY(r.tagIds() == QList<TagId>({1, 2}));
    QVERIFY(qIsNaN(r.valueAt(0, 2)));
    QVERIFY(qIsNaN(r.valueAt(2, 2)));
}

void TstRecorder::rowAtOrBefore()
{
    Recorder r(10);
    r.append(batch(1.0, {good(1, 1.0)}));
    r.append(batch(2.0, {good(1, 2.0)}));
    r.append(batch(3.0, {good(1, 3.0)}));

    QCOMPARE(r.rowAtOrBefore(0.5), qsizetype(-1)); // before first
    QCOMPARE(r.rowAtOrBefore(1.0), qsizetype(0));  // exact hit
    QCOMPARE(r.rowAtOrBefore(2.5), qsizetype(1));  // between rows
    QCOMPARE(r.rowAtOrBefore(3.0), qsizetype(2));  // exact last
    QCOMPARE(r.rowAtOrBefore(10.0), qsizetype(2)); // after last
}

void TstRecorder::sampleAt()
{
    Recorder r(10);
    r.append(batch(1.0, {good(1, 1.0)}));
    r.append(batch(2.0, {bad(1, Quality::Timeout)}));
    r.append(batch(3.0, {good(1, 3.0)}));

    QVERIFY(qIsNaN(r.sampleAt(1, 0.5)));    // before first row
    QCOMPARE(r.sampleAt(1, 1.5), 1.0);      // last row at or before t
    QVERIFY(qIsNaN(r.sampleAt(1, 2.0)));    // exact row with NaN value
    QVERIFY(qIsNaN(r.sampleAt(1, 2.7)));    // carried back to the NaN row
    QCOMPARE(r.sampleAt(1, 3.0), 3.0);
    QCOMPARE(r.sampleAt(1, 99.0), 3.0);
    QVERIFY(qIsNaN(r.sampleAt(42, 99.0)));  // unknown tag
}

void TstRecorder::rowRange()
{
    Recorder r(10);
    for (int i = 0; i < 5; ++i)
        r.append(batch(double(i), {good(1, double(i))}));

    auto range = r.rowRange(1.0, 3.0);
    QCOMPARE(range.first, qsizetype(1));
    QCOMPARE(range.second, qsizetype(4));

    range = r.rowRange(1.5, 1.7);
    QCOMPARE(range.first, range.second); // empty, between rows

    range = r.rowRange(3.0, 1.0);
    QCOMPARE(range.first, qsizetype(0));
    QCOMPARE(range.second, qsizetype(0));

    range = r.rowRange(-10.0, 100.0);
    QCOMPARE(range.first, qsizetype(0));
    QCOMPARE(range.second, qsizetype(5));

    range = r.rowRange(7.0, 9.0);
    QCOMPARE(range.first, range.second); // after last row
}

void TstRecorder::rowRangeAfterWraparound()
{
    Recorder r(3);
    for (int i = 0; i < 5; ++i)
        r.append(batch(double(i), {good(1, double(i))}));

    // Rows now hold times 2, 3, 4.
    auto range = r.rowRange(0.0, 10.0);
    QCOMPARE(range.first, qsizetype(0));
    QCOMPARE(range.second, qsizetype(3));

    range = r.rowRange(3.0, 3.0);
    QCOMPARE(range.first, qsizetype(1));
    QCOMPARE(range.second, qsizetype(2));

    range = r.rowRange(0.0, 1.9);
    QCOMPARE(range.first, range.second); // only evicted times

    QCOMPARE(r.sampleAt(1, 3.5), 3.0);
    QCOMPARE(r.rowAtOrBefore(1.0), qsizetype(-1));
}

void TstRecorder::clearKeepsCapacityAndStartTime()
{
    Recorder r(3);
    const QDateTime start = QDateTime::fromString(QStringLiteral("2026-01-02T03:04:05Z"), Qt::ISODate);
    r.setStartTime(start);
    for (int i = 0; i < 5; ++i)
        r.append(batch(double(i), {good(TagId(i), double(i))}));

    r.clear();
    QCOMPARE(r.size(), qsizetype(0));
    QCOMPARE(r.evictedCount(), quint64(0));
    QVERIFY(r.tagIds().isEmpty());
    QCOMPARE(r.capacity(), qsizetype(3));
    QCOMPARE(r.startTime(), start);
    QCOMPARE(r.rowAtOrBefore(10.0), qsizetype(-1));

    // Usable again after clear, including older timestamps.
    r.append(batch(0.0, {good(7, 7.0)}));
    QCOMPARE(r.size(), qsizetype(1));
    QCOMPARE(r.valueAt(0, 7), 7.0);
}

void TstRecorder::setCapacityClears()
{
    Recorder r(3);
    for (int i = 0; i < 5; ++i)
        r.append(batch(double(i), {good(1, double(i))}));

    r.setCapacity(5);
    QCOMPARE(r.capacity(), qsizetype(5));
    QCOMPARE(r.size(), qsizetype(0));
    QCOMPARE(r.evictedCount(), quint64(0));
    QVERIFY(r.tagIds().isEmpty());

    for (int i = 0; i < 7; ++i)
        r.append(batch(double(i), {good(1, double(i))}));
    QCOMPARE(r.size(), qsizetype(5));
    QCOMPARE(r.evictedCount(), quint64(2));
    QCOMPARE(r.timeAt(0), 2.0);
}

void TstRecorder::nonPositiveCapacityBecomesOne()
{
    Recorder r(0);
    QCOMPARE(r.capacity(), qsizetype(1));
    r.append(batch(0.0, {good(1, 1.0)}));
    r.append(batch(1.0, {good(1, 2.0)}));
    QCOMPARE(r.size(), qsizetype(1));
    QCOMPARE(r.valueAt(0, 1), 2.0);
    QCOMPARE(r.evictedCount(), quint64(1));

    r.setCapacity(-4);
    QCOMPARE(r.capacity(), qsizetype(1));
    QCOMPARE(r.size(), qsizetype(0));
}

void TstRecorder::startTimeGetSet()
{
    Recorder r;
    QVERIFY(!r.startTime().isValid());
    const QDateTime start = QDateTime::fromString(QStringLiteral("2026-05-06T07:08:09Z"), Qt::ISODate);
    r.setStartTime(start);
    QCOMPARE(r.startTime(), start);
}

void TstRecorder::largeRingWithManyAppends()
{
    Recorder r; // default capacity 72000
    QCOMPARE(r.capacity(), qsizetype(72000));

    for (int i = 0; i < 100000; ++i)
        r.append(batch(double(i), {good(1, double(i)), good(2, double(-i))}));

    QCOMPARE(r.size(), qsizetype(72000));
    QCOMPARE(r.evictedCount(), quint64(28000));
    QCOMPARE(r.timeAt(0), 28000.0);
    QCOMPARE(r.timeAt(71999), 99999.0);
    QCOMPARE(r.valueAt(0, 1), 28000.0);
    QCOMPARE(r.valueAt(71999, 2), -99999.0);
    QCOMPARE(r.rowAtOrBefore(99999.0), qsizetype(71999));
    QVERIFY(r.tagIds() == QList<TagId>({1, 2}));
}

QTEST_GUILESS_MAIN(TstRecorder)
#include "tst_recorder.moc"
