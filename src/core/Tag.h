#pragma once

#include <QString>
#include <QtGlobal>

namespace mb {

/// Modbus data area a tag lives in.
enum class Area : quint8 {
    HoldingRegister, ///< FC03 read, FC06/FC16 write
    InputRegister,   ///< FC04 read, read-only
};

/// How the raw 16-bit register is interpreted before scaling.
enum class ValueType : quint8 {
    Int16,  ///< two's complement, -32768..32767
    UInt16, ///< 0..65535
};

/// Quality of the most recent sample of a tag.
enum class Quality : quint8 {
    Unknown,      ///< never read yet
    Good,         ///< last read succeeded
    Timeout,      ///< no response within the timeout
    Exception,    ///< server answered with a Modbus exception
    Disconnected, ///< no connection to the server
    Error,        ///< any other failure
};

/// Stable tag identifier, unique within a project; never reused after deletion.
using TagId = quint32;

/// One named register the user reads, plots and optionally writes.
/// scaled value = decoded raw value * gain + offset
struct Tag {
    TagId id = 0;
    QString name;
    Area area = Area::HoldingRegister;
    quint16 address = 0; ///< zero-based protocol address
    ValueType type = ValueType::Int16;
    double gain = 1.0;
    double offset = 0.0;
    QString unit;
    QString color = QStringLiteral("#1f77b4"); ///< "#RRGGBB"
    bool plotted = true;
    bool writable = false; ///< only meaningful for Area::HoldingRegister

    bool isWritable() const { return writable && area == Area::HoldingRegister; }
    friend bool operator==(const Tag &, const Tag &) = default;
};

QString toString(Area area);
QString toString(ValueType type);
QString toString(Quality quality);

} // namespace mb
