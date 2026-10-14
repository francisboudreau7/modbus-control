#include "Recorder.h"
#include "SampleBatch.h"
#include "StepAnalysis.h"
#include "Tag.h"

#include <QtTest>

#include <cmath>
#include <functional>
#include <random>

using namespace mb;

namespace {

constexpr TagId kPv = 1;
constexpr TagId kCo = 2;

constexpr double kL = 1.5;
constexpr double kT = 4.0;

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

SampleBatch batch(double t, QList<TagSample> samples)
{
    SampleBatch b;
    b.t = t;
    b.samples = std::move(samples);
    return b;
}

bool near(double a, double b, double tol)
{
    return std::abs(a - b) <= tol;
}

QList<double> range(int n, double dt = 1.0)
{
    QList<double> t;
    for (int i = 0; i < n; ++i) {
        t.append(i * dt);
    }
    return t;
}

/// PV of a first-order-plus-dead-time response to a step at t = 10 s (CO 20 -> 30).
/// `sign` +1 rises from 10 to 30, -1 falls from 30 to 10.
double fopdt(double t, int sign = 1)
{
    const double rise = t > 10.0 + kL ? 20.0 * (1.0 - std::exp(-(t - 10.0 - kL) / kT)) : 0.0;
    return sign > 0 ? 10.0 + rise : 30.0 - rise;
}

/// CO step at t = 10 s; the sample exactly at 10 s sits on the 50 % level so that the
/// 50 % crossing is exactly 10 s.
double coStep(double t)
{
    if (t < 10.0) {
        return 20.0;
    }
    return t == 10.0 ? 25.0 : 30.0;
}

/// Samples every 0.1 s over [0, endS] with a PV (tag 1) and a CO (tag 2).
Recorder makeProcess(double endS, int sign = 1, double noiseStd = 0.0, unsigned seed = 1)
{
    Recorder rec;
    std::mt19937 gen(seed);
    std::normal_distribution<double> noise(0.0, noiseStd > 0.0 ? noiseStd : 1.0);
    const int n = int(std::lround(endS * 10.0));
    for (int i = 0; i <= n; ++i) {
        const double t = i / 10.0;
        double pv = fopdt(t, sign);
        if (noiseStd > 0.0) {
            pv += noise(gen);
        }
        rec.append(batch(t, {good(kPv, pv), good(kCo, coStep(t))}));
    }
    return rec;
}

/// Recording every 0.1 s over [0, endS] from two functions of time.
Recorder build(double endS, const std::function<double(double)> &pv,
               const std::function<double(double)> &co)
{
    Recorder rec;
    const int n = int(std::lround(endS * 10.0));
    for (int i = 0; i <= n; ++i) {
        const double t = i / 10.0;
        rec.append(batch(t, {good(kPv, pv(t)), good(kCo, co(t))}));
    }
    return rec;
}

double cleanCo(double t)
{
    return t < 10.0 ? 20.0 : 30.0;
}

double ramp(double t)
{
    return t < 10.0 ? 10.0 : 10.0 + (t - 10.0);
}

} // namespace

class Test : public QObject
{
    Q_OBJECT
private slots:
    // ---- smoothValues -------------------------------------------------------------------
    void smooth_widthZeroReturnsInput()
    {
        const QList<double> t = range(5);
        const QList<double> v{1, kNaN, 3, 4, 5};
        const QList<double> out = smoothValues(t, v, 0.0);
        QCOMPARE(out.size(), v.size());
        QCOMPARE(out[0], 1.0);
        QVERIFY(std::isnan(out[1]));
        QCOMPARE(out[4], 5.0);
        QCOMPARE(smoothValues(t, v, -3.0).size(), v.size());
    }

    void smooth_constantUnchanged()
    {
        const QList<double> t = range(50, 0.1);
        const QList<double> v(50, 7.25);
        const QList<double> out = smoothValues(t, v, 1.0);
        for (double x : out) {
            QVERIFY(near(x, 7.25, 1e-12));
        }
    }

    void smooth_knownExample()
    {
        const QList<double> t = range(5);
        const QList<double> v{0, 10, 20, 30, 40};
        const QList<double> out = smoothValues(t, v, 2.0); // half width 1
        QCOMPARE(out.size(), 5);
        QVERIFY(near(out[0], 5.0, 1e-12));
        QVERIFY(near(out[1], 10.0, 1e-12));
        QVERIFY(near(out[2], 20.0, 1e-12));
        QVERIFY(near(out[3], 30.0, 1e-12));
        QVERIFY(near(out[4], 35.0, 1e-12));
    }

    void smooth_nanHandling()
    {
        const QList<double> t = range(3);
        const QList<double> out = smoothValues(t, {1, kNaN, 3}, 2.0);
        QVERIFY(near(out[0], 1.0, 1e-12));
        QVERIFY(near(out[1], 2.0, 1e-12));
        QVERIFY(near(out[2], 3.0, 1e-12));

        const QList<double> out2 = smoothValues(t, {kNaN, kNaN, 5}, 0.5);
        QVERIFY(std::isnan(out2[0]));
        QVERIFY(std::isnan(out2[1]));
        QVERIFY(near(out2[2], 5.0, 1e-12));
    }

    void smooth_nonUniformSpacing()
    {
        const QList<double> t{0, 1, 1.5, 4};
        const QList<double> v{0, 3, 6, 9};
        const QList<double> out = smoothValues(t, v, 2.0);
        QVERIFY(near(out[0], 1.5, 1e-12));
        QVERIFY(near(out[1], 3.0, 1e-12));
        QVERIFY(near(out[2], 4.5, 1e-12));
        QVERIFY(near(out[3], 9.0, 1e-12));
    }

    void smooth_matchesBruteForce()
    {
        std::mt19937 gen(42);
        std::uniform_real_distribution<double> dt(0.0, 0.5);
        std::uniform_real_distribution<double> val(-10.0, 10.0);
        std::uniform_real_distribution<double> coin(0.0, 1.0);
        QList<double> t;
        QList<double> v;
        double now = 0.0;
        for (int i = 0; i < 400; ++i) {
            now += dt(gen);
            t.append(now);
            v.append(coin(gen) < 0.15 ? kNaN : val(gen));
        }
        const double width = 1.7;
        const QList<double> out = smoothValues(t, v, width);
        for (qsizetype i = 0; i < t.size(); ++i) {
            double sum = 0.0;
            int cnt = 0;
            for (qsizetype j = 0; j < t.size(); ++j) {
                if (std::abs(t[j] - t[i]) <= width / 2.0 && !std::isnan(v[j])) {
                    sum += v[j];
                    ++cnt;
                }
            }
            if (cnt == 0) {
                QVERIFY(std::isnan(out[i]));
            } else {
                QVERIFY2(near(out[i], sum / cnt, 1e-9), qPrintable(QString::number(i)));
            }
        }
    }

