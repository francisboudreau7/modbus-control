#include "TagCodec.h"

#include <QtTest>

#include <cmath>
#include <limits>
#include <optional>
#include <utility>

Q_DECLARE_METATYPE(mb::ValueType)
Q_DECLARE_METATYPE(mb::ByteOrder)

namespace {

constexpr mb::ByteOrder kOrders[] = {
    mb::ByteOrder::ABCD,
    mb::ByteOrder::CDAB,
    mb::ByteOrder::BADC,
    mb::ByteOrder::DCBA,
};

mb::Tag makeTag(mb::ValueType type, double gain, double offset,
                mb::ByteOrder order = mb::ByteOrder::ABCD)
{
    mb::Tag tag;
    tag.type = type;
    tag.gain = gain;
    tag.offset = offset;
    tag.byteOrder = order;
    return tag;
}

bool nearly(double a, double b)
{
    return std::abs(a - b) <= 1e-9;
}

// Splits a 32-bit ABCD pattern into the two registers a device holds for `order`
// (reg n = the lower address). Independent of combineRegisters: this is the inverse.
std::pair<quint16, quint16> splitRegisters(mb::ByteOrder order, quint32 pattern)
{
    const quint16 ab = static_cast<quint16>(pattern >> 16);
    const quint16 cd = static_cast<quint16>(pattern & 0xFFFFu);
    const auto swap = [](quint16 r) { return static_cast<quint16>((r << 8) | (r >> 8)); };
    switch (order) {
    case mb::ByteOrder::ABCD:
        return {ab, cd};
    case mb::ByteOrder::CDAB:
        return {cd, ab};
    case mb::ByteOrder::BADC:
        return {swap(ab), swap(cd)};
    case mb::ByteOrder::DCBA:
        return {swap(cd), swap(ab)};
    }
    return {ab, cd};
}

} // namespace

class TstTagCodec : public QObject
{
    Q_OBJECT
private slots:
    void decodeValue_data();
    void decodeValue();

    void encodeValue_data();
    void encodeValue();

    void roundTripRawGain01();
    void roundTripScaledGain01();
    void rounding_data();
    void rounding();

    void outOfRange_data();
    void outOfRange();

    void gainZero();
    void nonFinite_data();
    void nonFinite();

    void combineByteOrders_data();
    void combineByteOrders();
    void combine16BitIgnoresSecondRegister_data();
    void combine16BitIgnoresSecondRegister();
    void float32KnownValues_data();
    void float32KnownValues();
    void float32Infinity();
    void float32Nan();
    void int32AndUInt32_data();
    void int32AndUInt32();
    void gainOffsetOn32Bit();
    void int16IgnoresHighBits_data();
    void int16IgnoresHighBits();
    void encode32BitReturnsNullopt_data();
    void encode32BitReturnsNullopt();
};

void TstTagCodec::decodeValue_data()
{
    QTest::addColumn<mb::ValueType>("type");
    QTest::addColumn<quint16>("raw");
    QTest::addColumn<double>("gain");
    QTest::addColumn<double>("offset");
    QTest::addColumn<double>("expected");

    QTest::newRow("int16 zero") << mb::ValueType::Int16 << quint16(0x0000) << 1.0 << 0.0 << 0.0;
    QTest::newRow("int16 one") << mb::ValueType::Int16 << quint16(0x0001) << 1.0 << 0.0 << 1.0;
    QTest::newRow("int16 max") << mb::ValueType::Int16 << quint16(0x7FFF) << 1.0 << 0.0 << 32767.0;
    QTest::newRow("int16 min") << mb::ValueType::Int16 << quint16(0x8000) << 1.0 << 0.0 << -32768.0;
    QTest::newRow("int16 minus one") << mb::ValueType::Int16 << quint16(0xFFFF) << 1.0 << 0.0 << -1.0;
    QTest::newRow("uint16 max") << mb::ValueType::UInt16 << quint16(0xFFFF) << 1.0 << 0.0 << 65535.0;
    QTest::newRow("uint16 0x8000 is positive") << mb::ValueType::UInt16 << quint16(0x8000) << 1.0 << 0.0
                                               << 32768.0;
    QTest::newRow("gain 0.1 offset -40") << mb::ValueType::Int16 << quint16(500) << 0.1 << -40.0 << 10.0;
    QTest::newRow("gain 0.1 offset -40 negative raw") << mb::ValueType::Int16 << quint16(0xFFFF) << 0.1 << -40.0
                                                      << -40.1;
    QTest::newRow("negative gain int16") << mb::ValueType::Int16 << quint16(5) << -2.0 << 0.0 << -10.0;
    QTest::newRow("negative gain uint16 with offset") << mb::ValueType::UInt16 << quint16(30) << -1.0 << 100.0
                                                      << 70.0;
}

