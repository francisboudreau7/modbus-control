#include "TagCodec.h"

#include <cmath>

namespace mb {

double decodeValue(const Tag &tag, quint16 raw)
{
    double decoded = 0.0;
    switch (tag.type) {
    case ValueType::Int16:
        decoded = static_cast<double>(static_cast<qint16>(raw));
        break;
    case ValueType::UInt16:
        decoded = static_cast<double>(raw);
        break;
    }
    return decoded * tag.gain + tag.offset;
}

std::optional<quint16> encodeValue(const Tag &tag, double scaled)
{
    if (tag.gain == 0.0 || !std::isfinite(scaled))
        return std::nullopt;

    const double x = (scaled - tag.offset) / tag.gain;
    const double r = std::round(x);

    double lo = 0.0;
    double hi = 0.0;
    switch (tag.type) {
    case ValueType::Int16:
        lo = -32768.0;
        hi = 32767.0;
        break;
    case ValueType::UInt16:
        lo = 0.0;
        hi = 65535.0;
        break;
    }

    // Written as a positive comparison so that NaN (e.g. from a NaN offset) is rejected too.
    if (!(r >= lo && r <= hi))
        return std::nullopt;

    if (tag.type == ValueType::Int16)
        return static_cast<quint16>(static_cast<qint16>(r));
    return static_cast<quint16>(r);
}

} // namespace mb
