#include "Tag.h"

namespace mb {

QString toString(Area area)
{
    switch (area) {
    case Area::HoldingRegister: return QStringLiteral("Holding");
    case Area::InputRegister: return QStringLiteral("Input");
    }
    return {};
}

QString toString(ValueType type)
{
    switch (type) {
    case ValueType::Int16: return QStringLiteral("int16");
    case ValueType::UInt16: return QStringLiteral("uint16");
    case ValueType::Int32: return QStringLiteral("int32");
    case ValueType::UInt32: return QStringLiteral("uint32");
    case ValueType::Float32: return QStringLiteral("float32");
    }
    return {};
}

QString toString(ByteOrder order)
{
    switch (order) {
    case ByteOrder::ABCD: return QStringLiteral("ABCD");
    case ByteOrder::CDAB: return QStringLiteral("CDAB");
    case ByteOrder::BADC: return QStringLiteral("BADC");
    case ByteOrder::DCBA: return QStringLiteral("DCBA");
    }
    return {};
}

QString toString(Quality quality)
{
    switch (quality) {
    case Quality::Unknown: return QStringLiteral("Unknown");
    case Quality::Good: return QStringLiteral("Good");
    case Quality::Timeout: return QStringLiteral("Timeout");
    case Quality::Exception: return QStringLiteral("Exception");
    case Quality::Disconnected: return QStringLiteral("Disconnected");
    case Quality::Error: return QStringLiteral("Error");
    }
    return {};
}

QString toString(YAxis axis)
{
    return axis == YAxis::Right ? QStringLiteral("Right") : QStringLiteral("Left");
}

} // namespace mb