void TstTagCodec::decodeValue()
{
    QFETCH(mb::ValueType, type);
    QFETCH(quint16, raw);
    QFETCH(double, gain);
    QFETCH(double, offset);
    QFETCH(double, expected);

    const double actual = mb::decodeValue(makeTag(type, gain, offset), raw);
    QVERIFY2(nearly(actual, expected),
             qPrintable(QStringLiteral("actual %1, expected %2").arg(actual).arg(expected)));
}

void TstTagCodec::encodeValue_data()
{
    QTest::addColumn<mb::ValueType>("type");
    QTest::addColumn<double>("gain");
    QTest::addColumn<double>("offset");
    QTest::addColumn<double>("scaled");
    QTest::addColumn<quint16>("expected");

    // Inverses of the decodeValue cases.
    QTest::newRow("int16 zero") << mb::ValueType::Int16 << 1.0 << 0.0 << 0.0 << quint16(0x0000);
    QTest::newRow("int16 one") << mb::ValueType::Int16 << 1.0 << 0.0 << 1.0 << quint16(0x0001);
    QTest::newRow("int16 max") << mb::ValueType::Int16 << 1.0 << 0.0 << 32767.0 << quint16(0x7FFF);
    QTest::newRow("int16 min") << mb::ValueType::Int16 << 1.0 << 0.0 << -32768.0 << quint16(0x8000);
    QTest::newRow("int16 minus one") << mb::ValueType::Int16 << 1.0 << 0.0 << -1.0 << quint16(0xFFFF);
    QTest::newRow("uint16 max") << mb::ValueType::UInt16 << 1.0 << 0.0 << 65535.0 << quint16(0xFFFF);
    QTest::newRow("uint16 0x8000 is positive") << mb::ValueType::UInt16 << 1.0 << 0.0 << 32768.0
                                               << quint16(0x8000);
    QTest::newRow("gain 0.1 offset -40") << mb::ValueType::Int16 << 0.1 << -40.0 << 10.0 << quint16(500);
    QTest::newRow("negative gain int16") << mb::ValueType::Int16 << -2.0 << 0.0 << -10.0 << quint16(5);
    QTest::newRow("negative gain uint16 with offset") << mb::ValueType::UInt16 << -1.0 << 100.0 << 70.0
                                                      << quint16(30);
}

void TstTagCodec::encodeValue()
{
    QFETCH(mb::ValueType, type);
    QFETCH(double, gain);
    QFETCH(double, offset);
    QFETCH(double, scaled);
    QFETCH(quint16, expected);

    const std::optional<quint16> actual = mb::encodeValue(makeTag(type, gain, offset), scaled);
    QVERIFY(actual.has_value());
    QCOMPARE(*actual, expected);
}

void TstTagCodec::roundTripRawGain01()
{
    // Every raw register survives decode followed by encode, for both types.
    const mb::Tag signedTag = makeTag(mb::ValueType::Int16, 0.1, -40.0);
    const mb::Tag unsignedTag = makeTag(mb::ValueType::UInt16, 0.1, -40.0);
    for (int i = 0; i <= 0xFFFF; ++i) {
        const quint16 raw = static_cast<quint16>(i);

        const std::optional<quint16> signedBack = mb::encodeValue(signedTag, mb::decodeValue(signedTag, raw));
        QVERIFY(signedBack.has_value());
        QCOMPARE(*signedBack, raw);

        const std::optional<quint16> unsignedBack
            = mb::encodeValue(unsignedTag, mb::decodeValue(unsignedTag, raw));
        QVERIFY(unsignedBack.has_value());
        QCOMPARE(*unsignedBack, raw);
    }
}

