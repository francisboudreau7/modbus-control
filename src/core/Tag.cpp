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

} // namespace mb
