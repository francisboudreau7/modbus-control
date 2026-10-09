#include "BlockPlanner.h"

#include <algorithm>

namespace mb {

namespace {

void planArea(const QList<Tag> &tags, Area area, int maxGap, int maxCount, QList<ReadBlock> &out)
{
    QList<const Tag *> sorted;
    for (const Tag &t : tags) {
        if (t.area == area)
            sorted.append(&t);
    }
    std::sort(sorted.begin(), sorted.end(), [](const Tag *a, const Tag *b) {
        if (a->address != b->address)
            return a->address < b->address;
        return a->id < b->id;
    });

    bool open = false;
    ReadBlock block;
    int lastAddress = 0; // highest address in the current block
    for (const Tag *t : sorted) {
        const int addr = t->address;
        if (open) {
            const int gap = addr - lastAddress - 1;
            const int span = addr - block.start + 1;
            if (gap <= maxGap && span <= maxCount) {
                block.offsets.append(static_cast<quint16>(addr - block.start));
                block.tagIds.append(t->id);
                block.count = static_cast<quint16>(span);
                lastAddress = addr;
                continue;
            }
            out.append(block);
        }
        block = ReadBlock();
        block.area = area;
        block.start = t->address;
        block.count = 1;
        block.tagIds.append(t->id);
        block.offsets.append(0);
        lastAddress = addr;
        open = true;
    }
    if (open)
        out.append(block);
}

} // namespace

QList<ReadBlock> planBlocks(const QList<Tag> &tags, const PlannerOptions &options)
{
    const int maxGap = std::max(0, options.maxGap);
    // count is a quint16, so never allow a block of 65536 registers.
    const int maxCount = std::clamp(options.maxCount, 1, 65535);

    QList<ReadBlock> result;
    planArea(tags, Area::HoldingRegister, maxGap, maxCount, result);
    planArea(tags, Area::InputRegister, maxGap, maxCount, result);
    return result;
}

} // namespace mb
