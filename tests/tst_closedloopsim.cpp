#include "ClosedLoopSim.h"

#include <QtTest>

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>

using namespace mb;

namespace {

SimulationSetup baseSetup()
{
    SimulationSetup s;
    s.model = {2.0, 0.0, 10.0};
    s.pid.kp = 1.0;
    s.pv0 = 50.0;
    s.co0 = 50.0;
    s.spStep = 10.0;
    s.coMin = 0.0;
    s.coMax = 100.0;
    s.durationS = 300.0;
    s.dtS = 0.1;
    return s;
}

// Overshoot in % of the step (>= 0) and settling time (2 % band of the step; -1 = not settled).
double overshootPct(const ClosedLoopSeries &r, double step)
{
    double over = 0.0;
    for (qsizetype i = 0; i < r.pv.size(); ++i)
        over = std::max(over, (r.pv[i] - r.sp.last()) * (step >= 0.0 ? 1.0 : -1.0));
    return over / std::abs(step) * 100.0;
}

double settlingTime(const ClosedLoopSeries &r, double step, double bandPct = 2.0)
{
    const double band = bandPct / 100.0 * std::abs(step);
    qsizetype last = -1;
    for (qsizetype i = 0; i < r.pv.size(); ++i) {
        if (std::abs(r.pv[i] - r.sp[i]) > band)
            last = i;
    }
    if (last == r.pv.size() - 1)
        return -1.0;
    return r.t[last + 1];
}

} // namespace

class TstClosedLoopSim : public QObject
{
    Q_OBJECT
private slots:
    void openLoopSanity();
    void pOnlyOffset();
    void piReachesSetpoint();
    void piAntiWindup();
    void deadTime();
    void disturbance();
    void derivativeAction();
    void directAction();
    void invalidInputs_data();
    void invalidInputs();
    void sampleCountAndTimes();
    void deterministicAndFast();
    void zieglerNicholsVersusImc();
};

void TstClosedLoopSim::openLoopSanity()
{
    SimulationSetup s = baseSetup();
    s.pid.kp = 0.0;
    s.pid.ti = 20.0;
    s.pid.td = 3.0;
    const ClosedLoopSeries r = simulateClosedLoop(s);
    QVERIFY(!r.t.isEmpty());
    QCOMPARE(r.pv.size(), r.t.size());
    QCOMPARE(r.co.size(), r.t.size());
    QCOMPARE(r.sp.size(), r.t.size());
    for (qsizetype i = 0; i < r.t.size(); ++i) {
        QCOMPARE(r.pv[i], 50.0);
        QCOMPARE(r.co[i], 50.0);
    }
    QCOMPARE(r.sp[0], 50.0);
    QCOMPARE(r.sp[1], 60.0);
    QCOMPARE(r.sp.last(), 60.0);
}

void TstClosedLoopSim::pOnlyOffset()
{
    SimulationSetup s = baseSetup();
    s.pid.kp = 1.5;
    s.model = {2.0, 0.0, 10.0};
    s.durationS = 100.0; // 10 T
    const ClosedLoopSeries r = simulateClosedLoop(s);
    const double expected = s.spStep / (1.0 + 2.0 * 1.5);
    QVERIFY(std::abs((r.sp.last() - r.pv.last()) - expected) < 1e-3);
    // The output settled at co0 + Kp e.
    QVERIFY(std::abs(r.co.last() - (50.0 + 1.5 * expected)) < 1e-3);
}

void TstClosedLoopSim::piReachesSetpoint()
{
    SimulationSetup s = baseSetup();
    s.pid.kp = 1.0;
    s.pid.ti = 15.0;
    s.durationS = 600.0;
    const ClosedLoopSeries r = simulateClosedLoop(s);
    QVERIFY(std::abs(r.pv.last() - 60.0) < 1e-3);
    // The needed output is co0 + step / K.
    QVERIFY(std::abs(r.co.last() - 55.0) < 1e-3);
    for (double u : r.co) {
        QVERIFY(u >= s.coMin);
        QVERIFY(u <= s.coMax);
    }
}

void TstClosedLoopSim::piAntiWindup()
{
    SimulationSetup s = baseSetup();
    s.model = {2.0, 2.0, 20.0};
    s.pid.kp = 4.0; // aggressive: the output saturates at the step
    s.pid.ti = 20.0;
    s.durationS = 600.0;
    const ClosedLoopSeries free = simulateClosedLoop(s);
    QVERIFY(std::abs(free.pv.last() - 60.0) < 1e-3);

    s.coMax = 56.0; // needed steady value is 55
    const ClosedLoopSeries tight = simulateClosedLoop(s);
    QVERIFY(std::abs(tight.pv.last() - 60.0) < 1e-3);
    double maxCo = 0.0;
    for (double u : tight.co)
        maxCo = std::max(maxCo, u);
    QVERIFY(maxCo <= 56.0 + 1e-12);
    QCOMPARE(maxCo, 56.0); // it did saturate
    // No windup: the constrained overshoot is not much larger than the unconstrained one.
    QVERIFY2(overshootPct(tight, s.spStep) < overshootPct(free, s.spStep) + 5.0,
             qPrintable(QStringLiteral("tight %1 %, free %2 %")
                            .arg(overshootPct(tight, s.spStep))
                            .arg(overshootPct(free, s.spStep))));
    QVERIFY(overshootPct(tight, s.spStep) < 20.0);
}

