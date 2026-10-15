#include "LoopModel.h"

#include <QtTest>

#include <cmath>
#include <limits>
#include <vector>

using namespace mbloop;
using namespace mbloop::loop101;

namespace {

/// Parameters with all the dynamics removed (exact, noise-free) unless a test turns them on.
LoopParams plain()
{
    LoopParams p;
    p.deadTimeS = 0.0;
    p.ltNoisePct = 0.0;
    p.ltDampingS = 0.0;
    p.fcvStrokeS = 0.0;
    return p;
}

} // namespace

class TstLoopModel : public QObject
{
    Q_OBJECT
private slots:
    void steadyLevel()
    {
        LoopParams p;
        QVERIFY(qAbs(LoopModel::steadyLevelM(p, 50) - 0.50) < 1e-3);
        QVERIFY(qAbs(LoopModel::steadyLevelM(p, 30) - 0.18) < 1e-3);
        QVERIFY(qAbs(LoopModel::steadyLevelM(p, 70) - 0.98) < 1e-3);
        p.pumpSpeedPct = 50.0; // half the flow -> a quarter of the level
        QVERIFY(qAbs(LoopModel::steadyLevelM(p, 50) - 0.125) < 1e-3);
        p = LoopParams{};
        p.hvOpeningPct = 0.0;
        QVERIFY(std::isinf(LoopModel::steadyLevelM(p, 50)));
        QCOMPARE(LoopModel::steadyLevelM(LoopParams{}, 0.0), 0.0);
        LoopParams q;
        q.pumpSpeedPct = 0.0;
        QCOMPARE(LoopModel::steadyLevelM(q, 50.0), 0.0);
    }

    void resetIsSteady()
    {
        LoopParams p = plain();
        LoopModel m;
        m.reset(p, 50.0);
        QVERIFY(qAbs(m.state().levelM - 0.5) < 1e-3);
        QVERIFY(qAbs(m.state().pvPct - 50.0) < 0.1);
        const double h0 = m.state().levelM;
        m.advance(600.0);
        QVERIFY(qAbs(m.state().levelM - h0) < 1e-4);
        QVERIFY(qAbs(m.state().pvPct - 50.0) < 0.1);
        QVERIFY(qAbs(m.state().timeS - 600.0) < 1e-6);
        QVERIFY(!m.state().overflowing);
    }

    void stepResponseTau()
    {
        LoopParams p = plain();
        LoopModel m;
        m.reset(p, 50.0);
        const double h0 = m.state().levelM;
        const double h1 = LoopModel::steadyLevelM(p, 60.0);
        // Linearised time constant tau = 2 A h / Qhv, taken at the mean of the two operating points.
        const double hMean = 0.5 * (h0 + h1);
        const double qMean = kHvCoefficient * 0.5 * std::sqrt(hMean);
        const double tau = 2.0 * kTankAreaM2 * hMean / qMean;

        m.setOut(60.0);
        const double target = h0 + 0.632 * (h1 - h0);
        double t632 = -1.0;
        for (int i = 0; i < 100000 && t632 < 0.0; ++i) {
            m.advance(0.1);
            if (m.state().levelM >= target)
                t632 = m.state().timeS;
        }
        qInfo() << "t63.2 =" << t632 << "expected tau =" << tau;
        QVERIFY(t632 > 0.0);
        QVERIFY(qAbs(t632 - tau) / tau < 0.10);

        m.advance(20.0 * tau);
        QVERIFY(qAbs(m.state().levelM - h1) < 5e-4);
    }

    void smallStepMatchesLinearTau()
    {
        // For a small step the response is the exponential with the operating-point tau.
        LoopParams p = plain();
        LoopModel m;
        m.reset(p, 50.0);
        const double h0 = m.state().levelM;
        const double h1 = LoopModel::steadyLevelM(p, 51.0);
        const double tau = 2.0 * kTankAreaM2 * h0 / (kHvCoefficient * 0.5 * std::sqrt(h0));
        m.setOut(51.0);
        const double target = h0 + 0.632 * (h1 - h0);
        double t632 = -1.0;
        for (int i = 0; i < 100000 && t632 < 0.0; ++i) {
            m.advance(0.1);
            if (m.state().levelM >= target)
                t632 = m.state().timeS;
        }
        QVERIFY(t632 > 0.0);
        QVERIFY(qAbs(t632 - tau) / tau < 0.03);
    }