    // ---- referenceLevel -----------------------------------------------------------------
    void reference_cursorValue()
    {
        Recorder rec;
        for (int i = 0; i < 10; ++i) {
            rec.append(batch(i, {good(kPv, i * 10.0)}));
        }
        const auto v = referenceLevel(rec, kPv, 4.5, ReferenceMode::CursorValue, 1.0);
        QVERIFY(v.has_value());
        QCOMPARE(*v, 45.0); // interpolated between 40 (t = 4) and 50 (t = 5)
        QCOMPARE(*referenceLevel(rec, kPv, 4.0, ReferenceMode::CursorValue, 1.0), 40.0);
        QCOMPARE(*referenceLevel(rec, kPv, 20.0, ReferenceMode::CursorValue, 1.0), 90.0); // after the last row
        QVERIFY(!referenceLevel(rec, kPv, -1.0, ReferenceMode::CursorValue, 1.0).has_value());
        QVERIFY(!referenceLevel(rec, 99, 4.0, ReferenceMode::CursorValue, 1.0).has_value());
    }

    void reference_windowMean()
    {
        Recorder rec;
        for (int i = 0; i < 10; ++i) {
            rec.append(batch(i, {good(kPv, double(i))}));
        }
        const auto v = referenceLevel(rec, kPv, 5.0, ReferenceMode::WindowMean, 2.0);
        QVERIFY(v.has_value());
        QVERIFY(near(*v, 4.0, 1e-12)); // rows 3, 4, 5
    }

    void reference_nanAtCursor()
    {
        Recorder rec;
        rec.append(batch(0, {good(kPv, 1.0)}));
        rec.append(batch(1, {good(kPv, 2.0)}));
        rec.append(batch(2, {bad(kPv)}));
        QVERIFY(!referenceLevel(rec, kPv, 2.0, ReferenceMode::CursorValue, 1.0).has_value());
        // The window mean skips the NaN sample.
        const auto m = referenceLevel(rec, kPv, 2.0, ReferenceMode::WindowMean, 2.0);
        QVERIFY(m.has_value());
        QVERIFY(near(*m, 1.5, 1e-12));
        // A window holding only NaN yields nothing.
        QVERIFY(!referenceLevel(rec, kPv, 2.0, ReferenceMode::WindowMean, 0.5).has_value());
    }

    void reference_emptyWindow()
    {
        Recorder rec;
        for (int i = 0; i < 5; ++i) {
            rec.append(batch(i, {good(kPv, double(i))}));
        }
        QVERIFY(!referenceLevel(rec, kPv, 100.0, ReferenceMode::WindowMean, 1.0).has_value());
        QVERIFY(!referenceLevel(rec, kPv, -5.0, ReferenceMode::WindowMean, 1.0).has_value());
        QVERIFY(!referenceLevel(rec, kPv, 2.5, ReferenceMode::WindowMean, 0.4).has_value());
        QVERIFY(!referenceLevel(rec, kPv, kNaN, ReferenceMode::CursorValue, 1.0).has_value());
    }

    // ---- findCrossingTime ---------------------------------------------------------------
    void crossing_rising()
    {
        const QList<double> t = range(6);
        const QList<double> v{0, 0, 0, 10, 10, 10};
        const auto c = findCrossingTime(t, v, 0.0, 10.0, 63.2);
        QVERIFY(c.has_value());
        QVERIFY(near(*c, 2.632, 1e-9));
        const auto c50 = findCrossingTime(t, v, 0.0, 10.0, 50.0);
        QVERIFY(c50.has_value());
        QVERIFY(near(*c50, 2.5, 1e-9));
    }

    void crossing_falling()
    {
        const QList<double> t = range(6);
        const QList<double> v{10, 10, 10, 0, 0, 0};
        const auto c = findCrossingTime(t, v, 10.0, 0.0, 63.2);
        QVERIFY(c.has_value());
        QVERIFY(near(*c, 2.632, 1e-9));
        const auto c10 = findCrossingTime(t, v, 10.0, 0.0, 10.0);
        QVERIFY(c10.has_value());
        QVERIFY(near(*c10, 2.1, 1e-9));
    }

    void crossing_exactOnTarget()
    {
        const QList<double> t = range(5);
        const QList<double> v{0, 2, 5, 8, 10};
        QCOMPARE(findCrossingTime(t, v, 0.0, 10.0, 50.0).value(), 2.0);
        QCOMPARE(findCrossingTime(t, v, 0.0, 10.0, 20.0).value(), 1.0);
        QCOMPARE(findCrossingTime(t, v, 0.0, 10.0, 80.0).value(), 3.0);
        QCOMPARE(findCrossingTime(t, v, 0.0, 10.0, 100.0).value(), 4.0);
        QCOMPARE(findCrossingTime(t, v, 0.0, 10.0, 0.0).value(), 0.0);
    }

    void crossing_interpolation()
    {
        const QList<double> t = range(5);
        const QList<double> v{0, 2, 5, 8, 10};
        QVERIFY(near(findCrossingTime(t, v, 0.0, 10.0, 30.0).value(), 1.0 + 1.0 / 3.0, 1e-9));
        QVERIFY(near(findCrossingTime(t, v, 0.0, 10.0, 90.0).value(), 3.5, 1e-9));
        QVERIFY(near(findCrossingTime(t, v, 0.0, 10.0, 65.0).value(), 2.0 + 1.5 / 3.0, 1e-9));
    }

    void crossing_zeroPercentOnFlatBaseline()
    {
        const QList<double> t = range(7);
        const QList<double> v{0, 0, 0, 0, 3, 10, 10};
        // Last baseline sample before the rise.
        QCOMPARE(findCrossingTime(t, v, 0.0, 10.0, 0.0).value(), 3.0);
        // Falling: same on the mirrored data.
        const QList<double> w{10, 10, 10, 10, 7, 0, 0};
        QCOMPARE(findCrossingTime(t, w, 10.0, 0.0, 0.0).value(), 3.0);
    }

