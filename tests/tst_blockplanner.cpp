#include "BlockPlanner.h"

#include <QtTest>

#include <algorithm>
#include <random>

using namespace mb;

namespace {

Tag mk(TagId id, quint16 addr, Area area = Area::HoldingRegister)
{
    Tag t;
    t.id = id;
    t.address = addr;
    t.area = area;
    return t;
}

ReadBlock blk(Area area, quint16 start, quint16 count, QList<TagId> ids, QList<quint16> offs)
{
    ReadBlock b;
    b.area = area;
    b.start = start;
    b.count = count;
    b.tagIds = ids;
    b.offsets = offs;
    return b;
}

constexpr Area HR = Area::HoldingRegister;
constexpr Area IR = Area::InputRegister;

} // namespace

class TstBlockPlanner : public QObject
{
    Q_OBJECT
private slots:
    void empty()
    {
        QVERIFY(planBlocks({}).isEmpty());
    }

    void singleTag()
    {
        const auto r = planBlocks({mk(7, 100)});
        QCOMPARE(r, (QList<ReadBlock>{blk(HR, 100, 1, {7}, {0})}));
    }

    void twoAdjacent()
    {
        PlannerOptions o;
        o.maxGap = 0;
        const auto r = planBlocks({mk(1, 10), mk(2, 11)}, o);
        QCOMPARE(r, (QList<ReadBlock>{blk(HR, 10, 2, {1, 2}, {0, 1})}));
    }

    void maxGapZeroSplits()
    {
        PlannerOptions o;
        o.maxGap = 0;
        const auto r = planBlocks({mk(1, 10), mk(2, 12)}, o);
        QCOMPARE(r, (QList<ReadBlock>{blk(HR, 10, 1, {1}, {0}), blk(HR, 12, 1, {2}, {0})}));
    }

    void negativeGapTreatedAsZero()
    {
        PlannerOptions o;
        o.maxGap = -5;
        const auto r = planBlocks({mk(1, 10), mk(2, 11), mk(3, 13)}, o);
        QCOMPARE(r, (QList<ReadBlock>{blk(HR, 10, 2, {1, 2}, {0, 1}), blk(HR, 13, 1, {3}, {0})}));
    }

    void gapEqualsMaxGapMerges()
    {
        PlannerOptions o;
        o.maxGap = 3;
        // 10 and 14: three unused registers (11,12,13)
        const auto r = planBlocks({mk(1, 10), mk(2, 14)}, o);
        QCOMPARE(r, (QList<ReadBlock>{blk(HR, 10, 5, {1, 2}, {0, 4})}));
    }

    void gapAboveMaxGapSplits()
    {
        PlannerOptions o;
        o.maxGap = 3;
        const auto r = planBlocks({mk(1, 10), mk(2, 15)}, o);
        QCOMPARE(r, (QList<ReadBlock>{blk(HR, 10, 1, {1}, {0}), blk(HR, 15, 1, {2}, {0})}));
    }

    void duplicateAddresses()
    {
        const auto r = planBlocks({mk(9, 50), mk(3, 50), mk(5, 51)});
        QCOMPARE(r, (QList<ReadBlock>{blk(HR, 50, 2, {3, 9, 5}, {0, 0, 1})}));
    }

    void unsortedInput()
    {
        const auto r = planBlocks({mk(4, 20), mk(1, 10), mk(3, 12), mk(2, 11)});
        QCOMPARE(r, (QList<ReadBlock>{blk(HR, 10, 11, {1, 2, 3, 4}, {0, 1, 2, 10})}));
    }

    void mixedAreas()
    {
        const auto r = planBlocks({mk(1, 5, IR), mk(2, 5, HR), mk(3, 6, IR), mk(4, 100, HR), mk(5, 6, HR)});
        QCOMPARE(r, (QList<ReadBlock>{
                        blk(HR, 5, 2, {2, 5}, {0, 1}),
                        blk(HR, 100, 1, {4}, {0}),
                        blk(IR, 5, 2, {1, 3}, {0, 1}),
                    }));
    }

    void splitsAtMaxCount()
    {
        QList<Tag> tags;
        for (int i = 0; i < 300; ++i)
            tags.append(mk(static_cast<TagId>(i + 1), static_cast<quint16>(1000 + i)));
        const auto r = planBlocks(tags);
        QCOMPARE(r.size(), 3);
        QCOMPARE(r[0].start, quint16(1000));
        QCOMPARE(r[0].count, quint16(125));
        QCOMPARE(r[1].start, quint16(1125));
        QCOMPARE(r[1].count, quint16(125));
        QCOMPARE(r[2].start, quint16(1250));
        QCOMPARE(r[2].count, quint16(50));
        QCOMPARE(r[0].tagIds.size(), 125);
        QCOMPARE(r[1].tagIds.size(), 125);
        QCOMPARE(r[2].tagIds.size(), 50);
        QCOMPARE(r[2].offsets.last(), quint16(49));
        QCOMPARE(r[2].tagIds.first(), TagId(251));
    }

    void gapMergeRespectsMaxCount()
    {
        PlannerOptions o;
        o.maxGap = 1000;
        // span 0..124 is 125 registers: fits; 125 would make it 126: splits.
        const auto r = planBlocks({mk(1, 0), mk(2, 124), mk(3, 125)}, o);
        QCOMPARE(r, (QList<ReadBlock>{blk(HR, 0, 125, {1, 2}, {0, 124}), blk(HR, 125, 1, {3}, {0})}));
    }

    void tagAt65535()
    {
        const auto r = planBlocks({mk(1, 65535), mk(2, 65534), mk(3, 65526)});
        QCOMPARE(r, (QList<ReadBlock>{blk(HR, 65526, 10, {3, 2, 1}, {0, 8, 9})}));
    }

