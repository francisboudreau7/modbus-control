#include "BlockPlanner.h"

#include <algorithm>

namespace mb {

namespace {

void planArea(const QList<Tag> &tags, Area area, int maxGap, int maxCount, QList<ReadBlock> &out)
{
    QList<const Tag *> sorted;
    for (const Tag &t : tags) {
        if (t.area == area && t.fitsAddressSpace())
            sorted.append(&t);
    }
    std::sort(sorted.begin(), sorted.end(), [](const Tag *a, const Tag *b) {
        if (a->address != b->address)
            return a->address < b->address;
        return a->id < b->id;
    });

    bool open = false;
    ReadBlock block;
    int blockEnd = 0;   // highest register covered by the current block
    int blockLimit = 1; // size limit of the current block
    for (const Tag *t : sorted) {
        const int addr = t->address;
        const int regs = registerCount(t->type);
        const int end = addr + regs - 1;
        // A block holding a tag of `regs` registers may be at least that big, even if
        // maxCount is smaller (e.g. maxCount 1 with a 2-register tag): no tag is dropped.
        const int tagLimit = std::max(maxCount, regs);
        if (open) {
            const int gap = addr - blockEnd - 1; // negative when the tags overlap
            const int newEnd = std::max(blockEnd, end);
            const int span = newEnd - block.start + 1;
            const int limit = std::max(blockLimit, tagLimit);
            if (gap <= maxGap && span <= limit) {
                block.offsets.append(static_cast<quint16>(addr - block.start));
                block.tagIds.append(t->id);
                block.count = static_cast<quint16>(span);
                blockEnd = newEnd;
                blockLimit = limit;
                continue;
            }
            out.append(block);
        }
        block = ReadBlock();
        block.area = area;
        block.start = t->address;
        block.count = static_cast<quint16>(regs);
        block.tagIds.append(t->id);
        block.offsets.append(0);
        blockEnd = end;
        blockLimit = tagLimit;
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