void TstClosedLoopSim::deadTime()
{
    SimulationSetup s = baseSetup();
    s.model = {2.0, 5.0, 10.0};
    s.pid.kp = 1.0;
    s.pid.ti = 20.0;
    const ClosedLoopSeries r = simulateClosedLoop(s);
    qsizetype first = -1;
    for (qsizetype i = 0; i < r.pv.size(); ++i) {
        if (r.pv[i] != s.pv0) {
            first = i;
            break;
        }
    }
    QVERIFY(first > 0);
    QVERIFY2(r.t[first] > s.model.l - s.dtS, qPrintable(QString::number(r.t[first])));
    QVERIFY(r.t[first] < s.model.l + 2.0 * s.dtS);
    // The controller reacted at once.
    QVERIFY(r.co[1] > s.co0);
}

void TstClosedLoopSim::disturbance()
{
    SimulationSetup s = baseSetup();
    s.test = ClosedLoopTest::Disturbance;
    s.spStep = 25.0; // ignored for a disturbance
    s.loadStep = 5.0;
    s.model = {2.0, 1.0, 10.0};
    s.pid.kp = 0.5;
    s.pid.ti = 20.0;
    s.durationS = 800.0;
    const ClosedLoopSeries r = simulateClosedLoop(s);
    for (double sp : r.sp)
        QCOMPARE(sp, 50.0);
    double peak = 0.0;
    for (double pv : r.pv)
        peak = std::max(peak, pv - 50.0);
    QVERIFY(peak > 1.0); // K * load > 0 pushes the PV up
    QVERIFY(r.pv.first() == 50.0);
    QVERIFY(std::abs(r.pv.last() - 50.0) < 1e-3);
    QVERIFY(std::abs(r.co.last() - (50.0 - 5.0)) < 1e-3); // the controller cancels the load

    // Negative gain: the deviation has the sign of K * load.
    s.model.k = -2.0;
    s.pid.kp = -0.5;
    const ClosedLoopSeries n = simulateClosedLoop(s);
    double low = 0.0;
    for (double pv : n.pv)
        low = std::min(low, pv - 50.0);
    QVERIFY(low < -1.0);
    QVERIFY(std::abs(n.pv.last() - 50.0) < 1e-3);

    // P only: the offset is K load / (1 + K Kp).
    s.model.k = 2.0;
    s.pid.kp = 0.5;
    s.pid.ti.reset();
    const ClosedLoopSeries p = simulateClosedLoop(s);
    QVERIFY(std::abs((p.pv.last() - 50.0) - 2.0 * 5.0 / (1.0 + 2.0 * 0.5)) < 1e-3);
}

void TstClosedLoopSim::derivativeAction()
{
    SimulationSetup s = baseSetup();
    s.model = {2.0, 1.0, 10.0};
    s.pid.kp = 1.0;
    s.pid.ti = 20.0;
    const ClosedLoopSeries noD = simulateClosedLoop(s);
    s.pid.td = 5.0;
    const ClosedLoopSeries withD = simulateClosedLoop(s);
    // Derivative on the PV: at the SP step the output is the proportional kick only.
    QCOMPARE(withD.co[1], noD.co[1]);
    QVERIFY(withD.co[1] <= s.co0 + s.pid.kp * s.spStep + s.pid.kp / 20.0 * s.spStep * s.dtS + 1e-9);
    double maxDiff = 0.0;
    for (qsizetype i = 0; i < noD.pv.size(); ++i)
        maxDiff = std::max(maxDiff, std::abs(noD.pv[i] - withD.pv[i]));
    QVERIFY(maxDiff > 1e-3);
    // The derivative brakes the output once the PV moves: it is below the one without D shortly after.
    QVERIFY(withD.co[20] < noD.co[20]);
}

void TstClosedLoopSim::directAction()
{
    SimulationSetup s = baseSetup();
    s.model = {-2.0, 2.0, 20.0};
    s.pid.kp = -1.0;
    s.pid.ti = 20.0;
    s.durationS = 600.0;
    const ClosedLoopSeries r = simulateClosedLoop(s);
    QVERIFY(std::abs(r.pv.last() - 60.0) < 1e-3);
    QVERIFY(std::abs(r.co.last() - 45.0) < 1e-3); // 50 + 10 / K
    for (double pv : r.pv)
        QVERIFY(std::abs(pv - 50.0) < 3.0 * 10.0);
}

