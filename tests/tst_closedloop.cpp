#include "ClosedLoopAnalysis.h"
#include "Recorder.h"
#include "SampleBatch.h"
#include "Tag.h"

#include <QtTest>

#include <cmath>
#include <functional>

using namespace mb;

namespace {

constexpr TagId kPv = 1;
constexpr TagId kSp = 2;
constexpr TagId kCo = 3;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kPi = 3.14159265358979323846;

constexpr double kDt = 0.01;
constexpr qsizetype kStepIdx = 500;
constexpr qsizetype kN = 2500;

bool near(double a, double b, double tol)
{
    return std::abs(a - b) <= tol;
}

TagSample good(TagId id, double value)
{
    TagSample s;
    s.tagId = id;
    s.value = value;
    s.quality = Quality::Good;
    return s;
}

TagSample bad(TagId id)
{
    TagSample s;
    s.tagId = id;
    s.value = 999.0;
    s.quality = Quality::Timeout;
    return s;
}

/// Unit step response of a second-order system.
double y2(double s, double zeta, double wn)
{
    if (s <= 0.0) {
        return 0.0;
    }
    const double k = std::sqrt(1.0 - zeta * zeta);
    const double wd = wn * k;
    return 1.0 - std::exp(-zeta * wn * s) * (std::cos(wd * s) + zeta / k * std::sin(wd * s));
}

/// SP step spBefore -> spAfter at sample kStepIdx; PV = spBefore + d * response(t - tStep).
ClosedLoopSeries stepSeries(double spBefore, double spAfter, const std::function<double(double)> &resp)
{
    ClosedLoopSeries s;
    for (qsizetype i = 0; i <= kN; ++i) {
        s.t.append(double(i) * kDt);
        s.sp.append(i >= kStepIdx ? spAfter : spBefore);
        s.pv.append(spBefore + (spAfter - spBefore) * resp(double(i - kStepIdx) * kDt));
    }
    return s;
}

/// Constant SP, PV = sp - e(t - tStep).
ClosedLoopSeries disturbanceSeries(double sp, const std::function<double(double)> &e)
{
    ClosedLoopSeries s;
    for (qsizetype i = 0; i <= kN; ++i) {
        s.t.append(double(i) * kDt);
        s.sp.append(sp);
        s.pv.append(sp - e(double(i - kStepIdx) * kDt));
    }
    return s;
}

double tStep()
{
    return double(kStepIdx) * kDt;
}

double tEnd()
{
    return double(kN) * kDt;
}

/// Brute force: time after the last sample (after the first one from startIdx) outside the band.
double bruteSettling(const ClosedLoopSeries &s, qsizetype startIdx, double band)
{
    qsizetype last = -1;
    for (qsizetype i = startIdx + 1; i < s.t.size(); ++i) {
        if (std::abs(s.sp[i] - s.pv[i]) > band) {
            last = i;
        }
    }
    if (last < 0) {
        return 0.0;
    }
    if (last == s.t.size() - 1) {
        return kNaN;
    }
    return s.t[last + 1] - s.t[startIdx];
}

/// Numeric integral of f over [0, T] (fine trapezoid).
double integrate(const std::function<double(double)> &f, double T)
{
    const double h = 1e-4;
    const qsizetype n = qsizetype(T / h);
    double sum = 0.0;
    for (qsizetype i = 0; i < n; ++i) {
        sum += 0.5 * (f(double(i) * h) + f(double(i + 1) * h)) * h;
    }
    return sum;
}

SampleBatch batch(double t, QList<TagSample> samples)
{
    SampleBatch b;
    b.t = t;
    b.samples = std::move(samples);
    return b;
}

void fillRecorder(Recorder &rec, const ClosedLoopSeries &s, bool withSp, bool withCo)
{
    for (qsizetype i = 0; i < s.t.size(); ++i) {
        QList<TagSample> v;
        v.append(good(kPv, s.pv[i]));
        if (withSp) {
            v.append(good(kSp, s.sp[i]));
        }
        if (withCo) {
            v.append(good(kCo, s.co[i]));
        }
        rec.append(batch(s.t[i], v));
    }
}

bool hasCheck(const ClosedLoopResult &r, ClosedLoopCheck c)
{
    return r.checks.contains(c);
}

} // namespace