    void tagAt65535WithHugeMaxCount()
    {
        PlannerOptions o;
        o.maxGap = 70000;
        o.maxCount = 100000;
        const auto r = planBlocks({mk(1, 0), mk(2, 65535)}, o);
        // never a 65536-register block (count is 16-bit)
        QCOMPARE(r.size(), 2);
        QCOMPARE(r[0].count, quint16(1));
        QCOMPARE(r[1].start, quint16(65535));
    }

    void customOptions()
    {
        PlannerOptions o;
        o.maxGap = 2;
        o.maxCount = 4;
        const auto r = planBlocks({mk(1, 0), mk(2, 3), mk(3, 4), mk(4, 6), mk(5, 10)}, o);
        QCOMPARE(r, (QList<ReadBlock>{
                        blk(HR, 0, 4, {1, 2}, {0, 3}),
                        blk(HR, 4, 3, {3, 4}, {0, 2}),
                        blk(HR, 10, 1, {5}, {0}),
                    }));
    }

    void maxCountBelowOneTreatedAsOne()
    {
        PlannerOptions o;
        o.maxCount = 0;
        const auto r = planBlocks({mk(1, 1), mk(2, 2)}, o);
        QCOMPARE(r, (QList<ReadBlock>{blk(HR, 1, 1, {1}, {0}), blk(HR, 2, 1, {2}, {0})}));
    }

    void duplicatesWithMaxCountOne()
    {
        PlannerOptions o;
        o.maxCount = 1;
        const auto r = planBlocks({mk(2, 5), mk(1, 5)}, o);
        // same address always fits in a one-register block
        QCOMPARE(r, (QList<ReadBlock>{blk(HR, 5, 1, {1, 2}, {0, 0})}));
    }

    void randomizedProperties()
    {
        std::mt19937 rng(12345);
        auto rnd = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };

        for (int iter = 0; iter < 200; ++iter) {
            PlannerOptions o;
            o.maxGap = rnd(-2, 40);
            o.maxCount = rnd(0, 130);
            const int effGap = std::max(0, o.maxGap);
            const int effCount = std::max(1, o.maxCount);

            const int n = rnd(0, 150);
            const int span = rnd(1, 4) == 1 ? 65536 : rnd(1, 600);
            const int base = rnd(0, 65536 - span);
            QList<Tag> tags;
            for (int i = 0; i < n; ++i) {
                int addr = base + rnd(0, span - 1);
                if (rnd(0, 9) == 0)
                    addr = 65535;
                tags.append(mk(static_cast<TagId>(i + 1), static_cast<quint16>(addr),
                               rnd(0, 1) ? HR : IR));
            }
            std::shuffle(tags.begin(), tags.end(), rng);

            const auto blocks = planBlocks(tags, o);
            QByteArray ctx = "iter " + QByteArray::number(iter);

            QHash<TagId, int> seen;
            bool seenIr = false;
            for (int b = 0; b < blocks.size(); ++b) {
                const ReadBlock &blk_ = blocks[b];
                QVERIFY2(blk_.count >= 1 && blk_.count <= effCount, ctx);
                QVERIFY2(int(blk_.start) + int(blk_.count) <= 65536, ctx);
                QCOMPARE(blk_.tagIds.size(), blk_.offsets.size());
                QVERIFY2(!blk_.tagIds.isEmpty(), ctx);
                if (blk_.area == IR)
                    seenIr = true;
                else
                    QVERIFY2(!seenIr, ctx);
                for (int i = 0; i < blk_.tagIds.size(); ++i) {
                    QVERIFY2(blk_.offsets[i] < blk_.count, ctx);
                    if (i > 0) {
                        const bool ordered = blk_.offsets[i - 1] < blk_.offsets[i]
                            || (blk_.offsets[i - 1] == blk_.offsets[i] && blk_.tagIds[i - 1] < blk_.tagIds[i]);
                        QVERIFY2(ordered, ctx);
                    }
                    const TagId id = blk_.tagIds[i];
                    seen[id] += 1;
                    const Tag &t = tags[std::find_if(tags.begin(), tags.end(),
                                                     [id](const Tag &x) { return x.id == id; })
                                        - tags.begin()];
                    QVERIFY2(t.area == blk_.area, ctx);
                    QVERIFY2(int(blk_.start) + int(blk_.offsets[i]) == int(t.address), ctx);
                }
                // first and last entries touch the block edges
                QCOMPARE(int(blk_.offsets.first()), 0);
                QCOMPARE(int(blk_.offsets.last()), int(blk_.count) - 1);
                if (b > 0 && blocks[b - 1].area == blk_.area) {
                    const ReadBlock &prev = blocks[b - 1];
                    const int prevEnd = int(prev.start) + int(prev.count); // exclusive
                    QVERIFY2(int(blk_.start) >= prevEnd, ctx);
                    // blocks are only separate if merging was not allowed
                    const int gap = int(blk_.start) - prevEnd;
                    const int mergedSpan = int(blk_.start) + 1 - int(prev.start);
                    QVERIFY2(gap > effGap || mergedSpan > effCount, ctx);
                }
                // internal gaps respect maxGap
                for (int i = 1; i < blk_.offsets.size(); ++i)
                    QVERIFY2(int(blk_.offsets[i]) - int(blk_.offsets[i - 1]) - 1 <= effGap, ctx);
            }
            QCOMPARE(seen.size(), int(tags.size()));
            for (auto it = seen.cbegin(); it != seen.cend(); ++it)
                QCOMPARE(it.value(), 1);
        }
    }
};

QTEST_GUILESS_MAIN(TstBlockPlanner)
#include "tst_blockplanner.moc"