    void crossing_zeroPercentOnNoisyBaseline()
    {
        std::mt19937 gen(7);
        std::normal_distribution<double> noise(0.0, 0.05);
        QList<double> t;
        QList<double> v;
        for (int i = 0; i <= 300; ++i) {
            const double ti = i / 10.0;
            t.append(ti);
            v.append(fopdt(ti) + noise(gen));
        }
        const auto c = findCrossingTime(t, v, 10.0, 30.0, 0.0);
        QVERIFY(c.has_value());
        const double trueStart = 10.0 + kL;
        QVERIFY2(*c > trueStart - 1.0 && *c < trueStart + 0.3, qPrintable(QString::number(*c)));
    }

    void crossing_notReached()
    {
        const QList<double> t = range(5);
        // 90 % is never reached although the 50 % anchor exists.
        const QList<double> v{0, 5, 6, 7, 8};
        QCOMPARE(findCrossingTime(t, v, 0.0, 10.0, 50.0).value(), 1.0);
        QVERIFY(!findCrossingTime(t, v, 0.0, 10.0, 90.0).has_value());
    }

    void crossing_noAnchor()
    {
        const QList<double> t = range(4);
        const QList<double> v{0, 2, 4, 4.5};
        QVERIFY(!findCrossingTime(t, v, 0.0, 10.0, 0.0).has_value());
        QVERIFY(!findCrossingTime(t, v, 0.0, 10.0, 63.2).has_value());
        QVERIFY(!findCrossingTime({}, {}, 0.0, 10.0, 63.2).has_value());
        QVERIFY(!findCrossingTime(t, v, 5.0, 5.0, 63.2).has_value());
    }

    void crossing_nanSkipped()
    {
        const QList<double> t = range(3);
        const QList<double> v{0, kNaN, 10};
        QVERIFY(near(findCrossingTime(t, v, 0.0, 10.0, 63.2).value(), 1.264, 1e-9));
        QCOMPARE(findCrossingTime(t, v, 0.0, 10.0, 0.0).value(), 0.0);
        // Leading NaN: the first valid sample is already past the target.
        const QList<double> w{kNaN, 10, 10};
        QCOMPARE(findCrossingTime(t, w, 0.0, 10.0, 63.2).value(), 1.0);
    }

    void crossing_firstSampleAlreadyThere()
    {
        const QList<double> t = range(2);
        const QList<double> v{10, 10};
        QCOMPARE(findCrossingTime(t, v, 0.0, 10.0, 63.2).value(), 0.0);
        // Nothing before the anchor for a percent below 50.
        QVERIFY(!findCrossingTime(t, v, 0.0, 10.0, 20.0).has_value());
    }

    // ---- analyzeStep --------------------------------------------------------------------
    void analyze_cleanFopdt()
    {
        const Recorder rec = makeProcess(60.0);
        AnalysisSettings s;
        s.deltaCoTagId = kCo;
        const StepResult r = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY2(r.valid, qPrintable(r.error));
        QCOMPARE(r.tagId, kPv);
        QCOMPARE(r.t1, 9.0);
        QCOMPARE(r.t2, 60.0);
        QVERIFY(near(r.y0, 10.0, 1e-12));
        QVERIFY(near(r.y100, 30.0, 1e-3));
        QVERIFY(near(r.deltaPv, 20.0, 1e-3));
        QCOMPARE(r.setpoints.size(), 2);
        QCOMPARE(r.setpoints[0].percent, 0.0);
        QCOMPARE(r.setpoints[1].percent, 63.2);
        QVERIFY(r.setpoints[0].found);
        QVERIFY(r.setpoints[1].found);
        QVERIFY(near(r.setpoints[0].time, 10.0 + kL, 1e-9));
        QVERIFY2(near(r.setpoints[1].time, 10.0 + kL + kT, 0.05),
                 qPrintable(QString::number(r.setpoints[1].time)));
        QVERIFY(near(r.setpoints[1].target, r.y0 + 0.632 * r.deltaPv, 1e-9));
        QVERIFY(near(r.setpoints[1].measured, r.setpoints[1].target, 1e-9));

        QVERIFY(r.deltaCo.has_value());
        QVERIFY(near(*r.deltaCo, 10.0, 1e-12));
        QVERIFY(r.gain.has_value());
        QVERIFY(near(*r.gain, 2.0, 1e-3));
        QVERIFY(near(r.stepTime, 10.0, 1e-9));
        QVERIFY(r.deadTime.has_value());
        QVERIFY(r.timeConstant.has_value());
        QVERIFY2(near(*r.deadTime, kL, 0.02 * kL), qPrintable(QString::number(*r.deadTime)));
        QVERIFY2(near(*r.timeConstant, kT, 0.02 * kT),
                 qPrintable(QString::number(*r.timeConstant)));
    }

    void analyze_smithMethod()
    {
        const Recorder rec = makeProcess(60.0);
        AnalysisSettings s;
        s.percents = {28.3, 63.2};
        s.method = ModelMethod::Custom;
        s.modelPercentA = 28.3;
        s.modelPercentB = 63.2;
        s.deltaCoTagId = kCo;
        const StepResult r = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY2(r.valid, qPrintable(r.error));
        QVERIFY(r.deadTime.has_value() && r.timeConstant.has_value());
        QVERIFY2(near(*r.deadTime, kL, 0.02 * kL), qPrintable(QString::number(*r.deadTime)));
        QVERIFY2(near(*r.timeConstant, kT, 0.02 * kT),
                 qPrintable(QString::number(*r.timeConstant)));
        // The model percents need not be among the requested ones.
        AnalysisSettings s2 = s;
        s2.percents = {50.0};
        const StepResult r2 = analyzeStep(rec, kPv, 9.0, 60.0, s2);
        QCOMPARE(r2.setpoints.size(), 1);
        QVERIFY(r2.timeConstant.has_value());
        QVERIFY(near(*r2.timeConstant, *r.timeConstant, 1e-12));
    }

    void analyze_noisyWithWindowMeanAndSmoothing()
    {
        const Recorder rec = makeProcess(60.0, 1, 0.2, 12345);
        AnalysisSettings s;
        s.percents = {10.0, 63.2};
        s.reference = ReferenceMode::WindowMean;
        s.referenceWindowS = 1.0;
        s.smoothingS = 1.0;
        s.method = ModelMethod::Custom;
        s.modelPercentA = 10.0;
        s.modelPercentB = 63.2;
        s.deltaCoTagId = kCo;
        const StepResult r = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY2(r.valid, qPrintable(r.error));
        QVERIFY(near(r.y0, 10.0, 0.3));
        QVERIFY(near(r.y100, 30.0, 0.3));
        QVERIFY(r.gain.has_value());
        QVERIFY(near(*r.gain, 2.0, 0.1));
        QVERIFY(near(r.stepTime, 10.0, 1e-9));
        QVERIFY(r.deadTime.has_value() && r.timeConstant.has_value());
        QVERIFY2(near(*r.deadTime, kL, 0.05 * kL), qPrintable(QString::number(*r.deadTime)));
        QVERIFY2(near(*r.timeConstant, kT, 0.05 * kT),
                 qPrintable(QString::number(*r.timeConstant)));
        // `measured` is the raw value, so it only roughly matches the target.
        QVERIFY(r.setpoints[1].found);
        QVERIFY(near(r.setpoints[1].measured, r.setpoints[1].target, 1.0));
    }

