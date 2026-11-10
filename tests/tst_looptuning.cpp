// Regression test: the Loop Simulator's process model, the app's step analysis and the app's
// tuning rules run together, and the loop is closed with the tuned settings. Pins the numbers
// quoted by the Loop Simulator guide. Deterministic: LoopModel has a fixed seed, no wall clock.
#include "LoopModel.h"
#include "Recorder.h"
#include "SampleBatch.h"
#include "StepAnalysis.h"
#include "Tag.h"
#include "Tuning.h"

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace mb;

namespace {

constexpr TagId kPv = 1;
constexpr TagId kCo = 2;

constexpr double kSampleS = 0.2;
constexpr double kPreS = 40.0;   ///< settling time before recording
constexpr double kStepAtS = 60.0;
constexpr double kEndS = 300.0;

TagSample good(TagId id, double value)
{
    TagSample s;
    s.tagId = id;
    s.value = value;
    s.quality = Quality::Good;
    return s;
}

SampleBatch batch(double t, double pv, double co)
{
    SampleBatch b;
    b.t = t;
    b.samples = {good(kPv, pv), good(kCo, co)};
    return b;
}

struct OpenLoop
{
    Recorder recorder;
    AnalysisSettings settings;
    StepResult result;
};

/// Open-loop step test of the simulator: OUT outFrom -> outTo at t = 60 s (recorded from 40 s,
/// every 0.2 s), analysed between 50 s (C1) and the end with `method`.
OpenLoop recordOpenLoopStep(const mbloop::LoopParams &params, double outFrom, double outTo,
                            ModelMethod method)
{
    OpenLoop r;
    mbloop::LoopModel m(1);
    m.reset(params, outFrom);
    m.advance(kPreS);
    const int n = int(std::lround((kStepAtS + kEndS - kPreS) / kSampleS));
    const int stepIndex = int(std::lround((kStepAtS - kPreS) / kSampleS));
    for (int i = 0; i <= n; ++i) {
        const double t = kPreS + i * kSampleS;
        double co = outFrom;
        if (i == stepIndex) {
            m.setOut(outTo);
            co = 0.5 * (outFrom + outTo); // the sample on the 50 % level: step time exactly 60 s
        } else if (i > stepIndex) {
            co = outTo;
        }
        r.recorder.append(batch(t, m.state().pvPct, co));
        m.advance(kSampleS);
    }
    r.settings.method = method;
    r.settings.pvTagId = kPv;
    r.settings.deltaCoTagId = kCo;
    r.result = analyzeStep(r.recorder, kPv, kStepAtS - 10.0, kStepAtS + kEndS, r.settings);
    return r;
}

struct ClosedLoop
{
    double overshootPv = 0.0;    ///< peak (PV - SP) after the SP change, % of PV
    double overshootStep = 0.0;  ///< the same in % of the SP step
    double settleS = 0.0;        ///< last time (after the SP change) with |PV - SP| > 0.5
    double outMin = 100.0;
    double outMax = 0.0;
    double swing = 0.0;          ///< max - min of PV over the last 60 s
};

/// Closed loop on a fresh model: Auto at spFrom for 30 s, then SP -> spTo, `duration` seconds.
ClosedLoop closedLoop(const mbloop::LoopParams &params, double kp, double ti, double td,
                      double spFrom, double spTo, double duration)
{
    mbloop::LoopModel m(1);
    m.reset(params, spFrom);
    m.setSp(spFrom);
    mbloop::PidSettings pid;
    pid.kp = kp;
    pid.tiS = ti;
    pid.tdS = td;
    pid.reverse = true;
    m.setPid(pid);
    m.setAuto(true);
    m.advance(30.0);

    ClosedLoop c;
    m.setSp(spTo);
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    const int n = int(std::lround(duration / 0.1));
    const int lastWindow = int(std::lround((duration - 60.0) / 0.1));
    for (int i = 1; i <= n; ++i) {
        m.advance(0.1);
        const double pv = m.state().pvPct;
        const double t = i * 0.1;
        c.overshootPv = std::max(c.overshootPv, pv - spTo);
        if (std::abs(pv - spTo) > 0.5) {
            c.settleS = t;
        }
        c.outMin = std::min(c.outMin, m.state().outPct);
        c.outMax = std::max(c.outMax, m.state().outPct);
        if (i > lastWindow) {
            lo = std::min(lo, pv);
            hi = std::max(hi, pv);
        }
    }
    c.swing = hi - lo;
    c.overshootStep = c.overshootPv / (spTo - spFrom) * 100.0;
    return c;
}

TuningRow rowOf(const QList<TuningRow> &rows, ControllerKind kind)
{
    for (const TuningRow &r : rows) {
        if (r.kind == kind) {
            return r;
        }
    }
    return {};
}

ClosedLoop closedLoopRow(const mbloop::LoopParams &p, const TuningRow &row, double duration = 400.0)
{
    return closedLoop(p, row.kp.value_or(0.0), row.ti.value_or(0.0), row.td.value_or(0.0), 50.0,
                      60.0, duration);
}

struct Model
{
    double K = 0.0;
    double L = 0.0;
    double T = 0.0;
};

Model modelOf(const mbloop::LoopParams &p, ModelMethod method)
{
    const OpenLoop o = recordOpenLoopStep(p, 50.0, 60.0, method);
    Model m;
    if (o.result.valid && o.result.gain && o.result.deadTime && o.result.timeConstant) {
        m.K = *o.result.gain;
        m.L = *o.result.deadTime;
        m.T = *o.result.timeConstant;
    }
    return m;
}

bool near(double a, double b, double tol)
{
    return std::abs(a - b) <= tol;
}

} // namespace