class TstClosedLoop : public QObject
{
    Q_OBJECT
private slots:
    void underdampedStep()
    {
        const double zeta = 0.3;
        const double wn = 2.0;
        const double k = std::sqrt(1.0 - zeta * zeta);
        const double overshoot1 = std::exp(-kPi * zeta / k); // fraction of the step
        const auto s = stepSeries(10.0, 12.0, [&](double x) { return y2(x, zeta, wn); });
        ClosedLoopSettings st;
        const auto r = analyzeClosedLoopSeries(s, 0.0, tEnd(), st, 0.0, 100.0);
        QVERIFY2(r.valid, qPrintable(r.error));
        QVERIFY(r.test == ClosedLoopTest::SetpointStep);
        QVERIFY(near(r.startTime, tStep(), 1e-9));
        QVERIFY(near(r.spBefore, 10.0, 1e-9));
        QVERIFY(near(r.spAfter, 12.0, 1e-9));
        QVERIFY(near(r.pvAtStart, 10.0, 1e-9));
        QVERIFY(r.overshoot.has_value());
        QVERIFY(near(*r.overshoot, 2.0 * overshoot1, 1e-3));
        QVERIFY(near(*r.overshootPct, 100.0 * overshoot1, 0.05));
        QVERIFY(r.peakTime.has_value());
        QVERIFY(near(*r.peakTime, kPi / (wn * k), kDt));
        QVERIFY(r.decayRatio.has_value());
        QVERIFY(near(*r.decayRatio, overshoot1 * overshoot1, 2e-3));
        QVERIFY(r.period.has_value());
        QVERIFY(near(*r.period, 2.0 * kPi / (wn * k), 2.0 * kDt));
        QVERIFY(r.riseTime.has_value());
        QVERIFY(*r.riseTime > 0.0 && *r.riseTime < *r.peakTime);
        // settling: same samples, brute force
        const double brute = bruteSettling(s, kStepIdx, 0.02 * 2.0);
        QVERIFY(r.settlingTime.has_value());
        QVERIFY(near(*r.settlingTime, brute, 1e-9));
        QVERIFY(*r.settlingTime > 3.0 && *r.settlingTime < 10.0);
        QVERIFY(near(r.steadyStateError, 0.0, 1e-3));
        // integrals vs the analytic e(t) over [0, tEnd - tStep]
        const double T = tEnd() - tStep();
        const double iae = integrate([&](double x) { return 2.0 * std::abs(1.0 - y2(x, zeta, wn)); }, T);
        const double ise = integrate([&](double x) { const double e = 2.0 * (1.0 - y2(x, zeta, wn)); return e * e; }, T);
        const double itae = integrate([&](double x) { return x * 2.0 * std::abs(1.0 - y2(x, zeta, wn)); }, T);
        QVERIFY(near(r.iae, iae, 2e-3));
        QVERIFY(near(r.ise, ise, 2e-3));
        QVERIFY(near(r.itae, itae, 5e-3));
        QVERIFY(!hasCheck(r, ClosedLoopCheck::NotSettled));
        QVERIFY(hasCheck(r, ClosedLoopCheck::NoOutput));
        QVERIFY(!r.outMin && !r.outMax && !r.outTravel);
    }

    void swappedCursorsAndNaNSamples()
    {
        auto s = stepSeries(10.0, 12.0, [](double x) { return y2(x, 0.3, 2.0); });
        ClosedLoopSettings st;
        const auto ref = analyzeClosedLoopSeries(s, 0.0, tEnd(), st, 0.0, 100.0);
        const auto swapped = analyzeClosedLoopSeries(s, tEnd(), 0.0, st, 0.0, 100.0);
        QVERIFY(swapped.valid);
        QVERIFY(near(swapped.t1, 0.0, 1e-12) && near(swapped.t2, tEnd(), 1e-12));
        QVERIFY(near(*swapped.overshoot, *ref.overshoot, 1e-12));
        // NaN PV samples are skipped
        for (qsizetype i = 600; i < 640; ++i) {
            s.pv[i] = kNaN;
        }
        s.sp[1000] = kNaN;
        const auto r = analyzeClosedLoopSeries(s, 0.0, tEnd(), st, 0.0, 100.0);
        QVERIFY(r.valid);
        QVERIFY(near(*r.overshoot, *ref.overshoot, 5e-3));
        QVERIFY(near(*r.settlingTime, *ref.settlingTime, 0.05));
        QVERIFY(std::isfinite(r.iae));
    }