void TstTagCodec::roundTripScaledGain01()
{
    // Engineering values in steps of 0.1 come back within rounding error of the input.
    const mb::Tag tag = makeTag(mb::ValueType::Int16, 0.1, -40.0);
    for (int i = -2000; i <= 2000; ++i) {
        const double v = i * 0.1;
        const std::optional<quint16> raw = mb::encodeValue(tag, v);
        QVERIFY(raw.has_value());
        const double back = mb::decodeValue(tag, *raw);
        QVERIFY2(nearly(back, v), qPrintable(QStringLiteral("v %1, back %2").arg(v).arg(back)));
    }
}

void TstTagCodec::rounding_data()
{
    QTest::addColumn<double>("scaled");
    QTest::addColumn<qint16>("expected");

    QTest::newRow("2.4 down") << 2.4 << qint16(2);
    QTest::newRow("2.6 up") << 2.6 << qint16(3);
    QTest::newRow("-2.4 toward zero") << -2.4 << qint16(-2);
    QTest::newRow("-2.6 away from zero") << -2.6 << qint16(-3);
}

void TstTagCodec::rounding()
{
    QFETCH(double, scaled);
    QFETCH(qint16, expected);

    const std::optional<quint16> actual = mb::encodeValue(makeTag(mb::ValueType::Int16, 1.0, 0.0), scaled);
    QVERIFY(actual.has_value());
    QCOMPARE(static_cast<qint16>(*actual), expected);
}

void TstTagCodec::outOfRange_data()
{
    QTest::addColumn<mb::ValueType>("type");
    QTest::addColumn<double>("scaled");

    QTest::newRow("int16 above max") << mb::ValueType::Int16 << 32768.0;
    QTest::newRow("int16 below min") << mb::ValueType::Int16 << -32769.0;
    QTest::newRow("uint16 below zero") << mb::ValueType::UInt16 << -1.0;
    QTest::newRow("uint16 above max") << mb::ValueType::UInt16 << 65536.0;
}

void TstTagCodec::outOfRange()
{
    QFETCH(mb::ValueType, type);
    QFETCH(double, scaled);

    const std::optional<quint16> actual = mb::encodeValue(makeTag(type, 1.0, 0.0), scaled);
    QVERIFY(!actual.has_value());
}

void TstTagCodec::gainZero()
{
    const std::optional<quint16> signedResult = mb::encodeValue(makeTag(mb::ValueType::Int16, 0.0, 0.0), 1.0);
    QVERIFY(!signedResult.has_value());

    const std::optional<quint16> unsignedResult = mb::encodeValue(makeTag(mb::ValueType::UInt16, 0.0, 5.0), 5.0);
    QVERIFY(!unsignedResult.has_value());
}

void TstTagCodec::nonFinite_data()
{
    QTest::addColumn<mb::ValueType>("type");
    QTest::addColumn<double>("scaled");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    QTest::newRow("int16 NaN") << mb::ValueType::Int16 << nan;
    QTest::newRow("int16 +inf") << mb::ValueType::Int16 << inf;
    QTest::newRow("int16 -inf") << mb::ValueType::Int16 << -inf;
    QTest::newRow("uint16 NaN") << mb::ValueType::UInt16 << nan;
    QTest::newRow("uint16 +inf") << mb::ValueType::UInt16 << inf;
    QTest::newRow("uint16 -inf") << mb::ValueType::UInt16 << -inf;
}

void TstTagCodec::nonFinite()
{
    QFETCH(mb::ValueType, type);
    QFETCH(double, scaled);

    const std::optional<quint16> actual = mb::encodeValue(makeTag(type, 1.0, 0.0), scaled);
    QVERIFY(!actual.has_value());
}

void TstTagCodec::combineByteOrders_data()
{
    QTest::addColumn<mb::ByteOrder>("order");
    QTest::addColumn<quint16>("r0");
    QTest::addColumn<quint16>("r1");

    // A=0x12 B=0x34 C=0x56 D=0x78 must always come back as 0x12345678.
    QTest::newRow("ABCD") << mb::ByteOrder::ABCD << quint16(0x1234) << quint16(0x5678);
    QTest::newRow("CDAB") << mb::ByteOrder::CDAB << quint16(0x5678) << quint16(0x1234);
    QTest::newRow("BADC") << mb::ByteOrder::BADC << quint16(0x3412) << quint16(0x7856);
    QTest::newRow("DCBA") << mb::ByteOrder::DCBA << quint16(0x7856) << quint16(0x3412);
}

