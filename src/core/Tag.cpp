#include "Tag.h"

#include <QCoreApplication>

namespace mb {

QString toString(Area area)
{
    switch (area) {
    case Area::HoldingRegister: return QStringLiteral("Holding");
    case Area::InputRegister: return QStringLiteral("Input");
    case Area::Coil: return QStringLiteral("Coil");
    case Area::DiscreteInput: return QStringLiteral("Discrete");
    }
    return {};
}

QString areaName(Area area)
{
    switch (area) {
    case Area::HoldingRegister: return QCoreApplication::translate("mb::Tag", "Holding");
    case Area::InputRegister: return QCoreApplication::translate("mb::Tag", "Input");
    case Area::Coil: return QCoreApplication::translate("mb::Tag", "Coil");
    case Area::DiscreteInput: return QCoreApplication::translate("mb::Tag", "Discrete");
    }
    return {};
}

QString areaPrefix(Area area)
{
    switch (area) {
    case Area::HoldingRegister: return QStringLiteral("HR");
    case Area::InputRegister: return QStringLiteral("IR");
    case Area::Coil: return QStringLiteral("CO");
    case Area::DiscreteInput: return QStringLiteral("DI");
    }
    return {};
}

QString areaLabel(Area area)
{
    switch (area) {
    case Area::HoldingRegister:
        return QCoreApplication::translate("mb::Tag", "Holding register (FC03/FC06/FC16)");
    case Area::InputRegister: return QCoreApplication::translate("mb::Tag", "Input register (FC04)");
    case Area::Coil: return QCoreApplication::translate("mb::Tag", "Coil (FC01/FC05/FC15)");
    case Area::DiscreteInput: return QCoreApplication::translate("mb::Tag", "Discrete input (FC02)");
    }
    return {};
}

QString bitText(double value)
{
    return value != 0.0 ? QStringLiteral("ON") : QStringLiteral("OFF");
}

QString toString(ValueType type)
{
    switch (type) {
    case ValueType::Int16: return QStringLiteral("int16");
    case ValueType::UInt16: return QStringLiteral("uint16");
    case ValueType::Int32: return QStringLiteral("int32");
    case ValueType::UInt32: return QStringLiteral("uint32");
    case ValueType::Float32: return QStringLiteral("float32");
    case ValueType::Bool: return QStringLiteral("bool");
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

QString qualityText(Quality quality)
{
    switch (quality) {
    case Quality::Unknown: return QCoreApplication::translate("mb::Tag", "Unknown");
    case Quality::Good: return QCoreApplication::translate("mb::Tag", "Good");
    case Quality::Timeout: return QCoreApplication::translate("mb::Tag", "Timeout");
    case Quality::Exception: return QCoreApplication::translate("mb::Tag", "Exception");
    case Quality::Disconnected: return QCoreApplication::translate("mb::Tag", "Disconnected");
    case Quality::Error: return QCoreApplication::translate("mb::Tag", "Error");
    }
    return {};
}

QString toString(YAxis axis)
{
    return axis == YAxis::Right ? QStringLiteral("Right") : QStringLiteral("Left");
}

QString axisText(YAxis axis)
{
    return axis == YAxis::Right ? QCoreApplication::translate("mb::Tag", "Right")
                                : QCoreApplication::translate("mb::Tag", "Left");
}

} // namespace mb