    void firstOrderNoOvershoot()
    {
        const double T = 2.0;
        const auto s = stepSeries(0.0, 1.0, [&](double x) { return x > 0.0 ? 1.0 - std::exp(-x / T) : 0.0; });
        ClosedLoopSettings st;
        const auto r = analyzeClosedLoopSeries(s, 0.0, tEnd(), st, 0.0, 100.0);
        QVERIFY2(r.valid, qPrintable(r.error));
        QVERIFY(r.overshoot && *r.overshoot == 0.0);
        QVERIFY(r.overshootPct && *r.overshootPct == 0.0);
        QVERIFY(!r.peakTime);
        QVERIFY(!r.decayRatio);
        QVERIFY(!r.period);
        QVERIFY(r.riseTime.has_value());
        QVERIFY(near(*r.riseTime, T * std::log(9.0), 0.01));
        QVERIFY(r.settlingTime.has_value());
        QVERIFY(near(*r.settlingTime, T * std::log(50.0), 0.02)); // 2 % band
        QVERIFY(near(r.steadyStateError, 0.0, 1e-3));
    }

    void fallingStep()
    {
        const double zeta = 0.3;
        const double wn = 2.0;
        const auto up = analyzeClosedLoopSeries(
            stepSeries(10.0, 12.0, [&](double x) { return y2(x, zeta, wn); }), 0.0, tEnd(),
            ClosedLoopSettings{}, 0.0, 100.0);
        const auto down = analyzeClosedLoopSeries(
            stepSeries(12.0, 10.0, [&](double x) { return y2(x, zeta, wn); }), 0.0, tEnd(),
            ClosedLoopSettings{}, 0.0, 100.0);
        QVERIFY(down.valid);
        QVERIFY(near(down.spBefore, 12.0, 1e-9) && near(down.spAfter, 10.0, 1e-9));
        QVERIFY(near(*down.overshoot, *up.overshoot, 1e-9));
        QVERIFY(near(*down.overshootPct, *up.overshootPct, 1e-9));
        QVERIFY(near(*down.peakTime, *up.peakTime, 1e-9));
        QVERIFY(near(*down.riseTime, *up.riseTime, 1e-9));
        QVERIFY(near(*down.decayRatio, *up.decayRatio, 1e-9));
        QVERIFY(near(*down.settlingTime, *up.settlingTime, 1e-9));
        QVERIFY(near(down.iae, up.iae, 1e-9));
        QVERIFY(near(down.steadyStateError, -up.steadyStateError, 1e-9));
    }

    void offsetNotSettled()
    {
        // P-only: PV ends at 8 of the 10 requested.
        const double T = 2.0;
        const auto s = stepSeries(0.0, 10.0, [&](double x) { return x > 0.0 ? 0.8 * (1.0 - std::exp(-x / T)) : 0.0; });
        const auto r = analyzeClosedLoopSeries(s, 0.0, tEnd(), ClosedLoopSettings{}, 0.0, 100.0);
        QVERIFY(r.valid);
        QVERIFY(near(r.steadyStateError, 2.0, 1e-3));
        QVERIFY(!r.settlingTime);
        QVERIFY(hasCheck(r, ClosedLoopCheck::NotSettled));
        QVERIFY(*r.overshoot == 0.0);
        QVERIFY(!r.riseTime); // the PV never reaches 90 % of the step
    }

