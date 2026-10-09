#pragma once

#include "Tag.h"

#include <optional>

namespace mb {

/// Combines a tag's registers into its raw value. For 16-bit types returns r0 (r1 is
/// ignored). For 32-bit types returns the 32-bit pattern in ABCD order (A = most
/// significant byte), undoing tag.byteOrder; r0 is the register at tag.address, r1 the one
/// at tag.address + 1.
quint32 combineRegisters(const Tag &tag, quint16 r0, quint16 r1 = 0);

/// Interprets a raw value (as returned by combineRegisters) per tag.type and applies
/// gain/offset: decoded * gain + offset.
/// Int16: low 16 bits as two's complement. UInt16: low 16 bits. Int32: two's complement.
/// UInt32: unsigned. Float32: IEEE-754 single (NaN and infinities pass through unchanged).
double decodeValue(const Tag &tag, quint32 raw);

/// Inverse of decodeValue for writes of 16-bit types: (scaled - offset) / gain, rounded to
/// the nearest integer. Returns nullopt if gain is 0, the input is not finite, the result
/// is outside the range of tag.type, or tag.type is a 32-bit type (read-only for now).
std::optional<quint16> encodeValue(const Tag &tag, double scaled);

} // namespace mb