void TstTagCodec::combineByteOrders()
{
    QFETCH(mb::ByteOrder, order);
    QFETCH(quint16, r0);
    QFETCH(quint16, r1);

    for (const mb::ValueType type : {mb::ValueType::Int32, mb::ValueType::UInt32, mb::ValueType::Float32}) {
        const quint32 raw = mb::combineRegisters(makeTag(type, 1.0, 0.0, order), r0, r1);
        QCOMPARE(raw, quint32(0x12345678));
    }
}

void TstTagCodec::combine16BitIgnoresSecondRegister_data()
{
    QTest::addColumn<mb::ValueType>("type");
    QTest::addColumn<quint16>("r0");
    QTest::addColumn<quint16>("r1");
    QTest::addColumn<quint32>("expected");

    QTest::newRow("int16") << mb::ValueType::Int16 << quint16(0x1234) << quint16(0x5678) << quint32(0x1234);
    QTest::newRow("uint16") << mb::ValueType::UInt16 << quint16(0x1234) << quint16(0x5678) << quint32(0x1234);
    QTest::newRow("int16 r1 all ones") << mb::ValueType::Int16 << quint16(0x8000) << quint16(0xFFFF)
                                       << quint32(0x8000);
}

void TstTagCodec::combine16BitIgnoresSecondRegister()
{
    QFETCH(mb::ValueType, type);
    QFETCH(quint16, r0);
    QFETCH(quint16, r1);
    QFETCH(quint32, expected);

    for (const mb::ByteOrder order : kOrders)
        QCOMPARE(mb::combineRegisters(makeTag(type, 1.0, 0.0, order), r0, r1), expected);
    // The second argument defaults to 0 and is ignored for 16-bit types.
    QCOMPARE(mb::combineRegisters(makeTag(type, 1.0, 0.0), r0), expected);
}

void TstTagCodec::float32KnownValues_data()
{
    QTest::addColumn<quint32>("pattern");
    QTest::addColumn<double>("expected");

    QTest::newRow("1.0") << quint32(0x3F800000) << 1.0;
    QTest::newRow("-2.5") << quint32(0xC0200000) << -2.5;
    QTest::newRow("123.456") << quint32(0x42F6E979) << static_cast<double>(123.456f);
    QTest::newRow("0.0") << quint32(0x00000000) << 0.0;
    QTest::newRow("smallest denormal") << quint32(0x00000001)
                                       << static_cast<double>(std::numeric_limits<float>::denorm_min());
}

void TstTagCodec::float32KnownValues()
{
    QFETCH(quint32, pattern);
    QFETCH(double, expected);

    for (const mb::ByteOrder order : kOrders) {
        const auto [r0, r1] = splitRegisters(order, pattern);
        const mb::Tag tag = makeTag(mb::ValueType::Float32, 1.0, 0.0, order);
        const quint32 raw = mb::combineRegisters(tag, r0, r1);
        QCOMPARE(raw, pattern);
        QCOMPARE(mb::decodeValue(tag, raw), expected);
    }
}

void TstTagCodec::float32Infinity()
{
    for (const mb::ByteOrder order : kOrders) {
        const auto [r0, r1] = splitRegisters(order, 0x7F800000u);
        const mb::Tag tag = makeTag(mb::ValueType::Float32, 1.0, 0.0, order);
        const double decoded = mb::decodeValue(tag, mb::combineRegisters(tag, r0, r1));
        QVERIFY(std::isinf(decoded));
        QVERIFY(decoded > 0.0);
    }
}

void TstTagCodec::float32Nan()
{
    for (const mb::ByteOrder order : kOrders) {
        const auto [r0, r1] = splitRegisters(order, 0x7FC00000u);
        const mb::Tag tag = makeTag(mb::ValueType::Float32, 1.0, 0.0, order);
        const double decoded = mb::decodeValue(tag, mb::combineRegisters(tag, r0, r1));
        QVERIFY(std::isnan(decoded));
    }
}

