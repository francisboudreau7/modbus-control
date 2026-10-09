#include <QtTest>

#include "SignalGen.h"

#include <limits>

using mbsim::SignalGenerator;
using mbsim::SignalKind;
using mbsim::SignalParams;

namespace {

auto s16 = [](quint16 r) { return static_cast<int>(static_cast<qint16>(r)); };

SignalParams makeParams(SignalKind kind, double amplitude, double offset, double period)
{
    SignalParams p;
    p.kind = kind;
    p.amplitude = amplitude;
    p.offset = offset;
    p.periodS = period;
    return p;
}

} // namespace

class TstSignalGen : public QObject
{
    Q_OBJECT
private slots:
    void constant();
    void sine();
    void sinePeriodic();
    void triangle();
    void sawtooth();
    void square();
    void offsetApplied();
    void clamping();
    void twosComplement();
    void counter();
    void counterWraps();
    void noiseBounds();
    void noiseSeeds();
    void lagZeroDt();
    void lagTimeConstant();
    void lagConverges();
    void lagNegativeInput();
    void nextValueMatchesNext_data();
    void nextValueMatchesNext();
    void counterNextValueRaw();
    void nonFiniteAmplitude();
    void encodeInt16();
    void encodeInt32();
    void encodeFloat32();
};

void TstSignalGen::constant()
{
    SignalGenerator gen(makeParams(SignalKind::Constant, 1000.0, 1234.0, 5.0));
    for (double t = 0.0; t < 10.0; t += 0.5)
        QCOMPARE(s16(gen.next(t, 0.5)), 1234);
}

void TstSignalGen::sine()
{
    // amplitude 1000, period 4 s, offset 0
    SignalGenerator gen(makeParams(SignalKind::Sine, 1000.0, 0.0, 4.0));
    QCOMPARE(s16(gen.next(0.0, 0.0)), 0);
    QCOMPARE(s16(gen.next(1.0, 1.0)), 1000);   // period/4
    QVERIFY(qAbs(s16(gen.next(2.0, 1.0))) <= 1); // period/2
    QCOMPARE(s16(gen.next(3.0, 1.0)), -1000);  // 3*period/4
}

void TstSignalGen::sinePeriodic()
{
    SignalGenerator a(makeParams(SignalKind::Sine, 1000.0, 0.0, 4.0));
    SignalGenerator b(makeParams(SignalKind::Sine, 1000.0, 0.0, 4.0));
    for (double t = 0.0; t < 4.0; t += 0.25) {
        const int first = s16(a.next(t, 0.0));
        const int second = s16(b.next(t + 4.0, 0.0));
        QCOMPARE(second, first);
    }
}

void TstSignalGen::triangle()
{
    SignalGenerator gen(makeParams(SignalKind::Triangle, 1000.0, 0.0, 4.0));
    QCOMPARE(s16(gen.next(0.0, 0.0)), -1000);
    QCOMPARE(s16(gen.next(1.0, 1.0)), 0);
    QCOMPARE(s16(gen.next(2.0, 1.0)), 1000);
    QCOMPARE(s16(gen.next(3.0, 1.0)), 0);
}

void TstSignalGen::sawtooth()
{
    SignalGenerator gen(makeParams(SignalKind::Sawtooth, 1000.0, 0.0, 4.0));
    QCOMPARE(s16(gen.next(0.0, 0.0)), -1000);
    // Just before the period ends the value is close to +amplitude.
    QVERIFY(s16(gen.next(3.999, 3.999)) >= 995);
    // Wraps back to the start.
    QCOMPARE(s16(gen.next(4.0, 0.001)), -1000);
}

void TstSignalGen::square()
{
    SignalGenerator gen(makeParams(SignalKind::Square, 1000.0, 0.0, 4.0));
    QCOMPARE(s16(gen.next(0.5, 0.0)), 1000);   // first half
    QCOMPARE(s16(gen.next(1.9, 1.4)), 1000);
    QCOMPARE(s16(gen.next(2.5, 0.6)), -1000);  // second half
    QCOMPARE(s16(gen.next(3.9, 1.4)), -1000);
}

void TstSignalGen::offsetApplied()
{
    SignalGenerator gen(makeParams(SignalKind::Sine, 100.0, 500.0, 4.0));
    QCOMPARE(s16(gen.next(0.0, 0.0)), 500);
    QCOMPARE(s16(gen.next(1.0, 1.0)), 600);
    QCOMPARE(s16(gen.next(3.0, 2.0)), 400);

    SignalGenerator sq(makeParams(SignalKind::Square, 100.0, -200.0, 4.0));
    QCOMPARE(s16(sq.next(0.5, 0.0)), -100);
    QCOMPARE(s16(sq.next(2.5, 2.0)), -300);
}

