#include "EncodingDetect.h"

// Stub created by T150; implemented by T151.

namespace mb {

RegisterPair deviceRegisters(const Tag &, quint32)
{
    return {};
}

QString toString(const Encoding &encoding)
{
    return toString(encoding.type) + QLatin1Char(' ') + toString(encoding.order);
}

Encoding encodingOf(const Tag &tag)
{
    return {tag.type, tag.byteOrder};
}

Tag withEncoding(Tag tag, const Encoding &encoding)
{
    tag.type = encoding.type;
    tag.byteOrder = encoding.order;
    return tag;
}

std::optional<std::pair<double, double>> parseDisplayValue(const QString &)
{
    return std::nullopt;
}

EncodingResult detectEncoding(const Tag &, const QList<RegisterPair> &, const EncodingOptions &)
{
    return {};
}

bool currentEncodingSuspicious(const Tag &, const EncodingResult &)
{
    return false;
}

} // namespace mb