    void analyze_noisyDefaultPercents()
    {
        const Recorder rec = makeProcess(60.0, 1, 0.1, 99);
        AnalysisSettings s;
        s.reference = ReferenceMode::WindowMean;
        s.smoothingS = 0.5;
        s.deltaCoTagId = kCo;
        const StepResult r = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY2(r.valid, qPrintable(r.error));
        QVERIFY(r.setpoints[1].found);
        QVERIFY2(near(r.setpoints[1].time, 10.0 + kL + kT, 0.3),
                 qPrintable(QString::number(r.setpoints[1].time)));
    }

    void analyze_manualDeltaCo()
    {
        const Recorder rec = makeProcess(60.0);
        AnalysisSettings s;
        s.manualDeltaCo = 5.0;
        const StepResult r = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY2(r.valid, qPrintable(r.error));
        QCOMPARE(r.stepTime, 9.0);
        QVERIFY(r.deltaCo.has_value());
        QCOMPARE(*r.deltaCo, 5.0);
        QVERIFY(r.gain.has_value());
        QVERIFY(near(*r.gain, 4.0, 1e-3));
        QVERIFY(r.deadTime.has_value());
        QVERIFY(near(*r.deadTime, 2.5, 0.05));
        QVERIFY(near(*r.timeConstant, kT, 0.08));
    }

    void analyze_noDeltaCo()
    {
        const Recorder rec = makeProcess(60.0);
        AnalysisSettings s; // no tag, manual 0
        StepResult r = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY(r.valid);
        QVERIFY(!r.deltaCo.has_value());
        QVERIFY(!r.gain.has_value());
        QCOMPARE(r.stepTime, 9.0);
        QVERIFY(r.deadTime.has_value());

        // A ΔCO tag that is not recorded counts as "no ΔCO"; so does the PV tag itself.
        s.deltaCoTagId = 77;
        r = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY(r.valid);
        QVERIFY(!r.deltaCo.has_value());
        QVERIFY(!r.gain.has_value());
        QCOMPARE(r.stepTime, 9.0);
        s.deltaCoTagId = kPv;
        r = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY(!r.deltaCo.has_value());
    }

    void analyze_fallingStep()
    {
        const Recorder rec = makeProcess(60.0, -1);
        AnalysisSettings s;
        s.deltaCoTagId = kCo;
        const StepResult r = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY2(r.valid, qPrintable(r.error));
        QVERIFY(near(r.y0, 30.0, 1e-12));
        QVERIFY(r.deltaPv < 0.0);
        QVERIFY(r.gain.has_value());
        QVERIFY(near(*r.gain, -2.0, 1e-3));
        QVERIFY(near(r.setpoints[1].time, 10.0 + kL + kT, 0.05));
        QVERIFY(near(*r.deadTime, kL, 0.02 * kL));
        QVERIFY(near(*r.timeConstant, kT, 0.02 * kT));
        QVERIFY(near(r.setpoints[1].measured, r.setpoints[1].target, 1e-9));
    }

    void analyze_fallingCo()
    {
        // CO falls 30 -> 20 at t = 10 s, PV falls too: positive K, stepTime still 10.
        Recorder rec;
        for (int i = 0; i <= 600; ++i) {
            const double t = i / 10.0;
            const double co = t < 10.0 ? 30.0 : (t == 10.0 ? 25.0 : 20.0);
            rec.append(batch(t, {good(kPv, fopdt(t, -1)), good(kCo, co)}));
        }
        AnalysisSettings s;
        s.deltaCoTagId = kCo;
        const StepResult r = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY(r.valid);
        QVERIFY(near(*r.deltaCo, -10.0, 1e-12));
        QVERIFY(near(*r.gain, 2.0, 1e-3));
        QVERIFY(near(r.stepTime, 10.0, 1e-9));
    }

    void analyze_reverseCursors()
    {
        const Recorder rec = makeProcess(60.0);
        AnalysisSettings s;
        s.deltaCoTagId = kCo;
        const StepResult a = analyzeStep(rec, kPv, 9.0, 60.0, s);
        const StepResult b = analyzeStep(rec, kPv, 60.0, 9.0, s);
        QVERIFY(b.valid);
        QCOMPARE(b.t1, 9.0);
        QCOMPARE(b.t2, 60.0);
        QCOMPARE(b.y0, a.y0);
        QCOMPARE(b.y100, a.y100);
        QCOMPARE(b.setpoints.size(), a.setpoints.size());
        QCOMPARE(b.setpoints[1].time, a.setpoints[1].time);
        QCOMPARE(*b.deadTime, *a.deadTime);
        QCOMPARE(*b.timeConstant, *a.timeConstant);
        QCOMPARE(*b.gain, *a.gain);

        const auto in = analyzeStepInput(rec, kCo, 60.0, 9.0, s);
        QVERIFY(in.has_value());
        QVERIFY(near(in->delta, 10.0, 1e-12));
        QVERIFY(near(in->stepTime, 10.0, 1e-9));
    }

    void analyze_percentOrderKept()
    {
        const Recorder rec = makeProcess(60.0);
        AnalysisSettings s;
        s.percents = {90.0, 0.0, 50.0};
        const StepResult r = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY(r.valid);
        QCOMPARE(r.setpoints.size(), 3);
        QCOMPARE(r.setpoints[0].percent, 90.0);
        QCOMPARE(r.setpoints[1].percent, 0.0);
        QCOMPARE(r.setpoints[2].percent, 50.0);
        QVERIFY(r.setpoints[0].time > r.setpoints[2].time);
        QVERIFY(r.setpoints[2].time > r.setpoints[1].time);
    }