    void deadTime()
    {
        LoopParams p = plain();
        p.deadTimeS = 5.0;
        LoopModel m;
        m.reset(p, 50.0);
        const double h0 = m.state().levelM;
        m.setOut(70.0);
        m.advance(4.9);
        QVERIFY(qAbs(m.state().levelM - h0) < 1e-9);
        QVERIFY(m.state().qFcv > m.state().qIn);
        m.advance(0.3);
        QVERIFY(m.state().levelM > h0 + 1e-6);
    }

    void deadTimeChangeKeepsHistory()
    {
        LoopParams p = plain();
        p.deadTimeS = 5.0;
        LoopModel m;
        m.reset(p, 50.0);
        m.setOut(70.0);
        m.advance(2.0);
        p.deadTimeS = 10.0; // longer: padded with the oldest value
        m.setParams(p);
        const double qOld = m.state().qIn;
        m.advance(0.5);
        QVERIFY(qAbs(m.state().qIn - qOld) < 1e-12);
        p.deadTimeS = 0.0;
        m.setParams(p);
        m.advance(0.01);
        QVERIFY(qAbs(m.state().qIn - m.state().qFcv) < 1e-15);
    }

    void stroke()
    {
        LoopParams p = plain();
        p.fcvStrokeS = 5.0;
        LoopModel m;
        m.reset(p, 0.0);
        m.setOut(100.0);
        m.advance(2.5);
        QVERIFY(qAbs(m.state().stemPct - 50.0) < 0.5);
        m.advance(2.5);
        QVERIFY(qAbs(m.state().stemPct - 100.0) < 0.5);
        m.advance(1.0);
        QCOMPARE(m.state().stemPct, 100.0);
        m.setOut(0.0);
        m.advance(5.1);
        QCOMPARE(m.state().stemPct, 0.0);
    }

    void overflow()
    {
        LoopParams p = plain();
        LoopModel m;
        m.reset(p, 100.0); // steady level 1.96 m -> clamped to the overflow
        QVERIFY(qAbs(m.state().levelM - kOverflowLevelM) < 1e-12);
        m.advance(0.05);
        QVERIFY(m.state().overflowing);
        m.advance(100.0);
        QVERIFY(qAbs(m.state().levelM - kOverflowLevelM) < 1e-9);
        QVERIFY(m.state().overflowing);
        const double expected = m.state().qIn - m.state().qHv;
        QVERIFY(qAbs(m.state().qOverflow - expected) < 1e-9);
        QVERIFY(m.state().qOverflow > 1e-4);

        // Drops back below the overflow when OUT is reduced.
        m.setOut(40.0);
        m.advance(300.0);
        QVERIFY(!m.state().overflowing);
        QVERIFY(m.state().levelM < kOverflowLevelM);
        QCOMPARE(m.state().qOverflow, 0.0);
    }

    void ltDamping()
    {
        LoopParams p = plain();
        p.ltDampingS = 5.0;
        LoopModel m;
        m.reset(p, 50.0);
        m.setOut(70.0);
        m.advance(20.0);
        const double truePct = m.state().levelM * 100.0;
        QVERIFY(truePct > 50.0);
        QVERIFY(m.state().pvPct < truePct);
        QVERIFY(m.state().pvPct > 50.0);
        // The lag converges to the true level in a steady state.
        LoopModel s;
        s.reset(p, 50.0);
        s.setLevelM(0.2);
        QVERIFY(qAbs(s.state().pvPct - 20.0) < 1e-9); // setLevelM sets the lag too
        s.setOut(0.0);
        s.setStemPct(0.0);
        s.setLevelM(0.0);
        QCOMPARE(s.state().pvPct, 0.0);
    }

