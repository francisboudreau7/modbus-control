#include "RequestLog.h"

#include <bit>
#include <cstdio>

namespace mbsim {

namespace {

unsigned be16(const unsigned char *p)
{
    return (unsigned(p[0]) << 8) | unsigned(p[1]);
}

quint16 swapBytes(quint16 v)
{
    return quint16((v >> 8) | (v << 8));
}

std::string floatText(quint32 pattern)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", double(std::bit_cast<float>(pattern)));
    return buf;
}

std::string hex16(unsigned v)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%04X", v & 0xFFFFu);
    return buf;
}

} // namespace

bool isWriteFunction(unsigned fc)
{
    return fc == 0x05 || fc == 0x06 || fc == 0x0F || fc == 0x10 || fc == 0x17;
}

std::string describe32(quint16 r0, quint16 r1)
{
    const quint32 abcd = (quint32(r0) << 16) | r1;
    const quint32 cdab = (quint32(r1) << 16) | r0;
    const quint32 badc = (quint32(swapBytes(r0)) << 16) | swapBytes(r1);
    const quint32 dcba = (quint32(swapBytes(r1)) << 16) | swapBytes(r0);
    return "float ABCD=" + floatText(abcd) + " CDAB=" + floatText(cdab) + " BADC=" + floatText(badc)
           + " DCBA=" + floatText(dcba);
}

std::string describeRequest(int fd, unsigned unit, unsigned fc, const unsigned char *pdu, int pduLen)
{
    char head[64];
    std::snprintf(head, sizeof(head), "[%d] unit=%u fc=0x%02X", fd, unit, fc);
    std::string text = head;
    char buf[96];
    switch (fc) {
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
        if (pduLen >= 4) {
            std::snprintf(buf, sizeof(buf), " read addr=%u count=%u", be16(pdu), be16(pdu + 2));
            text += buf;
        }
        break;
    case 0x05:
        if (pduLen >= 4) {
            std::snprintf(buf, sizeof(buf), " write coil addr=%u value=%s", be16(pdu), hex16(be16(pdu + 2)).c_str());
            text += buf;
        }
        break;
    case 0x06:
        if (pduLen >= 4) {
            const unsigned v = be16(pdu + 2);
            std::snprintf(buf, sizeof(buf), " write addr=%u value=%s (%u)", be16(pdu), hex16(v).c_str(), v);
            text += buf;
        }
        break;
    case 0x10:
        if (pduLen >= 5) {
            const unsigned count = be16(pdu + 2);
            std::snprintf(buf, sizeof(buf), " write addr=%u count=%u values=", be16(pdu), count);
            text += buf;
            unsigned shown = 0;
            for (; shown < count && shown < 16 && pduLen >= int(5 + 2 * (shown + 1)); ++shown) {
                if (shown)
                    text += ',';
                text += hex16(be16(pdu + 5 + 2 * shown));
            }
            if (shown < count)
                text += ",...";
            if (count == 2 && pduLen >= 9)
                text += " | " + describe32(quint16(be16(pdu + 5)), quint16(be16(pdu + 7)));
        }
        break;
    case 0x17:
        if (pduLen >= 9) {
            std::snprintf(buf, sizeof(buf), " readwrite read addr=%u count=%u write addr=%u count=%u",
                          be16(pdu), be16(pdu + 2), be16(pdu + 4), be16(pdu + 6));
            text += buf;
        }
        break;
    default:
        break;
    }
    return text;
}

} // namespace mbsim
