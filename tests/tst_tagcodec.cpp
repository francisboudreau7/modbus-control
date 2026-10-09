#include "TagCodec.h"

#include <QtTest>

#include <cmath>
#include <limits>
#include <optional>

Q_DECLARE_METATYPE(mb::ValueType)

namespace {

mb::Tag makeTag(mb::ValueType type, double gain, double offset)
{
    mb::Tag tag;
    tag.type = type;
    tag.gain = gain;
    tag.offset = offset;
    return tag;
}

bool nearly(double a, double b)
{
    return std::abs(a - b) <= 1e-9;
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

QTEST_GUILESS_MAIN(TstTagCodec)
#include "tst_tagcodec.moc"