void TstSignalGen::clamping()
{
    SignalGenerator up(makeParams(SignalKind::Sine, 40000.0, 0.0, 4.0));
    QCOMPARE(s16(up.next(1.0, 0.0)), 32767);
    SignalGenerator down(makeParams(SignalKind::Sine, 40000.0, 0.0, 4.0));
    QCOMPARE(s16(down.next(3.0, 0.0)), -32768);

    SignalGenerator square(makeParams(SignalKind::Square, 40000.0, 0.0, 4.0));
    QCOMPARE(s16(square.next(0.5, 0.0)), 32767);
    QCOMPARE(s16(square.next(2.5, 0.0)), -32768);
}

void TstSignalGen::twosComplement()
{
    SignalGenerator gen(makeParams(SignalKind::Constant, 0.0, -1.0, 5.0));
    QCOMPARE(gen.next(0.0, 0.0), quint16(0xFFFF));

    SignalGenerator min(makeParams(SignalKind::Constant, 0.0, -1000.0, 5.0));
    QCOMPARE(min.next(0.0, 0.0), quint16(0xFC18));
}

void TstSignalGen::counter()
{
    SignalGenerator gen(makeParams(SignalKind::Counter, 0.0, 100.0, 5.0));
    QCOMPARE(gen.next(0.0, 0.0), quint16(100));
    QCOMPARE(gen.next(0.1, 0.1), quint16(101));
    QCOMPARE(gen.next(0.2, 0.1), quint16(102));
}

void TstSignalGen::counterWraps()
{
    SignalGenerator gen(makeParams(SignalKind::Counter, 0.0, 65535.0, 5.0));
    QCOMPARE(gen.next(0.0, 0.0), quint16(65535));
    QCOMPARE(gen.next(0.1, 0.1), quint16(0));
    QCOMPARE(gen.next(0.2, 0.1), quint16(1));
}

void TstSignalGen::noiseBounds()
{
    SignalGenerator gen(makeParams(SignalKind::Noise, 500.0, 0.0, 5.0), 42);
    for (int i = 0; i < 1000; ++i) {
        const int v = s16(gen.next(i * 0.1, 0.1));
        QVERIFY(v >= -500 && v <= 500);
    }
}

void TstSignalGen::noiseSeeds()
{
    const auto p = makeParams(SignalKind::Noise, 500.0, 0.0, 5.0);
    SignalGenerator a(p, 7);
    SignalGenerator b(p, 7);
    SignalGenerator c(p, 8);

    bool anyDifferent = false;
    for (int i = 0; i < 100; ++i) {
        const quint16 va = a.next(i * 0.1, 0.1);
        const quint16 vb = b.next(i * 0.1, 0.1);
        const quint16 vc = c.next(i * 0.1, 0.1);
        QCOMPARE(va, vb);
        if (va != vc)
            anyDifferent = true;
    }
    QVERIFY(anyDifferent);
}

void TstSignalGen::lagZeroDt()
{
    SignalParams p = makeParams(SignalKind::Lag, 0.0, 0.0, 5.0);
    p.tauS = 2.0;
    SignalGenerator gen(p);
    QCOMPARE(s16(gen.next(0.0, 0.0, 1000)), 0);  // first call, dt = 0: keeps the offset
    QCOMPARE(s16(gen.next(0.0, 0.0, 1000)), 0);  // dt = 0 again: unchanged
}

void TstSignalGen::lagTimeConstant()
{
    SignalParams p = makeParams(SignalKind::Lag, 0.0, 0.0, 5.0);
    p.tauS = 2.0;
    SignalGenerator gen(p);
    gen.next(0.0, 0.0, 1000);
    // One time constant: 1000 * (1 - exp(-1)) = 632.12
    QCOMPARE(s16(gen.next(2.0, 2.0, 1000)), 632);
    QVERIFY(qAbs(s16(gen.next(2.0, 0.0, 1000)) - 632) <= 1);
}

void TstSignalGen::lagConverges()
{
    SignalParams p = makeParams(SignalKind::Lag, 0.0, 0.0, 5.0);
    p.tauS = 2.0;
    SignalGenerator gen(p);
    gen.next(0.0, 0.0, 1000);
    int v = 0;
    double t = 0.0;
    for (int i = 0; i < 200; ++i) {
        t += 0.5;
        v = s16(gen.next(t, 0.5, 1000));
    }
    QCOMPARE(v, 1000);
}