    void ltNoise()
    {
        LoopParams p = plain();
        p.ltNoisePct = 0.2;
        LoopModel m(7);
        m.reset(p, 50.0);
        std::vector<double> pv;
        bool nonzero = false;
        double prev = m.state().pvPct;
        int changes = 0;
        for (int i = 0; i < 1000; ++i) {
            m.advance(kStepS);
            const double pct = m.state().levelM * 100.0;
            QVERIFY(qAbs(m.state().pvPct - pct) <= 0.2 + 1e-9);
            if (qAbs(m.state().pvPct - pct) > 1e-6)
                nonzero = true;
            if (m.state().pvPct != prev)
                ++changes;
            prev = m.state().pvPct;
        }
        QVERIFY(nonzero);
        QVERIFY(changes <= 101); // new value only every 100 ms of simulation time
        QVERIFY(changes >= 50);

        // Constant within each 100 ms window.
        LoopModel w(7);
        w.reset(p, 50.0);
        for (int k = 0; k < 20; ++k) {
            w.advance(kStepS);
            const double first = w.state().pvPct;
            for (int i = 1; i < 10; ++i) {
                w.advance(kStepS);
                if (i < 9)
                    QCOMPARE(w.state().pvPct, first);
            }
        }
    }

    void noiseSeed()
    {
        LoopParams p = plain();
        p.ltNoisePct = 1.0;
        LoopModel a(5), b(5), c(6);
        a.reset(p, 50.0);
        b.reset(p, 50.0);
        c.reset(p, 50.0);
        bool diff = false;
        for (int i = 0; i < 300; ++i) {
            a.advance(0.1);
            b.advance(0.1);
            c.advance(0.1);
            QCOMPARE(a.state().pvPct, b.state().pvPct);
            if (a.state().pvPct != c.state().pvPct)
                diff = true;
        }
        QVERIFY(diff);
    }

    void advanceCarry()
    {
        LoopParams p;
        p.ltNoisePct = 0.5;
        LoopModel a(3), b(3);
        a.reset(p, 40.0);
        b.reset(p, 40.0);
        a.setOut(60.0);
        b.setOut(60.0);
        a.advance(1.0);
        for (int i = 0; i < 100; ++i)
            b.advance(0.01);
        QCOMPARE(a.state().levelM, b.state().levelM);
        QCOMPARE(a.state().pvPct, b.state().pvPct);
        QCOMPARE(a.state().stemPct, b.state().stemPct);
        QCOMPARE(a.state().qIn, b.state().qIn);
        QCOMPARE(a.state().timeS, b.state().timeS);

        // Sub-step calls accumulate.
        LoopModel c(3), d(3);
        c.reset(p, 40.0);
        d.reset(p, 40.0);
        for (int i = 0; i < 10; ++i)
            c.advance(0.004);
        d.advance(0.04);
        QCOMPARE(c.state().timeS, d.state().timeS);
        QCOMPARE(c.state().levelM, d.state().levelM);
        c.advance(0.0);
        QCOMPARE(c.state().timeS, d.state().timeS);
    }

    void clamping()
    {
        LoopParams p;
        p.pumpSpeedPct = 150.0;
        p.hvOpeningPct = -5.0;
        p.deadTimeS = 99.0;
        p.ltNoisePct = 50.0;
        p.ltDampingS = 500.0;
        p.fcvStrokeS = 1000.0;
        LoopModel m;
        m.setParams(p);
        QCOMPARE(m.params().pumpSpeedPct, 100.0);
        QCOMPARE(m.params().hvOpeningPct, 0.0);
        QCOMPARE(m.params().deadTimeS, 30.0);
        QCOMPARE(m.params().ltNoisePct, 5.0);
        QCOMPARE(m.params().ltDampingS, 60.0);
        QCOMPARE(m.params().fcvStrokeS, 120.0);
        p.pumpSpeedPct = -1.0;
        p.deadTimeS = -2.0;
        m.setParams(p);
        QCOMPARE(m.params().pumpSpeedPct, 0.0);
        QCOMPARE(m.params().deadTimeS, 0.0);

        m.setOut(150.0);
        QCOMPARE(m.state().outPct, 100.0);
        m.setOut(-1.0);
        QCOMPARE(m.state().outPct, 0.0);
        m.setSp(120.0);
        QCOMPARE(m.state().spPct, 100.0);
        m.setSp(-3.0);
        QCOMPARE(m.state().spPct, 0.0);
        m.setStemPct(130.0);
        QCOMPARE(m.state().stemPct, 100.0);
        m.setStemPct(-1.0);
        QCOMPARE(m.state().stemPct, 0.0);
        m.setLevelM(5.0);
        QCOMPARE(m.state().levelM, kOverflowLevelM);
        m.setLevelM(-1.0);
        QCOMPARE(m.state().levelM, 0.0);
    }

