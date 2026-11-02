#pragma once

#include "Tag.h"

#include <QList>

namespace mb {

/// One read request covering [start, start + count) in one area.
struct ReadBlock {
    Area area = Area::HoldingRegister;
    quint16 start = 0;
    quint16 count = 0;
    /// Parallel lists: tag tagIds[i] is at register start + offsets[i].
    QList<TagId> tagIds;
    QList<quint16> offsets;

    friend bool operator==(const ReadBlock &, const ReadBlock &) = default;
};

struct PlannerOptions {
    /// Two tags in the same area are merged into one block when the number of unused
    /// registers between them is <= maxGap.
    int maxGap = 8;
    /// Protocol limit for FC03/FC04. A block holding a 2-register tag may use 2 registers
    /// even if maxCount is 1, so no tag is ever dropped.
    int maxCount = 125;
    /// v1.3.4, bit areas (coils, discrete inputs): the same rules counted in bits.
    int maxBitGap = 64;
    int maxBitCount = 2000; ///< protocol limit for FC01/FC02
};

/// Groups tags into as few reads as possible. Each tag occupies registerCount(tag.type)
/// registers starting at tag.address and is never split across blocks; its offset is that
/// of its first register. Tags that do not fit the address space (!fitsAddressSpace())
/// are left out of every block. Rules:
/// - tags are grouped per area; holding-register blocks come first, then input-register, coil
///   and discrete-input blocks (v1.3.4), each sorted by start address; bit areas use maxBitGap and
///   maxBitCount instead of maxGap and maxCount (a Bool tag is 1 bit);
/// - no block exceeds maxCount registers; no tag is split or dropped;
/// - several tags may share an address (each gets its own entry);
/// - input order does not matter; within a block entries are sorted by (offset, tag id).
QList<ReadBlock> planBlocks(const QList<Tag> &tags, const PlannerOptions &options = {});

} // namespace mb