    void analyze_percentNotReached()
    {
        const Recorder rec = makeProcess(60.0);
        AnalysisSettings s;
        s.percents = {50.0, 99.0};
        s.modelPercentA = 0.0;
        s.modelPercentB = 63.2;
        const StepResult r = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY(r.setpoints[0].found);
        QVERIFY(r.setpoints[1].found);

        // Ideal step: the 50 % crossing is interpolated between the samples at 9.9 s and 10 s.
        Recorder partial;
        for (int i = 0; i <= 200; ++i) {
            const double t = i / 10.0;
            partial.append(batch(t, {good(kPv, t < 10 ? 0.0 : 10.0)}));
        }
        partial.append(batch(20.1, {good(kPv, 10.0)}));
        AnalysisSettings s2;
        s2.percents = {50.0};
        const StepResult r2 = analyzeStep(partial, kPv, 5.0, 20.0, s2);
        QVERIFY(r2.valid);
        QVERIFY(r2.setpoints[0].found);
        QVERIFY(near(r2.setpoints[0].measured, r2.setpoints[0].target, 1e-9));
    }

    void analyze_invalidCases()
    {
        const Recorder rec = makeProcess(60.0);
        AnalysisSettings s;

        StepResult r = analyzeStep(rec, kPv, kNaN, 40.0, s);
        QVERIFY(!r.valid);
        QCOMPARE(r.error, QStringLiteral("Place both cursors"));
        QCOMPARE(r.tagId, kPv);
        r = analyzeStep(rec, kPv, 9.0, kNaN, s);
        QVERIFY(!r.valid);
        QCOMPARE(r.error, QStringLiteral("Place both cursors"));

        // Flat PV between the cursors.
        r = analyzeStep(rec, kPv, 2.0, 8.0, s);
        QVERIFY(!r.valid);
        QCOMPARE(r.error, QStringLiteral("No change between C1 and C2"));
        QCOMPARE(r.t1, 2.0);
        QCOMPARE(r.t2, 8.0);

        // Cursors before the recorded data.
        r = analyzeStep(rec, kPv, -5.0, 40.0, s);
        QVERIFY(!r.valid);
        QCOMPARE(r.error, QStringLiteral("No data at C1"));

        // Unknown tag.
        r = analyzeStep(rec, 55, 9.0, 40.0, s);
        QVERIFY(!r.valid);
        QCOMPARE(r.error, QStringLiteral("No data at C1"));
    }

    void analyze_nanAtCursor()
    {
        Recorder rec;
        for (int i = 0; i <= 600; ++i) {
            const double t = i / 10.0;
            const bool nan1 = i == 90;  // 9.0 s
            const bool nan2 = i == 400; // 40.0 s
            rec.append(batch(t, {nan1 || nan2 ? bad(kPv) : good(kPv, fopdt(t))}));
        }
        AnalysisSettings s;
        StepResult r = analyzeStep(rec, kPv, 9.0, 50.0, s);
        QVERIFY(!r.valid);
        QCOMPARE(r.error, QStringLiteral("No data at C1"));
        r = analyzeStep(rec, kPv, 8.0, 40.0, s);
        QVERIFY(!r.valid);
        QCOMPARE(r.error, QStringLiteral("No data at C2"));

        // With a reference window the NaN sample is skipped and the analysis works.
        s.reference = ReferenceMode::WindowMean;
        s.referenceWindowS = 1.0;
        r = analyzeStep(rec, kPv, 9.0, 50.0, s);
        QVERIFY2(r.valid, qPrintable(r.error));
        r = analyzeStep(rec, kPv, 8.0, 40.0, s);
        QVERIFY2(r.valid, qPrintable(r.error));
        QVERIFY(r.setpoints[1].found);
    }

    void analyze_stepInput()
    {
        const Recorder rec = makeProcess(60.0);
        AnalysisSettings s;
        const auto in = analyzeStepInput(rec, kCo, 9.0, 40.0, s);
        QVERIFY(in.has_value());
        QVERIFY(near(in->delta, 10.0, 1e-12));
        QVERIFY(near(in->stepTime, 10.0, 1e-9));

        // No change, missing data, unknown tag, NaN cursor.
        QVERIFY(!analyzeStepInput(rec, kCo, 1.0, 5.0, s).has_value());
        QVERIFY(!analyzeStepInput(rec, kCo, -5.0, 40.0, s).has_value());
        QVERIFY(!analyzeStepInput(rec, 123, 9.0, 40.0, s).has_value());
        QVERIFY(!analyzeStepInput(rec, kCo, kNaN, 40.0, s).has_value());

        // ΔCO window mean.
        s.reference = ReferenceMode::WindowMean;
        s.referenceWindowS = 1.0;
        const auto in2 = analyzeStepInput(rec, kCo, 9.0, 40.0, s);
        QVERIFY(in2.has_value());
        QVERIFY(near(in2->delta, 10.0, 1e-12));
    }

    // Regression: when every baseline sample after C1 sits slightly above the 0 % level
    // (noise, drift), 0 % must still be found — at C1 — instead of "not found".
    void analyze_zeroPercentWhenBaselineStaysAboveCursorSample()
    {
        Recorder rec;
        rec.append(batch(0.0, {good(kPv, 0.0)}));
        for (int i = 1; i <= 5; ++i)
            rec.append(batch(i / 10.0, {good(kPv, 0.2)}));
        for (int i = 6; i <= 40; ++i)
            rec.append(batch(i / 10.0, {good(kPv, 10.0)}));

        AnalysisSettings s;
        s.percents = {0.0};
        StepResult r = analyzeStep(rec, kPv, 0.05, 4.0, s);
        QVERIFY(r.valid);
        QCOMPARE(r.y0, 0.1); // interpolated between 0 (t = 0) and 0.2 (t = 0.1)
        QVERIFY(r.setpoints.at(0).found);
        QCOMPARE(r.setpoints.at(0).time, 0.05);

        // Mean before cursor: the window before C1 is searched too.
        s.reference = ReferenceMode::WindowMean;
        s.referenceWindowS = 0.3;
        r = analyzeStep(rec, kPv, 0.25, 4.0, s); // y0 = mean(0, 0.2, 0.2) ≈ 0.133
        QVERIFY(r.valid);
        QVERIFY(r.setpoints.at(0).found);
        QCOMPARE(r.setpoints.at(0).time, 0.25);
    }