void TstClosedLoopSim::invalidInputs_data()
{
    QTest::addColumn<int>("what");
    QTest::newRow("T zero") << 0;
    QTest::newRow("T negative") << 1;
    QTest::newRow("dt zero") << 2;
    QTest::newRow("dt negative") << 3;
    QTest::newRow("L negative") << 4;
    QTest::newRow("duration zero") << 5;
    QTest::newRow("Ti negative") << 6;
    QTest::newRow("Td negative") << 7;
    QTest::newRow("limits equal") << 8;
    QTest::newRow("limits reversed") << 9;
    QTest::newRow("dt above duration") << 10;
}

void TstClosedLoopSim::invalidInputs()
{
    QFETCH(int, what);
    SimulationSetup s = baseSetup();
    switch (what) {
    case 0: s.model.t = 0.0; break;
    case 1: s.model.t = -1.0; break;
    case 2: s.dtS = 0.0; break;
    case 3: s.dtS = -0.1; break;
    case 4: s.model.l = -1.0; break;
    case 5: s.durationS = 0.0; break;
    case 6: s.pid.ti = -1.0; break;
    case 7: s.pid.td = -1.0; break;
    case 8: s.coMin = s.coMax; break;
    case 9: s.coMin = 100.0; s.coMax = 0.0; break;
    case 10: s.durationS = 1.0; s.dtS = 2.0; break;
    }
    const ClosedLoopSeries r = simulateClosedLoop(s);
    QVERIFY(r.t.isEmpty());
    QVERIFY(r.pv.isEmpty());
    QVERIFY(r.sp.isEmpty());
    QVERIFY(r.co.isEmpty());
}

void TstClosedLoopSim::sampleCountAndTimes()
{
    SimulationSetup s = baseSetup();
    s.durationS = 60.0;
    s.dtS = 0.1;
    ClosedLoopSeries r = simulateClosedLoop(s);
    QCOMPARE(r.t.size(), 601);
    for (qsizetype i = 0; i < r.t.size(); ++i)
        QVERIFY(std::abs(r.t[i] - static_cast<double>(i) * 0.1) < 1e-9);
    QCOMPARE(r.pv.size(), 601);
    QCOMPARE(r.sp.size(), 601);
    QCOMPARE(r.co.size(), 601);
    QCOMPARE(r.pv[0], s.pv0);
    QCOMPARE(r.co[0], s.co0);

    s.durationS = 10.0;
    s.dtS = 10.0; // dt == duration: two samples
    r = simulateClosedLoop(s);
    QCOMPARE(r.t.size(), 2);
    QCOMPARE(r.t[1], 10.0);
}

void TstClosedLoopSim::deterministicAndFast()
{
    SimulationSetup s = baseSetup();
    s.model = {2.2, 4.0, 32.5};
    s.pid.kp = 1.86;
    s.pid.ti = 32.5;
    s.pid.td = 2.0;
    s.durationS = 600.0;
    const ClosedLoopSeries a = simulateClosedLoop(s);
    QElapsedTimer timer;
    timer.start();
    const ClosedLoopSeries b = simulateClosedLoop(s);
    const qint64 ms = timer.elapsed();
    QVERIFY(a.pv == b.pv);
    QVERIFY(a.co == b.co);
    QVERIFY2(ms < 100, qPrintable(QStringLiteral("%1 ms").arg(ms))); // debug build; < 10 ms in release
}

void TstClosedLoopSim::zieglerNicholsVersusImc()
{
    SimulationSetup s = baseSetup();
    s.model = {2.2, 4.0, 32.5}; // the default level loop identified with Smith's method
    s.spStep = 10.0;
    s.durationS = 300.0;

    s.pid.kp = 1.86;
    s.pid.ti = 32.5;
    const ClosedLoopSeries imc = simulateClosedLoop(s);
    s.pid.kp = 3.35;
    s.pid.ti = 13.2;
    const ClosedLoopSeries zn = simulateClosedLoop(s);

    const double imcOver = overshootPct(imc, s.spStep);
    const double znOver = overshootPct(zn, s.spStep);
    const double imcSettle = settlingTime(imc, s.spStep);
    const double znSettle = settlingTime(zn, s.spStep);
    qInfo().noquote() << QStringLiteral("IMC PI: overshoot %1 %, settling %2 s; ZN PI: overshoot %3 %, settling %4 s")
                             .arg(imcOver, 0, 'f', 1)
                             .arg(imcSettle, 0, 'f', 1)
                             .arg(znOver, 0, 'f', 1)
                             .arg(znSettle, 0, 'f', 1);
    QVERIFY(znOver > imcOver + 10.0);
    QVERIFY(imcSettle > 0.0);
    QVERIFY(znSettle > 0.0);
    QVERIFY(std::abs(imc.pv.last() - 60.0) < 0.1);
    QVERIFY(std::abs(zn.pv.last() - 60.0) < 0.1);
}

QTEST_GUILESS_MAIN(TstClosedLoopSim)
#include "tst_closedloopsim.moc"