void TstSignalGen::lagNegativeInput()
{
    SignalParams p = makeParams(SignalKind::Lag, 0.0, 0.0, 5.0);
    p.tauS = 2.0;
    SignalGenerator gen(p);
    gen.next(0.0, 0.0, quint16(0xFC18));  // -1000 as int16
    int v = 0;
    double t = 0.0;
    for (int i = 0; i < 200; ++i) {
        t += 0.5;
        v = s16(gen.next(t, 0.5, quint16(0xFC18)));
    }
    QCOMPARE(v, -1000);
}

namespace {

struct Words
{
    quint16 first;
    quint16 second;
};

/// Encodes one value. The second register starts as 0xAAAA so that a write to it (or the
/// lack of one) is visible in the result.
Words encoded(double value, mbsim::RegisterFormat format, mbsim::WordOrder order)
{
    quint16 out[2] = {0xAAAA, 0xAAAA};
    mbsim::encodeRegisters(value, format, order, out);
    return {out[0], out[1]};
}

} // namespace

void TstSignalGen::nextValueMatchesNext_data()
{
    QTest::addColumn<int>("kind");
    QTest::newRow("constant") << static_cast<int>(SignalKind::Constant);
    QTest::newRow("sine") << static_cast<int>(SignalKind::Sine);
    QTest::newRow("triangle") << static_cast<int>(SignalKind::Triangle);
    QTest::newRow("sawtooth") << static_cast<int>(SignalKind::Sawtooth);
    QTest::newRow("square") << static_cast<int>(SignalKind::Square);
    QTest::newRow("counter") << static_cast<int>(SignalKind::Counter);
    QTest::newRow("noise") << static_cast<int>(SignalKind::Noise);
    QTest::newRow("lag") << static_cast<int>(SignalKind::Lag);
}

void TstSignalGen::nextValueMatchesNext()
{
    QFETCH(int, kind);
    const auto sk = static_cast<SignalKind>(kind);
    SignalParams p = makeParams(sk, 1000.0, 50.0, 4.0);
    p.tauS = 2.0;
    p.sourceAddress = 100;

    // Same seed and same inputs: next() must be the int16 encoding of nextValue(),
    // except for Counter, whose next() is the raw counter.
    SignalGenerator viaValue(p, 9);
    SignalGenerator viaNext(p, 9);
    for (int i = 0; i < 200; ++i) {
        const double t = i * 0.1;
        const double dt = i == 0 ? 0.0 : 0.1;
        const auto input = quint16(1000 + i);
        const double value = viaValue.nextValue(t, dt, input);
        const quint16 raw = viaNext.next(t, dt, input);
        if (sk == SignalKind::Counter)
            QCOMPARE(raw, static_cast<quint16>(value));
        else
            QCOMPARE(raw, encoded(value, mbsim::RegisterFormat::Int16, mbsim::WordOrder::ABCD).first);
    }
}

void TstSignalGen::counterNextValueRaw()
{
    SignalGenerator gen(makeParams(SignalKind::Counter, 0.0, 40000.0, 5.0));
    QCOMPARE(gen.nextValue(0.0, 0.0), 40000.0); // not clamped to int16
    QCOMPARE(gen.nextValue(0.1, 0.1), 40001.0);

    SignalGenerator wrap(makeParams(SignalKind::Counter, 0.0, 65535.0, 5.0));
    QCOMPARE(wrap.nextValue(0.0, 0.0), 65535.0);
    QCOMPARE(wrap.nextValue(0.1, 0.1), 0.0);
}

void TstSignalGen::nonFiniteAmplitude()
{
    // An infinite amplitude gives NaN at phase 0; it must come out as 0, not as undefined behaviour.
    SignalGenerator gen(makeParams(SignalKind::Sine, std::numeric_limits<double>::infinity(), 0.0, 4.0));
    QCOMPARE(gen.next(0.0, 0.0), quint16(0));

    const auto nan = std::numeric_limits<double>::quiet_NaN();
    QCOMPARE(encoded(nan, mbsim::RegisterFormat::Int16, mbsim::WordOrder::ABCD).first, quint16(0));
    QCOMPARE(encoded(nan, mbsim::RegisterFormat::Int32, mbsim::WordOrder::ABCD).first, quint16(0));
    QCOMPARE(encoded(nan, mbsim::RegisterFormat::Int32, mbsim::WordOrder::ABCD).second, quint16(0));
}