    void hvClosedAndPumpOff()
    {
        // HV closed: nothing drains, the level only rises.
        LoopParams p = plain();
        p.hvOpeningPct = 0.0;
        LoopModel m;
        m.reset(p, 50.0);
        QCOMPARE(m.state().levelM, kOverflowLevelM); // steady state is infinite -> overflow level
        m.setLevelM(0.2);
        m.advance(10.0);
        QCOMPARE(m.state().qHv, 0.0);
        QVERIFY(m.state().levelM > 0.2);

        // Pump off: no inflow, the tank drains.
        LoopParams q = plain();
        q.pumpSpeedPct = 0.0;
        LoopModel n;
        n.reset(q, 50.0);
        n.setLevelM(0.5);
        n.advance(10.0);
        QCOMPARE(n.state().qFcv, 0.0);
        QCOMPARE(n.state().qIn, 0.0);
        QVERIFY(n.state().levelM < 0.5);
        n.advance(20000.0);
        QVERIFY(n.state().levelM >= 0.0);
        QVERIFY(n.state().levelM < 0.01);
    }

    void fillPipe()
    {
        LoopParams p = plain();
        p.deadTimeS = 3.0;
        LoopModel m;
        m.reset(p, 20.0);
        m.setOut(80.0);
        m.setStemPct(80.0);
        m.fillPipe();
        m.advance(0.01);
        QVERIFY(qAbs(m.state().qIn - m.state().qFcv) < 1e-15);
        QVERIFY(qAbs(m.state().qFcv - kFcvMaxFlowM3s * 0.8) < 1e-12);
    }

    // ---- LIC-101 PID (v1.3.1) -------------------------------------------------------------
    void pidSetpointStepSettles()
    {
        LoopParams p;
        p.ltNoisePct = 0.0;
        LoopModel m;
        m.reset(p, 50.0);
        m.setAuto(true);
        m.advance(60.0);
        QVERIFY(qAbs(m.state().pvPct - 50.0) < 0.1); // stays put at SP = PV
        m.setSp(60.0);
        const double t0 = m.state().timeS;
        double peak = 0.0, settledAt = -1.0;
        for (int i = 0; i < 6000; ++i) { // 600 s in 0.1 s
            m.advance(0.1);
            peak = std::max(peak, m.state().pvPct);
            const bool inBand = qAbs(m.state().pvPct - 60.0) <= 0.5;
            if (!inBand)
                settledAt = -1.0;
            else if (settledAt < 0.0)
                settledAt = m.state().timeS - t0;
        }
        qInfo("SP step 50->60: peak %.2f %%, enters +-0.5 %% band for good at %.1f s", peak, settledAt);
        QVERIFY(settledAt >= 0.0 && settledAt < 300.0);
        QVERIFY(peak < 66.0); // modest overshoot, no sustained oscillation
        QVERIFY(qAbs(m.state().pvPct - 60.0) < 0.1);
    }

    void pidProportionalOnlyOffset()
    {
        LoopParams p = plain();
        PidSettings pid;
        pid.tiS = 0.0;
        LoopModel m;
        m.reset(p, 50.0);
        m.setPid(pid);
        m.setAuto(true);
        m.advance(600.0);
        // theory: PV = 200 (OUT/100)^2, OUT = Kp (SP - PV)  ->  4x^2 + 2x - 1 = 0, x = OUT/100
        const double x = (-2.0 + std::sqrt(20.0)) / 8.0;
        const double out = 100.0 * x, pv = 200.0 * x * x;
        QVERIFY(qAbs(m.state().outPct - out) < 0.2);
        QVERIFY(qAbs(m.state().pvPct - pv) < 0.2);
        QVERIFY(qAbs((m.state().spPct - m.state().pvPct) - out) < 0.2); // permanent offset = Kp-scaled OUT
        QVERIFY(m.state().spPct - m.state().pvPct > 10.0);
    }

    void pidBumplessEntryAndKpChange()
    {
        LoopParams p;
        p.ltNoisePct = 0.0;
        LoopModel m;
        m.reset(p, 50.0);
        m.setSp(57.0); // error 7 %
        m.setOut(40.0);
        m.advance(5.0);
        const double before = m.state().outPct;
        QCOMPARE(before, 40.0);
        m.setAuto(true);
        QVERIFY(m.isAuto() && m.state().autoMode);
        QCOMPARE(m.state().outPct, before); // not touched at that instant
        m.advance(0.1);                     // first PID execution
        QVERIFY(qAbs(m.state().outPct - before) < 0.2);
        m.advance(10.0);
        const double o1 = m.state().outPct;
        PidSettings pid = m.pid();
        pid.kp = 4.0;
        m.setPid(pid);
        QCOMPARE(m.state().outPct, o1);
        m.advance(0.1);
        QVERIFY(qAbs(m.state().outPct - o1) < 0.5); // only the natural change over 0.1 s
    }