    // Regression (user report): C1 placed on a rising part of the response, between two
    // samples. 0 % must be C1 itself, never the earlier sample; every time stays in [C1, C2].
    void analyze_timesStayBetweenCursors()
    {
        Recorder rec;
        for (int i = 0; i <= 20; ++i)
            rec.append(batch(i * 0.2, {good(kPv, i * 0.2 * 100.0)})); // ramp 100/s, 0.2 s apart
        AnalysisSettings s;
        s.percents = {0.0, 28.3, 63.2, 100.0};
        for (const ReferenceMode mode : {ReferenceMode::CursorValue, ReferenceMode::WindowMean}) {
            s.reference = mode;
            const StepResult r = analyzeStep(rec, kPv, 1.1, 3.0, s);
            QVERIFY(r.valid);
            for (const SetpointResult &sp : r.setpoints) {
                QVERIFY2(sp.found, qPrintable(QString::number(sp.percent)));
                QVERIFY2(sp.time >= 1.1 - 1e-12 && sp.time <= 3.0 + 1e-12,
                         qPrintable(QStringLiteral("%1 %% at %2").arg(sp.percent).arg(sp.time)));
            }
            QCOMPARE(r.setpoints.at(0).time, 1.1);
            if (mode == ReferenceMode::CursorValue) {
                QCOMPARE(r.y0, 110.0); // interpolated at C1
                QCOMPARE(r.setpoints.at(0).measured, 110.0);
                QCOMPARE(r.setpoints.at(3).time, 3.0);
            }
        }
    }

    void analyze_modelNeedsValidPercents()
    {
        const Recorder rec = makeProcess(60.0);
        AnalysisSettings s;
        s.method = ModelMethod::Custom;
        s.modelPercentA = 63.2;
        s.modelPercentB = 28.3; // a >= b
        const StepResult r = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY(r.valid);
        QVERIFY(!r.deadTime.has_value());
        QVERIFY(!r.timeConstant.has_value());
        QVERIFY(!r.fitRms.has_value());
        QVERIFY(!r.fitRmsPercent.has_value());
    }

    // ---- fopdtModelValue ----------------------------------------------------------------
    void model_valueOnHandBuiltResult()
    {
        StepResult r;
        r.valid = true;
        r.y0 = 10.0;
        r.deltaPv = 20.0;
        r.stepTime = 10.0;
        r.deadTime = 1.5;
        r.timeConstant = 4.0;
        // Before and at stepTime + L the model sits at y0.
        QCOMPARE(fopdtModelValue(r, 5.0), 10.0);
        QCOMPARE(fopdtModelValue(r, 10.0), 10.0);
        QCOMPARE(fopdtModelValue(r, 11.5), 10.0);
        // One time constant after stepTime + L: 63.2 % of the change.
        QVERIFY(near(fopdtModelValue(r, 11.5 + 4.0), 10.0 + 20.0 * (1.0 - std::exp(-1.0)), 1e-9));
        QVERIFY(near(fopdtModelValue(r, 11.5 + 4.0), 22.6424, 1e-4));
        // Far after the step it settles at y100.
        QVERIFY(near(fopdtModelValue(r, 200.0), 30.0, 1e-9));
        // Falling step.
        r.y0 = 30.0;
        r.deltaPv = -20.0;
        QVERIFY(near(fopdtModelValue(r, 11.5 + 4.0), 30.0 - 20.0 * (1.0 - std::exp(-1.0)), 1e-9));
        QVERIFY(near(fopdtModelValue(r, 200.0), 10.0, 1e-9));
    }

    void model_nanWithoutModel()
    {
        StepResult r;
        r.valid = true;
        r.y0 = 10.0;
        r.deltaPv = 20.0;
        r.stepTime = 10.0;
        r.deadTime = 1.5;
        r.timeConstant = 4.0;
        QVERIFY(!std::isnan(fopdtModelValue(r, 15.0)));

        StepResult invalid = r;
        invalid.valid = false;
        QVERIFY(std::isnan(fopdtModelValue(invalid, 15.0)));

        StepResult noDead = r;
        noDead.deadTime.reset();
        QVERIFY(std::isnan(fopdtModelValue(noDead, 15.0)));

        StepResult noTau = r;
        noTau.timeConstant.reset();
        QVERIFY(std::isnan(fopdtModelValue(noTau, 15.0)));
    }

    void analyze_fitRmsCleanAndNoisy()
    {
        // Clean data: the model reproduces the response almost exactly.
        const Recorder clean = makeProcess(60.0);
        AnalysisSettings s;
        s.deltaCoTagId = kCo;
        const StepResult c = analyzeStep(clean, kPv, 9.0, 60.0, s);
        QVERIFY(c.valid);
        QVERIFY(c.fitRms.has_value());
        QVERIFY(c.fitRmsPercent.has_value());
        QVERIFY2(*c.fitRms < 0.01 * std::abs(c.deltaPv), qPrintable(QString::number(*c.fitRms)));
        QVERIFY(near(*c.fitRmsPercent, *c.fitRms / std::abs(c.deltaPv) * 100.0, 1e-9));

        // Noisy data (sigma 0.2): the residual is about the noise standard deviation.
        const Recorder noisy = makeProcess(60.0, 1, 0.2, 12345);
        AnalysisSettings n;
        n.reference = ReferenceMode::WindowMean;
        n.referenceWindowS = 1.0;
        n.smoothingS = 1.0;
        n.method = ModelMethod::Custom;
        n.modelPercentA = 10.0;
        n.modelPercentB = 63.2;
        n.deltaCoTagId = kCo;
        const StepResult nr = analyzeStep(noisy, kPv, 9.0, 60.0, n);
        QVERIFY2(nr.valid, qPrintable(nr.error));
        QVERIFY(nr.fitRms.has_value());
        QVERIFY2(near(*nr.fitRms, 0.2, 0.2 * 0.3), qPrintable(QString::number(*nr.fitRms)));
        QVERIFY(near(*nr.fitRmsPercent, *nr.fitRms / std::abs(nr.deltaPv) * 100.0, 1e-9));
    }

