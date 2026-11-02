#pragma once

#include <QtGlobal>

#include <string>

namespace mbsim {

/// True for the Modbus write function codes (FC05, FC06, FC0F, FC10, FC17).
bool isWriteFunction(unsigned fc);

/// One log line for a request (no trailing newline). `pdu` points just after the function code.
/// Reads: "[4] unit=1 fc=0x03 read addr=0 count=12".
/// FC06:  "[4] unit=1 fc=0x06 write addr=100 value=0x03E8 (1000)".
/// FC01/FC02 read bits like the register reads. FC05: "[4] unit=1 fc=0x05 write coil addr=3 value=ON"
/// (ON for 0xFF00, OFF for 0x0000, else the hex value). FC15: "[4] unit=1 fc=0x0F write coils addr=0
/// count=3 values=ON,OFF,ON" (the first 16 bits at most, then ",...").
/// FC16:  "[4] unit=1 fc=0x10 write addr=110 count=2 values=0x42F7,0x0000" and, for exactly two
///        registers, " | " + describe32(). At most 16 values are listed, then ",...".
std::string describeRequest(int fd, unsigned unit, unsigned fc, const unsigned char *pdu, int pduLen);

/// The 32-bit value of two registers (r0 = lower address) read in each byte order, with the
/// same meaning as the client's ByteOrder: "float ABCD=123.5 CDAB=1.7e-41 BADC=... DCBA=...".
/// Floats use up to 9 significant digits; non-finite values print as nan/inf.
std::string describe32(quint16 r0, quint16 r1);

} // namespace mbsim