    void pidAntiWindup()
    {
        LoopParams p;
        p.ltNoisePct = 0.0;
        p.pumpSpeedPct = 50.0; // the level cannot get above about 25 %: OUT saturates
        LoopModel m;
        m.reset(p, 50.0);
        PidSettings pid;
        pid.kp = 2.0;
        m.setPid(pid);
        m.setAuto(true);
        m.setSp(100.0);
        m.advance(200.0);
        QVERIFY(m.state().outPct > 99.0); // saturated (P alone is about 150 %)
        m.setSp(10.0);
        const double t0 = m.state().timeS;
        double leftAt = -1.0;
        for (int i = 0; i < 600 && leftAt < 0.0; ++i) {
            m.advance(0.1);
            if (m.state().outPct < 90.0)
                leftAt = m.state().timeS - t0;
        }
        QVERIFY(leftAt >= 0.0);
        QVERIFY(leftAt < 1.0); // within a few PID periods
    }

    void pidDirectActionRunsAway()
    {
        LoopParams p;
        p.ltNoisePct = 0.0;
        LoopModel m;
        m.reset(p, 50.0);
        PidSettings pid;
        pid.reverse = false;
        m.setPid(pid);
        m.setAuto(true);
        m.setSp(70.0); // SP above PV: direct action closes the valve
        m.advance(60.0);
        QCOMPARE(m.state().outPct, 0.0);
        QVERIFY(m.state().pvPct < 45.0);
    }

    void pidNoDerivativeKickOnSetpoint()
    {
        PidSettings pid;
        pid.kp = 2.0;
        pid.tiS = 30.0;
        pid.tdS = 10.0;
        LoopModel m;
        m.reset(plain(), 50.0);
        m.setPid(pid);
        m.setAuto(true);
        m.advance(0.2); // SP = PV: OUT stays at 50
        const double o0 = m.state().outPct;
        m.setSp(55.0);
        m.advance(0.1); // one PID execution after the step; PV hardly moved
        QVERIFY(qAbs((m.state().outPct - o0) - 2.0 * 5.0) < 0.2); // Kp * dSP only
    }

    void pidDerivativeStillSettles()
    {
        LoopParams p;
        p.ltNoisePct = 0.0;
        PidSettings pid;
        pid.tdS = 5.0;
        LoopModel m;
        m.reset(p, 50.0);
        m.setPid(pid);
        m.setAuto(true);
        m.setSp(60.0);
        m.advance(400.0);
        QVERIFY(qAbs(m.state().pvPct - 60.0) < 0.5);
    }

    void pidSetOutIgnoredInAutoAndResetReturnsToManual()
    {
        LoopModel m;
        m.reset(plain(), 50.0);
        PidSettings pid;
        pid.kp = 3.0;
        pid.tiS = 12.0;
        pid.reverse = false;
        m.setPid(pid);
        m.setAuto(true);
        m.setOut(10.0);
        QCOMPARE(m.state().outPct, 50.0);
        m.reset(plain(), 50.0);
        QVERIFY(!m.isAuto() && !m.state().autoMode);
        QVERIFY(m.pid() == pid); // settings are kept
        m.setOut(10.0);
        QCOMPARE(m.state().outPct, 10.0);
        m.advance(5.0);
        QCOMPARE(m.state().outPct, 10.0); // Manual: the PID does not run
    }

    void pidSettingsClamped()
    {
        LoopModel m;
        PidSettings pid;
        pid.kp = 500.0;
        pid.tiS = -3.0;
        pid.tdS = 9999.0;
        m.setPid(pid);
        QCOMPARE(m.pid().kp, 100.0);
        QCOMPARE(m.pid().tiS, 0.0);
        QCOMPARE(m.pid().tdS, 600.0);
    }
};

QTEST_MAIN(TstLoopModel)
#include "tst_loopmodel.moc"
