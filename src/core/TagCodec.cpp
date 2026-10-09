#include "TagCodec.h"

#include <bit>
#include <cfloat>
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

std::optional<QList<quint16>> encodeRegisters(const Tag &tag, double scaled)
{
    if (registerCount(tag.type) == 1) {
        const std::optional<quint16> reg = encodeValue(tag, scaled);
        if (!reg)
            return std::nullopt;
        return QList<quint16>{*reg};
    }
    if (tag.gain == 0.0 || !std::isfinite(scaled))
        return std::nullopt;

    const double x = (scaled - tag.offset) / tag.gain;
    if (!std::isfinite(x))
        return std::nullopt;

    // The 32-bit pattern in ABCD form, i.e. the value combineRegisters would return.
    quint32 pattern = 0;
    switch (tag.type) {
    case ValueType::Int32: {
        const double r = std::round(x);
        // Positive comparison so that the bounds are rejected only when really exceeded.
        if (!(r >= -2147483648.0 && r <= 2147483647.0))
            return std::nullopt;
        pattern = static_cast<quint32>(static_cast<qint32>(r));
        break;
    }
    case ValueType::UInt32: {
        const double r = std::round(x);
        if (!(r >= 0.0 && r <= 4294967295.0))
            return std::nullopt;
        pattern = static_cast<quint32>(r);
        break;
    }
    case ValueType::Float32:
        if (std::fabs(x) > FLT_MAX)
            return std::nullopt;
        pattern = std::bit_cast<quint32>(static_cast<float>(x));
        break;
    case ValueType::Int16:
    case ValueType::UInt16:
        return std::nullopt; // handled above
    }

    // Exact inverse of combineRegisters: undo the byte order of the two registers.
    const quint16 ab = static_cast<quint16>(pattern >> 16);
    const quint16 cd = static_cast<quint16>(pattern & 0xFFFFu);
    switch (tag.byteOrder) {
    case ByteOrder::ABCD:
        return QList<quint16>{ab, cd};
    case ByteOrder::CDAB:
        return QList<quint16>{cd, ab};
    case ByteOrder::BADC:
        return QList<quint16>{swapBytes(ab), swapBytes(cd)};
    case ByteOrder::DCBA:
        return QList<quint16>{swapBytes(cd), swapBytes(ab)};
    }
    return std::nullopt;
}

} // namespace mb
