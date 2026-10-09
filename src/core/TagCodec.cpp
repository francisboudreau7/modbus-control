#include "TagCodec.h"

#include <bit>
#include <cmath>

namespace mb {

namespace {

quint16 swapBytes(quint16 r)
{
    return static_cast<quint16>((r << 8) | (r >> 8));
}

} // namespace

quint32 combineRegisters(const Tag &tag, quint16 r0, quint16 r1)
{
    if (registerCount(tag.type) == 1)
        return r0;

    // Bring both registers to "AB" / "CD" form, then join them.
    quint16 hi = r0;
    quint16 lo = r1;
    switch (tag.byteOrder) {
    case ByteOrder::ABCD:
        break;
    case ByteOrder::CDAB:
        hi = r1;
        lo = r0;
        break;
    case ByteOrder::BADC:
        hi = swapBytes(r0);
        lo = swapBytes(r1);
        break;
    case ByteOrder::DCBA:
        hi = swapBytes(r1);
        lo = swapBytes(r0);
        break;
    }
    return (quint32(hi) << 16) | lo;
}

double decodeValue(const Tag &tag, quint32 raw)
{
    double decoded = 0.0;
    switch (tag.type) {
    case ValueType::Int16:
        decoded = static_cast<double>(static_cast<qint16>(static_cast<quint16>(raw)));
        break;
    case ValueType::UInt16:
        decoded = static_cast<double>(static_cast<quint16>(raw));
        break;
    case ValueType::Int32:
        decoded = static_cast<double>(static_cast<qint32>(raw));
        break;
    case ValueType::UInt32:
        decoded = static_cast<double>(raw);
        break;
    case ValueType::Float32:
        decoded = static_cast<double>(std::bit_cast<float>(raw));
        break;
    }
    return decoded * tag.gain + tag.offset;
}

std::optional<quint16> encodeValue(const Tag &tag, double scaled)
{
    if (registerCount(tag.type) != 1)
        return std::nullopt; // 32-bit tags are read-only for now
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