class TstLoopTuning : public QObject
{
    Q_OBJECT

private slots:
    void defaultParamsMatchExample();
    void smithModel();
    void zeroTo632Model();
    void zieglerNicholsFromSmithSettles();
    void zieglerNicholsFromZeroTo632DoesNot();
    void imcFromSmithIsGentle();
    void simcFromSmith();
    void checkAppearsOnlyForFirstMovementMethod();
    void guideNumbers();
};

void TstLoopTuning::defaultParamsMatchExample()
{
    const mbloop::LoopParams p;
    QCOMPARE(p.pumpSpeedPct, 100.0);
    QCOMPARE(p.hvOpeningPct, 50.0);
    QCOMPARE(p.deadTimeS, 2.0);
    QCOMPARE(p.ltNoisePct, 0.05);
    QCOMPARE(p.ltDampingS, 2.0);
    QCOMPARE(p.fcvStrokeS, 5.0);
}

void TstLoopTuning::smithModel()
{
    const Model m = modelOf(mbloop::LoopParams{}, ModelMethod::Smith);
    QVERIFY(near(m.K, 2.2, 0.11));
    QVERIFY(near(m.L, 4.0, 0.5));
    QVERIFY(near(m.T, 32.5, 2.0));
}

void TstLoopTuning::zeroTo632Model()
{
    const Model z = modelOf(mbloop::LoopParams{}, ModelMethod::ZeroTo632);
    const Model s = modelOf(mbloop::LoopParams{}, ModelMethod::Smith);
    // L at the first movement depends on the noise at the cursor (0.25 s with C1 at 59.5 s,
    // 1.05 s at 55 s, 1.22 s at 50 s); the cursor is placed 10 s before the step.
    QVERIFY(near(z.L, 1.2, 0.4));
    QVERIFY(near(z.T, 35.3, 2.0));
    QVERIFY(s.L - z.L >= 1.5);
}

void TstLoopTuning::zieglerNicholsFromSmithSettles()
{
    const mbloop::LoopParams p;
    const Model m = modelOf(p, ModelMethod::Smith);
    const TuningRow pi = rowOf(tune(m.K, m.L, m.T, TuningRule::ZieglerNichols), ControllerKind::PI);
    QVERIFY(pi.available);
    QVERIFY(near(*pi.kp, 3.3, 0.4));
    QVERIFY(near(*pi.ti, 13.4, 2.0));
    const ClosedLoop c = closedLoopRow(p, pi);
    QVERIFY(c.settleS < 90.0);
    QVERIFY(c.swing < 0.5);
    QVERIFY(c.overshootPv > 2.0 && c.overshootPv < 9.0);
}

void TstLoopTuning::zieglerNicholsFromZeroTo632DoesNot()
{
    const mbloop::LoopParams p;
    const Model m = modelOf(p, ModelMethod::ZeroTo632);
    const TuningRow pi = rowOf(tune(m.K, m.L, m.T, TuningRule::ZieglerNichols), ControllerKind::PI);
    QVERIFY(pi.available);
    QVERIFY(*pi.kp > 6.0);
    const ClosedLoop c = closedLoopRow(p, pi);
    QVERIFY(c.swing > 5.0);
    QVERIFY(c.outMin <= 0.0);
    QVERIFY(c.outMax >= 100.0);
}

void TstLoopTuning::imcFromSmithIsGentle()
{
    const mbloop::LoopParams p;
    const Model m = modelOf(p, ModelMethod::Smith);
    const TuningRow pi = rowOf(tune(m.K, m.L, m.T, TuningRule::ImcLambda, 0.0), ControllerKind::PI);
    QVERIFY(pi.available);
    QVERIFY(near(*pi.kp, 1.8, 0.3));
    QVERIFY(near(*pi.ti, 32.5, 2.0));
    const ClosedLoop c = closedLoopRow(p, pi);
    QVERIFY(c.overshootPv < 2.0);
    QVERIFY(c.settleS < 40.0);
}