void TstTagCodec::int32AndUInt32_data()
{
    QTest::addColumn<mb::ValueType>("type");
    QTest::addColumn<quint32>("raw");
    QTest::addColumn<double>("expected");

    QTest::newRow("int32 all ones") << mb::ValueType::Int32 << quint32(0xFFFFFFFF) << -1.0;
    QTest::newRow("int32 min") << mb::ValueType::Int32 << quint32(0x80000000) << -2147483648.0;
    QTest::newRow("int32 max") << mb::ValueType::Int32 << quint32(0x7FFFFFFF) << 2147483647.0;
    QTest::newRow("uint32 all ones") << mb::ValueType::UInt32 << quint32(0xFFFFFFFF) << 4294967295.0;
    QTest::newRow("uint32 0x80000000 is positive") << mb::ValueType::UInt32 << quint32(0x80000000)
                                                   << 2147483648.0;
}

void TstTagCodec::int32AndUInt32()
{
    QFETCH(mb::ValueType, type);
    QFETCH(quint32, raw);
    QFETCH(double, expected);

    const double actual = mb::decodeValue(makeTag(type, 1.0, 0.0), raw);
    QVERIFY2(nearly(actual, expected),
             qPrintable(QStringLiteral("actual %1, expected %2").arg(actual).arg(expected)));
}

void TstTagCodec::gainOffsetOn32Bit()
{
    // 100.0f = 0x42C80000: 100 * 0.5 + 1 = 51.
    QCOMPARE(mb::decodeValue(makeTag(mb::ValueType::Float32, 0.5, 1.0), 0x42C80000u), 51.0);
    // Int32 -1 * 2 + 10 = 8.
    QVERIFY(nearly(mb::decodeValue(makeTag(mb::ValueType::Int32, 2.0, 10.0), 0xFFFFFFFFu), 8.0));
}

void TstTagCodec::int16IgnoresHighBits_data()
{
    QTest::addColumn<mb::ValueType>("type");
    QTest::addColumn<quint32>("raw");
    QTest::addColumn<double>("expected");

    QTest::newRow("int16 0xFFFF0005") << mb::ValueType::Int16 << quint32(0xFFFF0005) << 5.0;
    QTest::newRow("int16 0x0001FFFF") << mb::ValueType::Int16 << quint32(0x0001FFFF) << -1.0;
    QTest::newRow("int16 0xABCD8000") << mb::ValueType::Int16 << quint32(0xABCD8000) << -32768.0;
    QTest::newRow("uint16 0xABCD8000") << mb::ValueType::UInt16 << quint32(0xABCD8000) << 32768.0;
    QTest::newRow("uint16 0x0001FFFF") << mb::ValueType::UInt16 << quint32(0x0001FFFF) << 65535.0;
}

void TstTagCodec::int16IgnoresHighBits()
{
    QFETCH(mb::ValueType, type);
    QFETCH(quint32, raw);
    QFETCH(double, expected);

    QCOMPARE(mb::decodeValue(makeTag(type, 1.0, 0.0), raw), expected);
}

void TstTagCodec::encode32BitReturnsNullopt_data()
{
    QTest::addColumn<mb::ValueType>("type");
    QTest::addColumn<double>("scaled");

    for (const mb::ValueType type : {mb::ValueType::Int32, mb::ValueType::UInt32, mb::ValueType::Float32}) {
        const QString name = mb::toString(type);
        QTest::addRow("%s 0", qPrintable(name)) << type << 0.0;
        QTest::addRow("%s 1", qPrintable(name)) << type << 1.0;
        QTest::addRow("%s -1", qPrintable(name)) << type << -1.0;
        QTest::addRow("%s 100", qPrintable(name)) << type << 100.0;
    }
}

void TstTagCodec::encode32BitReturnsNullopt()
{
    QFETCH(mb::ValueType, type);
    QFETCH(double, scaled);

    for (const mb::ByteOrder order : kOrders)
        QVERIFY(!mb::encodeValue(makeTag(type, 1.0, 0.0, order), scaled).has_value());
}

QTEST_GUILESS_MAIN(TstTagCodec)
#include "tst_tagcodec.moc"
