#include <QtTest>

#include "SignalGen.h"

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

QTEST_GUILESS_MAIN(TstSignalGen)
#include "tst_signalgen.moc"
