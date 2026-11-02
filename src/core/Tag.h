#pragma once

#include <QString>
#include <QtGlobal>

#include <cmath>
#include <optional>

namespace mb {

/// Modbus data area a tag lives in.
enum class Area : quint8 {
    HoldingRegister, ///< FC03 read, FC06/FC16 write
    InputRegister,   ///< FC04 read, read-only
    Coil,            ///< v1.3.4: FC01 read, FC05 (one) / FC15 (several) write; 1 bit per address
    DiscreteInput,   ///< v1.3.4: FC02 read, read-only; 1 bit per address
};

/// True for Coil and DiscreteInput (addresses hold bits, tags are ValueType::Bool).
constexpr bool isBitArea(Area area)
{
    return area == Area::Coil || area == Area::DiscreteInput;
}

/// True for the areas a client can write: HoldingRegister and Coil.
constexpr bool isWritableArea(Area area)
{
    return area == Area::HoldingRegister || area == Area::Coil;
}

/// Read function code of an area: 3 (HR), 4 (IR), 1 (coil), 2 (discrete input).
constexpr int readFunctionCode(Area area)
{
    switch (area) {
    case Area::HoldingRegister: return 3;
    case Area::InputRegister: return 4;
    case Area::Coil: return 1;
    case Area::DiscreteInput: return 2;
    }
    return 0;
}

/// Write function code PollWorker uses for `count` consecutive addresses of a writable area:
/// HR 6 (count 1) / 16 (more), coil 5 (count 1) / 15 (more); 0 for read-only areas.
constexpr int writeFunctionCode(Area area, int count)
{
    if (area == Area::HoldingRegister)
        return count == 1 ? 6 : 16;
    if (area == Area::Coil)
        return count == 1 ? 5 : 15;
    return 0;
}

/// How the raw register(s) are interpreted before scaling.
enum class ValueType : quint8 {
    Int16,   ///< 1 register, two's complement, -32768..32767
    UInt16,  ///< 1 register, 0..65535
    Int32,   ///< 2 registers, two's complement
    UInt32,  ///< 2 registers, unsigned
    Float32, ///< 2 registers, IEEE-754 single precision
    Bool,    ///< v1.3.4: one coil or discrete input, value 0 (OFF) or 1 (ON); no scaling
};

/// Number of consecutive addresses a value of this type occupies (1 or 2): registers for
/// register types, one bit for Bool.
constexpr int registerCount(ValueType type)
{
    return (type == ValueType::Int16 || type == ValueType::UInt16 || type == ValueType::Bool) ? 1 : 2;
}

/// The type/area rule: Bool goes with bit areas (Coil, DiscreteInput) and only with them.
constexpr bool typeFitsArea(ValueType type, Area area)
{
    return (type == ValueType::Bool) == isBitArea(area);
}

/// Byte order of a 32-bit value spread over two consecutive registers. A..D are the
/// value's bytes from most to least significant; "reg n" is the lower address.
enum class ByteOrder : quint8 {
    ABCD, ///< big endian:    reg n = A B, reg n+1 = C D
    CDAB, ///< word swap:     reg n = C D, reg n+1 = A B
    BADC, ///< byte swap:     reg n = B A, reg n+1 = D C
    DCBA, ///< little endian: reg n = D C, reg n+1 = B A
};

/// Y axis a tag's curve uses on the trend plots.
enum class YAxis : quint8 {
    Left,
    Right,
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
    bool writable = false; ///< only meaningful for writable areas (holding registers, coils)
    /// Read from projects before v1.2.6 only (ProjectIO maps it to AnalysisSettings::pvTagId and
    /// setpointTagIds); not used by the UI and no longer written.
    bool analyzed = false;
    YAxis axis = YAxis::Left; ///< plot axis on both tabs
    /// Quick-write values (scaled, like the Value column); empty = no button. Only used for
    /// writable tags (isWritable()).
    std::optional<double> quickA;
    std::optional<double> quickB;
    /// Write limits (scaled values, inclusive); empty = no limit. Only used for writable tags.
    /// writeMin <= writeMax when both are set.
    std::optional<double> writeMin;
    std::optional<double> writeMax;
    /// False when the 32-bit encoding (type, byte order) was guessed by encoding detection, is
    /// still being detected (a new or re-addressed 32-bit tag), or was flagged as implausible, and
    /// nobody confirmed it yet; writes then ask first. True for 16-bit tags and for encodings the
    /// user set or confirmed, or that the readings confirmed.
    bool encodingVerified = true;
    /// The user answered "Keep int16" (or uint16) to the half-of-a-float32 check: this 16-bit tag
    /// is not checked again automatically. Cleared when its area, address or type changes.
    bool confirmed16Bit = false;

    bool isWritable() const { return writable && isWritableArea(area); }
    /// v1.3.4: a coil or discrete input tag (type Bool). Gain, offset and byte order do not
    /// apply; the value is 0 or 1.
    bool isBit() const { return isBitArea(area); }
    /// True when `scaled` is finite and within writeMin/writeMax (unset limits don't apply).
    bool withinWriteLimits(double scaled) const
    {
        return std::isfinite(scaled)
               && (!writeMin || scaled >= *writeMin) && (!writeMax || scaled <= *writeMax);
    }
    /// Last register used by the tag; a 2-register tag at address 65535 does not fit.
    bool fitsAddressSpace() const { return int(address) + registerCount(type) <= 65536; }
    friend bool operator==(const Tag &, const Tag &) = default;
};

// v1.3.4 translations: toString() texts are the canonical English names used in project and
// session files, CSV files and setData(); they are never translated. Texts shown to the user
// come from the display functions below (areaName, areaLabel, qualityText, axisText), which go
// through QCoreApplication::translate (context "mb::Tag").

QString toString(Area area); ///< "Holding", "Input", "Coil", "Discrete" (file format; not translated)
/// v1.3.4: translated short area name for display (the tag table's Area column): "Holding",
/// "Input", "Coil", "Discrete" in English.
QString areaName(Area area);
QString toString(ValueType type); ///< "int16", "uint16", "int32", "uint32", "float32", "bool"

/// v1.3.4: register-text prefix of an area: "HR", "IR", "CO", "DI" (as in "CO 12").
QString areaPrefix(Area area);
/// v1.3.4: area name with its function codes, for combos and tooltips (translated):
/// "Holding register (FC03/FC06/FC16)", "Input register (FC04)", "Coil (FC01/FC05/FC15)",
/// "Discrete input (FC02)".
QString areaLabel(Area area);
/// v1.3.4: a Bool value as text: "ON" for 1 (any non-zero), "OFF" for 0. Not translated (same
/// in the French interface, as on most HMIs; also written to the write log).
QString bitText(double value);
QString toString(ByteOrder order); ///< "ABCD", "CDAB", "BADC", "DCBA"
QString toString(Quality quality); ///< "Unknown", "Good", "Timeout", ... (not translated)
/// v1.3.4: translated quality for display (the tag table's Quality column).
QString qualityText(Quality quality);
QString toString(YAxis axis); ///< "Left", "Right" (file format; not translated)
/// v1.3.4: translated axis name for display: "Left", "Right" in English.
QString axisText(YAxis axis);

} // namespace mb