void TstLoopTuning::simcFromSmith()
{
    const mbloop::LoopParams p;
    const Model m = modelOf(p, ModelMethod::Smith);
    const TuningRow pi = rowOf(tune(m.K, m.L, m.T, TuningRule::Simc, 0.0), ControllerKind::PI);
    QVERIFY(pi.available);
    QVERIFY(near(*pi.kp, 1.86, 0.3));
    QVERIFY(near(*pi.ti, 31.8, 2.0));
    const ClosedLoop c = closedLoopRow(p, pi);
    QVERIFY(c.overshootPv < 2.0);
    QVERIFY(c.settleS < 40.0);
    QVERIFY(c.swing < 0.5);
}

void TstLoopTuning::checkAppearsOnlyForFirstMovementMethod()
{
    const mbloop::LoopParams p;
    {
        OpenLoop o = recordOpenLoopStep(p, 50.0, 60.0, ModelMethod::ZeroTo632);
        o.settings.tuningRule = TuningRule::ZieglerNichols;
        const QList<StepCheck> checks = checkStep(o.recorder, o.result, o.settings);
        QVERIFY(checks.contains(StepCheck::DeadTimeFromFirstMove));
    }
    {
        OpenLoop o = recordOpenLoopStep(p, 50.0, 60.0, ModelMethod::Smith);
        o.settings.tuningRule = TuningRule::ZieglerNichols;
        const QList<StepCheck> checks = checkStep(o.recorder, o.result, o.settings);
        QVERIFY(!checks.contains(StepCheck::DeadTimeFromFirstMove));
    }
}

void TstLoopTuning::guideNumbers()
{
    const mbloop::LoopParams p;
    QStringList lines;
    lines << "Loop Simulator guide numbers (default loop, OUT 50 -> 60 %, SP 50 -> 60 %)";
    lines << "Model            K      L (s)  T (s)";
    const struct { ModelMethod m; const char *name; } methods[] = {
        {ModelMethod::ZeroTo632, "0 % / 63.2 %"},
        {ModelMethod::Smith, "Smith"},
        {ModelMethod::Sundaresan, "Sundaresan"},
    };
    for (const auto &me : methods) {
        const Model m = modelOf(p, me.m);
        lines << QStringLiteral("%1 %2 %3 %4")
                     .arg(QString::fromLatin1(me.name), -16)
                     .arg(m.K, 6, 'f', 3)
                     .arg(m.L, 6, 'f', 2)
                     .arg(m.T, 6, 'f', 2);
    }
    const Model s = modelOf(p, ModelMethod::Smith);
    lines << "Smith model, closed loop SP 50 -> 60 (400 s):";
    lines << "Rule             Kp      Ti (s)  Td (s)  overshoot %PV  overshoot %step  settle (s)";
    const struct { TuningRule r; ControllerKind k; const char *name; } rows[] = {
        {TuningRule::ZieglerNichols, ControllerKind::P, "ZN P"},
        {TuningRule::ZieglerNichols, ControllerKind::PI, "ZN PI"},
        {TuningRule::ZieglerNichols, ControllerKind::PID, "ZN PID"},
        {TuningRule::CohenCoon, ControllerKind::PI, "Cohen-Coon PI"},
        {TuningRule::ImcLambda, ControllerKind::PI, "IMC PI"},
        {TuningRule::Simc, ControllerKind::PI, "SIMC PI"},
    };
    for (const auto &ro : rows) {
        const TuningRow row = rowOf(tune(s.K, s.L, s.T, ro.r, 0.0), ro.k);
        if (!row.available) {
            continue;
        }
        const ClosedLoop c = closedLoopRow(p, row);
        lines << QStringLiteral("%1 %2 %3 %4 %5 %6 %7")
                     .arg(QString::fromLatin1(ro.name), -16)
                     .arg(row.kp.value_or(0.0), 6, 'f', 2)
                     .arg(row.ti.value_or(0.0), 7, 'f', 1)
                     .arg(row.td.value_or(0.0), 6, 'f', 1)
                     .arg(c.overshootPv, 13, 'f', 2)
                     .arg(c.overshootStep, 15, 'f', 1)
                     .arg(c.settleS, 10, 'f', 1);
    }
    qInfo().noquote() << "\n" + lines.join('\n');
}

QTEST_GUILESS_MAIN(TstLoopTuning)
#include "tst_looptuning.moc"