    void disturbanceShape()
    {
        const double tau = 2.0;
        const double A = 4.0;
        const auto make = [&](double amp) {
            return disturbanceSeries(50.0, [=](double x) {
                if (x < 0.0) {
                    return 0.0;
                }
                const double u = x / tau;
                return amp * u * std::exp(1.0 - u);
            });
        };
        const auto solveX = [](double frac) { // x > 1 with x exp(1 - x) = frac
            double lo = 1.0;
            double hi = 60.0;
            for (int i = 0; i < 200; ++i) {
                const double mid = 0.5 * (lo + hi);
                if (mid * std::exp(1.0 - mid) > frac) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            return 0.5 * (lo + hi);
        };
        for (const double amp : {A, -A}) {
            const auto s = make(amp);
            ClosedLoopSettings st;
            st.test = ClosedLoopTest::Disturbance;
            for (const double pct : {2.0, 5.0}) {
                st.settlingBandPct = pct;
                const auto r = analyzeClosedLoopSeries(s, tStep(), tEnd(), st, 0.0, 100.0);
                QVERIFY2(r.valid, qPrintable(r.error));
                QVERIFY(r.test == ClosedLoopTest::Disturbance);
                QVERIFY(near(r.startTime, tStep(), 1e-12));
                QVERIFY(near(r.spAfter, 50.0, 1e-12));
                QVERIFY(!r.overshoot && !r.overshootPct && !r.peakTime && !r.riseTime);
                QVERIFY(r.maxDeviation.has_value());
                QVERIFY(near(*r.maxDeviation, amp, 1e-3));
                QVERIFY(near(*r.maxDeviationTime, tau, kDt));
                QVERIFY(r.settlingTime.has_value());
                QVERIFY(near(*r.settlingTime, tau * solveX(pct / 100.0), 2.0 * kDt));
                QVERIFY(!r.decayRatio && !r.period);
                QVERIFY(!hasCheck(r, ClosedLoopCheck::SetpointMoved));
                QVERIFY(near(r.steadyStateError, 0.0, 0.02)); // e is still 0.3 % of A at the end
                // analytic integrals of e = amp u exp(1 - u), truncated at X = (tEnd - tStep) / tau
                const double X = (tEnd() - tStep()) / tau;
                const double iae = std::abs(amp) * tau * std::exp(1.0) * (1.0 - (1.0 + X) * std::exp(-X));
                QVERIFY(near(r.iae, iae, 2e-3 * iae));
                const double ise = amp * amp * std::exp(2.0) * tau / 4.0;
                QVERIFY(near(r.ise, ise, 2e-3 * ise));
            }
        }
    }

    void disturbanceSetpointMoved()
    {
        auto s = disturbanceSeries(50.0, [](double x) { return x > 0.0 ? 2.0 * std::exp(-x) : 0.0; });
        ClosedLoopSettings st;
        st.test = ClosedLoopTest::Disturbance;
        QVERIFY(!hasCheck(analyzeClosedLoopSeries(s, tStep(), tEnd(), st, 0.0, 100.0),
                          ClosedLoopCheck::SetpointMoved));
        for (qsizetype i = 1200; i <= kN; ++i) {
            s.sp[i] = 51.0;
        }
        const auto r = analyzeClosedLoopSeries(s, tStep(), tEnd(), st, 0.0, 100.0);
        QVERIFY(r.valid);
        QVERIFY(hasCheck(r, ClosedLoopCheck::SetpointMoved));
    }

    void noSetpointStep()
    {
        auto s = disturbanceSeries(50.0, [](double x) { return x > 0.0 ? 2.0 * std::exp(-x) : 0.0; });
        const auto r = analyzeClosedLoopSeries(s, 0.0, tEnd(), ClosedLoopSettings{}, 0.0, 100.0);
        QVERIFY(!r.valid);
        QVERIFY(hasCheck(r, ClosedLoopCheck::NoSetpointStep));
        QCOMPARE(r.error, QStringLiteral("The setpoint does not change between the cursors"));
    }

    void outputMetrics()
    {
        ClosedLoopSeries s;
        const double co[] = {20, 20, 20, 60, 60, 60, 50, 40, 40, 40, 40};
        for (int i = 0; i <= 10; ++i) {
            s.t.append(double(i));
            s.sp.append(i >= 2 ? 1.0 : 0.0);
            s.pv.append(i >= 2 ? 1.0 - std::exp(-(i - 2) / 2.0) : 0.0);
            s.co.append(co[i]);
        }
        const auto r = analyzeClosedLoopSeries(s, 0.0, 10.0, ClosedLoopSettings{}, 0.0, 100.0);
        QVERIFY2(r.valid, qPrintable(r.error));
        QVERIFY(near(r.startTime, 2.0, 1e-12));
        QVERIFY(r.outMin && near(*r.outMin, 20.0, 1e-12));
        QVERIFY(r.outMax && near(*r.outMax, 60.0, 1e-12));
        QVERIFY(r.outTravel && near(*r.outTravel, 60.0, 1e-12)); // 40 + 10 + 10
        QVERIFY(!hasCheck(r, ClosedLoopCheck::OutputSaturated));
        QVERIFY(!hasCheck(r, ClosedLoopCheck::NoOutput));

        // the output before startTime is ignored
        s.co[0] = 0.0;
        s.co[1] = 100.0;
        QVERIFY(!hasCheck(analyzeClosedLoopSeries(s, 0.0, 10.0, ClosedLoopSettings{}, 0.0, 100.0),
                          ClosedLoopCheck::OutputSaturated));

        const auto sat = [&](double value) {
            auto s2 = s;
            s2.co[5] = value;
            return hasCheck(analyzeClosedLoopSeries(s2, 0.0, 10.0, ClosedLoopSettings{}, 0.0, 100.0),
                            ClosedLoopCheck::OutputSaturated);
        };
        QVERIFY(sat(100.0));
        QVERIFY(sat(99.95));
        QVERIFY(!sat(99.8)); // 0.2 % inside
        QVERIFY(sat(0.0));
        QVERIFY(sat(0.05));
        QVERIFY(!sat(0.2));
        // NaN output samples are skipped
        auto s3 = s;
        s3.co[4] = kNaN;
        const auto r3 = analyzeClosedLoopSeries(s3, 0.0, 10.0, ClosedLoopSettings{}, 0.0, 100.0);
        QVERIFY(near(*r3.outTravel, 60.0, 1e-12));
    }

    void noisyPeaks()
    {
        // Underdamped response with noise well below the 2 % band: peaks stay those of the signal.
        const double zeta = 0.3;
        const double wn = 2.0;
        const double k = std::sqrt(1.0 - zeta * zeta);
        auto s = stepSeries(10.0, 12.0, [&](double x) { return y2(x, zeta, wn); });
        quint32 seed = 12345;
        for (qsizetype i = 0; i < s.pv.size(); ++i) {
            seed = seed * 1664525u + 1013904223u;
            s.pv[i] += (double(seed >> 8) / double(1u << 24) - 0.5) * 0.02; // +-0.01 (0.5 % of d)
        }
        const auto r = analyzeClosedLoopSeries(s, 0.0, tEnd(), ClosedLoopSettings{}, 0.0, 100.0);
        QVERIFY(r.valid);
        QVERIFY(r.decayRatio.has_value());
        QVERIFY(near(*r.decayRatio, std::pow(std::exp(-kPi * zeta / k), 2.0), 0.05));
        QVERIFY(near(*r.period, 2.0 * kPi / (wn * k), 0.1));
    }

    void degenerateSeries()
    {
        ClosedLoopSettings st;
        // single sample
        ClosedLoopSeries one;
        one.t = {0.0};
        one.pv = {1.0};
        one.sp = {1.0};
        auto r = analyzeClosedLoopSeries(one, 0.0, 1.0, st, 0.0, 100.0);
        QVERIFY(!r.valid);
        QCOMPARE(r.error, QStringLiteral("No data between the cursors"));
        // empty
        r = analyzeClosedLoopSeries(ClosedLoopSeries{}, 0.0, 1.0, st, 0.0, 100.0);
        QVERIFY(!r.valid);
        // NaN cursors
        const auto s = stepSeries(10.0, 12.0, [](double x) { return y2(x, 0.3, 2.0); });
        r = analyzeClosedLoopSeries(s, kNaN, 1.0, st, 0.0, 100.0);
        QVERIFY(!r.valid);
        QCOMPARE(r.error, QStringLiteral("Place both cursors"));
        r = analyzeClosedLoopSeries(s, 1.0, kNaN, st, 0.0, 100.0);
        QVERIFY(!r.valid);
        // cursors outside the data
        r = analyzeClosedLoopSeries(s, 1000.0, 2000.0, st, 0.0, 100.0);
        QVERIFY(!r.valid);
        QCOMPARE(r.error, QStringLiteral("No data between the cursors"));
        // all PV NaN
        auto allNaN = s;
        for (auto &v : allNaN.pv) {
            v = kNaN;
        }
        QVERIFY(!analyzeClosedLoopSeries(allNaN, 0.0, tEnd(), st, 0.0, 100.0).valid);
        // the series ends at the step time
        ClosedLoopSeries e;
        for (int i = 0; i < 5; ++i) {
            e.t.append(double(i));
            e.sp.append(i == 4 ? 1.0 : 0.0);
            e.pv.append(0.0);
        }
        r = analyzeClosedLoopSeries(e, 0.0, 4.0, st, 0.0, 100.0);
        QVERIFY(r.valid);
        QVERIFY(near(r.startTime, 4.0, 1e-12));
        QVERIFY(!r.settlingTime);
        QVERIFY(std::isfinite(r.iae) && std::isfinite(r.steadyStateError));
        // duplicated times
        ClosedLoopSeries dup;
        dup.t = {0, 1, 1, 1, 2, 3, 4, 5};
        dup.sp = {0, 0, 1, 1, 1, 1, 1, 1};
        dup.pv = {0, 0, 0.2, 0.3, 0.6, 0.9, 1.0, 1.0};
        r = analyzeClosedLoopSeries(dup, 0.0, 5.0, st, 0.0, 100.0);
        QVERIFY(r.valid);
        QVERIFY(std::isfinite(r.iae) && std::isfinite(r.ise) && std::isfinite(r.itae));
        QVERIFY(r.riseTime.has_value());
        QVERIFY(std::isfinite(*r.riseTime));
        // disturbance with a single sample after the cursor
        ClosedLoopSettings dst;
        dst.test = ClosedLoopTest::Disturbance;
        r = analyzeClosedLoopSeries(dup, 5.0, 5.0, dst, 0.0, 100.0);
        QVERIFY(!r.valid);
    }

    // --- Recorder based ----------------------------------------------------------------------

    void recorderTagSetpoint()
    {
        auto s = stepSeries(10.0, 12.0, [](double x) { return y2(x, 0.3, 2.0); });
        s.co.clear();
        for (qsizetype i = 0; i < s.t.size(); ++i) {
            s.co.append(i > kStepIdx ? 40.0 : 20.0);
        }
        Recorder rec(10000);
        fillRecorder(rec, s, true, true);
        const auto direct = analyzeClosedLoopSeries(s, 0.0, tEnd(), ClosedLoopSettings{}, 0.0, 100.0);
        const auto r = analyzeClosedLoop(rec, kPv, kSp, kCo, tEnd(), 0.0, ClosedLoopSettings{}, 0.0, 100.0);
        QVERIFY2(r.valid, qPrintable(r.error));
        QVERIFY(near(r.startTime, tStep(), 1e-9));
        QVERIFY(near(*r.overshoot, *direct.overshoot, 1e-12));
        QVERIFY(near(*r.settlingTime, *direct.settlingTime, 1e-12));
        QVERIFY(near(r.iae, direct.iae, 1e-12));
        QVERIFY(r.outTravel && near(*r.outTravel, 20.0, 1e-12));
        QVERIFY(!hasCheck(r, ClosedLoopCheck::NoOutput));
        // no output tag
        const auto noCo = analyzeClosedLoop(rec, kPv, kSp, 0, 0.0, tEnd(), ClosedLoopSettings{}, 0.0, 100.0);
        QVERIFY(noCo.valid && hasCheck(noCo, ClosedLoopCheck::NoOutput) && !noCo.outMin);
        // a sub-range: the cursors cut the rows
        const auto part = analyzeClosedLoop(rec, kPv, kSp, 0, 4.0, 10.0, ClosedLoopSettings{}, 0.0, 100.0);
        QVERIFY(part.valid);
        QVERIFY(near(part.t1, 4.0, 1e-12) && near(part.t2, 10.0, 1e-12));
        QVERIFY(near(part.startTime, tStep(), 1e-9));
    }

    void recorderTypedSetpointStep()
    {
        const double zeta = 0.3;
        const double wn = 2.0;
        const double k = std::sqrt(1.0 - zeta * zeta);
        const double t0 = 5.0;
        ClosedLoopSeries s; // rows start at the step
        for (qsizetype i = 0; i <= 2000; ++i) {
            s.t.append(t0 + double(i) * kDt);
            s.pv.append(10.0 + 2.0 * y2(double(i) * kDt, zeta, wn));
        }
        // a bad row before the first one must not matter
        Recorder rec(10000);
        rec.append(batch(t0 - 1.0, {bad(kPv)}));
        for (qsizetype i = 0; i < s.t.size(); ++i) {
            rec.append(batch(s.t[i], {good(kPv, s.pv[i])}));
        }
        ClosedLoopSettings st;
        st.typedSpBefore = 10.0;
        st.typedSpAfter = 12.0;
        const auto r = analyzeClosedLoop(rec, kPv, 0, 0, t0, s.t.last(), st, 0.0, 100.0);
        QVERIFY2(r.valid, qPrintable(r.error));
        QVERIFY(near(r.startTime, t0, 1e-12));
        QVERIFY(near(r.spBefore, 10.0, 1e-12) && near(r.spAfter, 12.0, 1e-12));
        QVERIFY(near(*r.overshoot, 2.0 * std::exp(-kPi * zeta / k), 1e-3));
        QVERIFY(near(*r.peakTime, kPi / (wn * k), kDt));
        QVERIFY(near(*r.decayRatio, std::exp(-2.0 * kPi * zeta / k), 2e-3));
        QVERIFY(near(r.steadyStateError, 0.0, 1e-3));
        // cursors given in the other order, a typed SP that falls
        st.typedSpBefore = 12.0;
        st.typedSpAfter = 10.0;
        Recorder rec2(10000);
        for (qsizetype i = 0; i < s.t.size(); ++i) {
            rec2.append(batch(s.t[i], {good(kPv, 22.0 - s.pv[i])}));
        }
        const auto f = analyzeClosedLoop(rec2, kPv, 0, 0, s.t.last(), t0, st, 0.0, 100.0);
        QVERIFY(f.valid);
        QVERIFY(near(*f.overshoot, *r.overshoot, 1e-9));
        // no step typed
        st.typedSpBefore = 10.0;
        st.typedSpAfter = 10.0;
        const auto n = analyzeClosedLoop(rec, kPv, 0, 0, t0, s.t.last(), st, 0.0, 100.0);
        QVERIFY(!n.valid && hasCheck(n, ClosedLoopCheck::NoSetpointStep));
    }

    void recorderTypedDisturbance()
    {
        const double tau = 2.0;
        const double A = -2.5;
        Recorder rec(10000);
        double tLast = 0.0;
        for (qsizetype i = 0; i <= 2000; ++i) {
            const double x = double(i) * kDt;
            const double u = x / tau;
            tLast = 5.0 + x;
            rec.append(batch(tLast, {good(kPv, 30.0 - A * u * std::exp(1.0 - u)), good(kCo, 50.0 + A)}));
        }
        ClosedLoopSettings st;
        st.test = ClosedLoopTest::Disturbance;
        st.typedSpAfter = 30.0;
        st.typedSpBefore = 99.0; // ignored
        const auto r = analyzeClosedLoop(rec, kPv, 0, kCo, 5.0, tLast, st, 0.0, 100.0);
        QVERIFY2(r.valid, qPrintable(r.error));
        QVERIFY(near(r.startTime, 5.0, 1e-12));
        QVERIFY(near(r.spAfter, 30.0, 1e-12));
        QVERIFY(near(*r.maxDeviation, A, 1e-3));
        QVERIFY(near(*r.maxDeviationTime, tau, kDt));
        QVERIFY(r.settlingTime.has_value());
        QVERIFY(!hasCheck(r, ClosedLoopCheck::SetpointMoved));
        QVERIFY(r.outTravel && *r.outTravel == 0.0);
    }

    void recorderInvalid()
    {
        Recorder rec(100);
        ClosedLoopSettings st;
        auto r = analyzeClosedLoop(rec, kPv, 0, 0, 0.0, 1.0, st, 0.0, 100.0);
        QVERIFY(!r.valid);
        QCOMPARE(r.error, QStringLiteral("No data between the cursors"));
        r = analyzeClosedLoop(rec, kPv, 0, 0, kNaN, 1.0, st, 0.0, 100.0);
        QVERIFY(!r.valid);
        QCOMPARE(r.error, QStringLiteral("Place both cursors"));
        rec.append(batch(0.0, {good(kPv, 1.0)}));
        rec.append(batch(1.0, {bad(kPv)}));
        rec.append(batch(2.0, {bad(kPv)}));
        r = analyzeClosedLoop(rec, kPv, 0, 0, 0.0, 2.0, st, 0.0, 100.0);
        QVERIFY(!r.valid); // one row with a PV value
        QCOMPARE(r.error, QStringLiteral("No data between the cursors"));
    }
};

QTEST_GUILESS_MAIN(TstClosedLoop)
#include "tst_closedloop.moc"