    // ---- model methods ------------------------------------------------------------------
    void methods_smithSundaresanCustom()
    {
        const Recorder rec = makeProcess(80.0);
        const auto run = [&](ModelMethod m, double a = 0.0, double b = 63.2) {
            AnalysisSettings s;
            s.method = m;
            s.modelPercentA = a;
            s.modelPercentB = b;
            s.percents = {50.0}; // model percents are not requested setpoints
            s.deltaCoTagId = kCo;
            return analyzeStep(rec, kPv, 9.0, 80.0, s);
        };
        const QList<StepResult> results{run(ModelMethod::Smith), run(ModelMethod::Sundaresan),
                                        run(ModelMethod::Custom, 20.0, 80.0)};
        for (const StepResult &r : results) {
            QVERIFY2(r.valid, qPrintable(r.error));
            QVERIFY(r.deadTime && r.timeConstant);
            QVERIFY2(near(*r.deadTime, kL, 0.03 * kL), qPrintable(QString::number(*r.deadTime)));
            QVERIFY2(near(*r.timeConstant, kT, 0.03 * kT),
                     qPrintable(QString::number(*r.timeConstant)));
        }
        // Custom uses A/B; other methods ignore them.
        const StepResult smithIgnoring = run(ModelMethod::Smith, 20.0, 80.0);
        QVERIFY(near(*smithIgnoring.timeConstant, *results[0].timeConstant, 1e-12));
        QVERIFY(std::abs(*results[2].timeConstant - *results[0].timeConstant) > 1e-9);
        // Default is unchanged by A/B.
        const StepResult d1 = run(ModelMethod::ZeroTo632);
        const StepResult d2 = run(ModelMethod::ZeroTo632, 50.0, 90.0);
        QVERIFY(near(*d1.timeConstant, *d2.timeConstant, 1e-12));
        QVERIFY(near(*d1.deadTime, *d2.deadTime, 1e-12));
        QVERIFY(near(*d1.timeConstant, kT, 0.03 * kT));
    }

    // ---- findStep -----------------------------------------------------------------------
    void findStep_clean()
    {
        const Recorder rec = build(60.0, [](double t) { return fopdt(t); }, cleanCo);
        const auto w = findStep(rec, kCo, kPv, 0.0, 60.0);
        QVERIFY(w.has_value());
        QVERIFY(near(w->stepTime, 10.0, 1e-9));
        QVERIFY(near(w->c1, 5.0, 1e-9)); // half of the 10 s gap to `from`
        QVERIFY(w->settled);
        // settles at about 27.2 s; c2 = ts + 0.2 (ts - stepTime)
        QVERIFY2(near(w->c2, 30.6, 0.5), qPrintable(QString::number(w->c2)));

        const auto near1 = findStep(rec, kCo, kPv, 8.0, 60.0);
        QVERIFY(near1 && near(near1->c1, 9.0, 1e-9)); // half-gap rule

        // 5 s cap: step at 40 s, `from` at 0.
        const Recorder wide = build(80.0, [](double t) { return fopdt(t - 30.0); },
                                    [](double t) { return t < 40.0 ? 20.0 : 30.0; });
        const auto capped = findStep(wide, kCo, kPv, 0.0, 80.0);
        QVERIFY(capped && near(capped->stepTime, 40.0, 1e-9) && near(capped->c1, 35.0, 1e-9));
        QVERIFY(capped->settled);
    }

    void findStep_previousChangeLimitsC1()
    {
        const Recorder rec = build(60.0, [](double t) { return fopdt(t); }, [](double t) {
            return t < 6.0 ? 15.0 : (t < 10.0 ? 20.0 : 30.0);
        });
        const auto w = findStep(rec, kCo, kPv, 0.0, 60.0);
        QVERIFY(w && near(w->stepTime, 10.0, 1e-9));
        QVERIFY2(near(w->c1, 8.0, 1e-9), qPrintable(QString::number(w->c1))); // (10 - 6) / 2
    }

    void findStep_c1NeverAfterPreStepSample()
    {
        // Slow sampling: one row every 10 s, CO 20 -> 30 between the rows at 10 s and 20 s.
        Recorder slow;
        for (int i = 0; i <= 10; ++i) {
            const double t = 10.0 * i;
            slow.append(batch(t, {good(kPv, fopdt(t - 10.0)), good(kCo, t < 20.0 ? 20.0 : 30.0)}));
        }
        const auto w = findStep(slow, kCo, kPv, 0.0, 100.0);
        QVERIFY(w.has_value());
        QVERIFY(near(w->stepTime, 20.0, 1e-9));
        QVERIFY2(near(w->c1, 10.0, 1e-9), qPrintable(QString::number(w->c1))); // not 15 s
        AnalysisSettings s;
        s.deltaCoTagId = kCo;
        const StepResult r = analyzeStep(slow, kPv, w->c1, w->c2, s);
        QVERIFY2(r.valid, qPrintable(r.error));
        QVERIFY(r.deltaCo.has_value());
        QVERIFY2(near(*r.deltaCo, 10.0, 1e-9), qPrintable(QString::number(*r.deltaCo)));

        // Two-sample transition (25 at 10.0 s, 30 from 10.1 s): the previous change is the
        // immediately preceding row, so C1 is that row, not the midpoint to it.
        const Recorder two = build(60.0, [](double t) { return fopdt(t); }, coStep);
        const auto t2 = findStep(two, kCo, kPv, 0.0, 60.0);
        QVERIFY(t2.has_value());
        QVERIFY(near(t2->stepTime, 10.1, 1e-9));
        QVERIFY2(near(t2->c1, 10.0, 1e-9), qPrintable(QString::number(t2->c1)));
    }

    void findStep_largestAndTie()
    {
        const auto pv = [](double t) { return fopdt(t); };
        const Recorder bigFirst = build(100.0, pv, [](double t) {
            return t < 10.0 ? 20.0 : (t < 50.0 ? 30.0 : 35.0);
        });
        const auto a = findStep(bigFirst, kCo, kPv, 0.0, 100.0);
        QVERIFY(a && near(a->stepTime, 10.0, 1e-9));

        const Recorder tie = build(100.0, pv, [](double t) {
            return t < 10.0 ? 20.0 : (t < 50.0 ? 30.0 : 40.0);
        });
        const auto b = findStep(tie, kCo, kPv, 0.0, 100.0);
        QVERIFY(b && near(b->stepTime, 50.0, 1e-9));
        QVERIFY(near(b->c1, 45.0, 1e-9)); // half of 40 s capped at 5 s
    }

    void findStep_notSettledAndEndBound()
    {
        const Recorder rec = build(60.0, ramp, cleanCo);
        const auto w = findStep(rec, kCo, kPv, 0.0, 60.0);
        QVERIFY(w.has_value());
        QVERIFY(!w->settled);
        QVERIFY(near(w->c2, 60.0, 1e-9));

        // A later small CO change bounds `end` at the sample before it.
        const Recorder bounded = build(60.0, ramp, [](double t) {
            return t < 10.0 ? 20.0 : (t < 40.0 ? 30.0 : 30.5);
        });
        const auto b = findStep(bounded, kCo, kPv, 0.0, 60.0);
        QVERIFY(b && near(b->stepTime, 10.0, 1e-9));
        QVERIFY(!b->settled);
        QVERIFY2(near(b->c2, 39.9, 1e-9), qPrintable(QString::number(b->c2)));
    }

