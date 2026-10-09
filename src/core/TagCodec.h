#pragma once

#include "Tag.h"

#include <optional>

namespace mb {

/// Interprets a raw register per tag.type (Int16 = two's complement) and applies
/// gain/offset: decoded * gain + offset.
double decodeValue(const Tag &tag, quint16 raw);

/// Inverse of decodeValue for writes: (scaled - offset) / gain, rounded to the
/// nearest integer. Returns nullopt if gain is 0, the input is not finite, or the
/// result is outside the range of tag.type.
std::optional<quint16> encodeValue(const Tag &tag, double scaled);

} // namespace mb
