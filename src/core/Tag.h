#pragma once

#include <QString>
#include <QtGlobal>

namespace mb {

/// Modbus data area a tag lives in.
enum class Area : quint8 {
    HoldingRegister, ///< FC03 read, FC06/FC16 write
    InputRegister,   ///< FC04 read, read-only
};

/// How the raw register(s) are interpreted before scaling.
enum class ValueType : quint8 {
    Int16,   ///< 1 register, two's complement, -32768..32767
    UInt16,  ///< 1 register, 0..65535
    Int32,   ///< 2 registers, two's complement
    UInt32,  ///< 2 registers, unsigned
    Float32, ///< 2 registers, IEEE-754 single precision
};

/// Number of consecutive registers a value of this type occupies (1 or 2).
constexpr int registerCount(ValueType type)
{
    return (type == ValueType::Int16 || type == ValueType::UInt16) ? 1 : 2;
}

/// Byte order of a 32-bit value spread over two consecutive registers. A..D are the
/// value's bytes from most to least significant; "reg n" is the lower address.
enum class ByteOrder : quint8 {
    ABCD, ///< big endian:    reg n = A B, reg n+1 = C D
    CDAB, ///< word swap:     reg n = C D, reg n+1 = A B
    BADC, ///< byte swap:     reg n = B A, reg n+1 = D C
    DCBA, ///< little endian: reg n = D C, reg n+1 = B A
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
    quint16 address = 0; ///< zero-based protocol address of the first register
    ValueType type = ValueType::Int16;
    ByteOrder byteOrder = ByteOrder::ABCD; ///< only used by 32-bit types
    double gain = 1.0;
    double offset = 0.0;
    QString unit;
    QString color = QStringLiteral("#1f77b4"); ///< "#RRGGBB"
    bool plotted = true;
    bool writable = false; ///< only meaningful for 16-bit Area::HoldingRegister tags

    /// 32-bit tags are read-only for now.
    bool isWritable() const
    {
        return writable && area == Area::HoldingRegister && registerCount(type) == 1;
    }
    /// Last register used by the tag; a 2-register tag at address 65535 does not fit.
    bool fitsAddressSpace() const { return int(address) + registerCount(type) <= 65536; }
    friend bool operator==(const Tag &, const Tag &) = default;
};

QString toString(Area area);
QString toString(ValueType type); ///< "int16", "uint16", "int32", "uint32", "float32"
QString toString(ByteOrder order); ///< "ABCD", "CDAB", "BADC", "DCBA"
QString toString(Quality quality);

} // namespace mb