    void findStep_noStep()
    {
        const Recorder flat = build(30.0, [](double t) { return fopdt(t); },
                                    [](double) { return 20.0; });
        QVERIFY(!findStep(flat, kCo, kPv, 0.0, 30.0).has_value());
        const Recorder rec = build(30.0, [](double t) { return fopdt(t); }, cleanCo);
        QVERIFY(!findStep(rec, kCo, 77, 0.0, 30.0).has_value());   // PV without samples
        QVERIFY(!findStep(rec, 77, kPv, 0.0, 30.0).has_value());   // CO without samples
        QVERIFY(!findStep(rec, kCo, kPv, 20.0, 30.0).has_value()); // step outside the range
        QVERIFY(!findStep(rec, kCo, kPv, 30.0, 10.0).has_value()); // from >= to
    }

    // ---- checkStep ----------------------------------------------------------------------
    void check_cleanAndInvalid()
    {
        const Recorder rec = makeProcess(60.0);
        AnalysisSettings s;
        s.deltaCoTagId = kCo;
        const StepResult r = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY(r.valid);
        QVERIFY2(checkStep(rec, r).isEmpty(), "clean response gives no checks");
        StepResult inv;
        QVERIFY(checkStep(rec, inv).isEmpty());
        QVERIFY(checkStep(rec, analyzeStep(rec, kPv, 20.0, 20.0, s)).isEmpty());
    }

    void check_noDeltaCoAndNoModel()
    {
        const Recorder rec = makeProcess(60.0);
        AnalysisSettings s; // no CO tag, no manual delta
        const StepResult r = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY(r.valid && r.deadTime);
        QVERIFY(checkStep(rec, r) == QList<StepCheck>{StepCheck::NoDeltaCo});

        StepResult noModel = r;
        noModel.deltaCo = 5.0;
        noModel.deadTime.reset();
        noModel.timeConstant.reset();
        noModel.fitRms.reset();
        noModel.fitRmsPercent.reset();
        QVERIFY(checkStep(rec, noModel) == QList<StepCheck>{StepCheck::NoModel});
    }

    void check_notSettled()
    {
        const Recorder rec = makeProcess(60.0);
        AnalysisSettings s;
        s.deltaCoTagId = kCo;
        const StepResult early = analyzeStep(rec, kPv, 9.0, 16.0, s);
        QVERIFY(early.valid);
        QVERIFY(checkStep(rec, early).contains(StepCheck::NotSettled));
        const StepResult late = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY(!checkStep(rec, late).contains(StepCheck::NotSettled));
        // Fewer than 3 samples in the window: not flagged.
        Recorder sparse;
        for (double t : {0.0, 10.0, 20.0, 30.0}) {
            sparse.append(batch(t, {good(kPv, t), good(kCo, 0.0)}));
        }
        StepResult sr;
        sr.tagId = kPv;
        sr.valid = true;
        sr.t1 = 0.0;
        sr.t2 = 30.0;
        sr.deltaPv = 30.0;
        sr.deltaCo = 1.0;
        sr.deadTime = 1.0;
        sr.timeConstant = 5.0;
        sr.fitRmsPercent = 1.0;
        QVERIFY(!checkStep(sparse, sr).contains(StepCheck::NotSettled));
    }

    void check_smallStep()
    {
        AnalysisSettings s;
        s.deltaCoTagId = kCo;
        s.reference = ReferenceMode::WindowMean;
        s.referenceWindowS = 2.0;
        const Recorder noisy = makeProcess(60.0, 1, 6.0, 7);
        const StepResult rn = analyzeStep(noisy, kPv, 9.0, 60.0, s);
        QVERIFY(rn.valid);
        QVERIFY(checkStep(noisy, rn).contains(StepCheck::SmallStep));
        const Recorder quiet = makeProcess(60.0, 1, 0.2, 7);
        const StepResult rq = analyzeStep(quiet, kPv, 9.0, 60.0, s);
        QVERIFY(rq.valid);
        QVERIFY(!checkStep(quiet, rq).contains(StepCheck::SmallStep));
    }

    void check_deadTimeAndFit()
    {
        const Recorder rec = makeProcess(60.0);
        AnalysisSettings s;
        s.deltaCoTagId = kCo;
        const StepResult base = analyzeStep(rec, kPv, 9.0, 60.0, s);
        QVERIFY(base.valid && base.timeConstant);

        StepResult r = base;
        r.deadTime = -0.5;
        QVERIFY(checkStep(rec, r) == QList<StepCheck>{StepCheck::NegativeDeadTime});
        r.deadTime = 0.0;
        QVERIFY(checkStep(rec, r).isEmpty());

        r.timeConstant = 4.0;
        r.deadTime = 4.1;
        QVERIFY(checkStep(rec, r) == QList<StepCheck>{StepCheck::DeadTimeDominant});
        r.deadTime = 4.0; // ratio exactly 1: not dominant
        QVERIFY(checkStep(rec, r).isEmpty());

        r = base;
        r.fitRmsPercent = 7.9;
        QVERIFY(checkStep(rec, r).isEmpty());
        r.fitRmsPercent = 8.0;
        QVERIFY(checkStep(rec, r) == QList<StepCheck>{StepCheck::FitPoor});
    }

    void check_order()
    {
        const Recorder rec = makeProcess(60.0);
        AnalysisSettings s; // no CO
        StepResult r = analyzeStep(rec, kPv, 9.0, 16.0, s);
        r.deadTime = -1.0;
        r.fitRmsPercent = 50.0;
        const QList<StepCheck> expected{StepCheck::NoDeltaCo, StepCheck::NotSettled,
                                        StepCheck::NegativeDeadTime, StepCheck::FitPoor};
        QVERIFY(checkStep(rec, r) == expected);
    }

    void fitQuality_thresholds()
    {
        QVERIFY(fitQuality(0.0) == FitQuality::Good);
        QVERIFY(fitQuality(2.99) == FitQuality::Good);
        QVERIFY(fitQuality(3.0) == FitQuality::Acceptable);
        QVERIFY(fitQuality(7.99) == FitQuality::Acceptable);
        QVERIFY(fitQuality(8.0) == FitQuality::Poor);
        QVERIFY(fitQuality(100.0) == FitQuality::Poor);
    }
};

QTEST_MAIN(Test)
#include "tst_stepanalysis.moc"