void TstSignalGen::encodeInt16()
{
    using mbsim::RegisterFormat;
    using mbsim::WordOrder;
    QCOMPARE(encoded(-1000.4, RegisterFormat::Int16, WordOrder::ABCD).first, quint16(0xFC18));
    QCOMPARE(encoded(-1.0, RegisterFormat::Int16, WordOrder::ABCD).first, quint16(0xFFFF));
    QCOMPARE(encoded(2.5, RegisterFormat::Int16, WordOrder::ABCD).first, quint16(3)); // half away from zero
    QCOMPARE(encoded(40000.0, RegisterFormat::Int16, WordOrder::ABCD).first, quint16(0x7FFF));
    QCOMPARE(encoded(-40000.0, RegisterFormat::Int16, WordOrder::ABCD).first, quint16(0x8000));
    // The int16 format writes one register only, whatever the byte order.
    QCOMPARE(encoded(1.0, RegisterFormat::Int16, WordOrder::CDAB).second, quint16(0xAAAA));
}

void TstSignalGen::encodeInt32()
{
    using mbsim::RegisterFormat;
    using mbsim::WordOrder;
    const auto int32 = RegisterFormat::Int32;

    // -1 is 0xFFFFFFFF, the same in every order.
    for (const WordOrder order : {WordOrder::ABCD, WordOrder::CDAB, WordOrder::BADC, WordOrder::DCBA}) {
        const Words w = encoded(-1.0, int32, order);
        QCOMPARE(w.first, quint16(0xFFFF));
        QCOMPARE(w.second, quint16(0xFFFF));
    }

    // 123456789 = 0x075BCD15
    const Words abcd = encoded(123456789.0, int32, WordOrder::ABCD);
    QCOMPARE(abcd.first, quint16(0x075B));
    QCOMPARE(abcd.second, quint16(0xCD15));
    const Words cdab = encoded(123456789.0, int32, WordOrder::CDAB);
    QCOMPARE(cdab.first, quint16(0xCD15));
    QCOMPARE(cdab.second, quint16(0x075B));
    const Words badc = encoded(123456789.0, int32, WordOrder::BADC);
    QCOMPARE(badc.first, quint16(0x5B07));
    QCOMPARE(badc.second, quint16(0x15CD));
    const Words dcba = encoded(123456789.0, int32, WordOrder::DCBA);
    QCOMPARE(dcba.first, quint16(0x15CD));
    QCOMPARE(dcba.second, quint16(0x5B07));

    // Clamping to the int32 range.
    const Words high = encoded(1e12, int32, WordOrder::ABCD);
    QCOMPARE(high.first, quint16(0x7FFF));
    QCOMPARE(high.second, quint16(0xFFFF));
    const Words low = encoded(-1e12, int32, WordOrder::ABCD);
    QCOMPARE(low.first, quint16(0x8000));
    QCOMPARE(low.second, quint16(0x0000));

    // Rounds to nearest, half away from zero.
    const Words rounded = encoded(2.5, int32, WordOrder::ABCD);
    QCOMPARE(rounded.first, quint16(0x0000));
    QCOMPARE(rounded.second, quint16(0x0003));
}

void TstSignalGen::encodeFloat32()
{
    using mbsim::RegisterFormat;
    using mbsim::WordOrder;
    const auto float32 = RegisterFormat::Float32;

    // 123.456 as IEEE-754 single precision is 0x42F6E979.
    const Words abcd = encoded(123.456, float32, WordOrder::ABCD);
    QCOMPARE(abcd.first, quint16(0x42F6));
    QCOMPARE(abcd.second, quint16(0xE979));
    const Words cdab = encoded(123.456, float32, WordOrder::CDAB);
    QCOMPARE(cdab.first, quint16(0xE979));
    QCOMPARE(cdab.second, quint16(0x42F6));
    const Words badc = encoded(123.456, float32, WordOrder::BADC);
    QCOMPARE(badc.first, quint16(0xF642));
    QCOMPARE(badc.second, quint16(0x79E9));
    const Words dcba = encoded(123.456, float32, WordOrder::DCBA);
    QCOMPARE(dcba.first, quint16(0x79E9));
    QCOMPARE(dcba.second, quint16(0xF642));

    // Floats are not rounded or clamped: 1.0 is 0x3F800000, and 0.5 keeps its fraction.
    QCOMPARE(encoded(1.0, float32, WordOrder::ABCD).first, quint16(0x3F80));
    QCOMPARE(encoded(1.0, float32, WordOrder::ABCD).second, quint16(0x0000));
    QCOMPARE(encoded(0.5, float32, WordOrder::ABCD).first, quint16(0x3F00));
}

QTEST_GUILESS_MAIN(TstSignalGen)
#include "tst_signalgen.moc"
