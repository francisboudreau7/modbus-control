#include "ClosedLoopAnalysis.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace mb {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// One analysed sample: both PV and SP are known.
struct Pt {
    double t = 0.0;
    double pv = 0.0;
    double sp = 0.0;
    double e() const { return sp - pv; }
};

/// Linear interpolation of v(t) over increasing times; clamps outside the range, and returns the
/// first sample at an exactly matching time (so duplicated times never divide by zero).
double interpolate(const QList<Pt> &s, double Pt::*field, double t)
{
    const qsizetype n = s.size();
    if (n == 0) {
        return kNaN;
    }
    const auto it = std::lower_bound(s.begin(), s.end(), t,
                                     [](const Pt &p, double x) { return p.t < x; });
    const qsizetype hi = it - s.begin();
    if (hi == 0) {
        return s.first().*field;
    }
    if (hi == n) {
        return s.last().*field;
    }
    if (s[hi].t == t) {
        return s[hi].*field;
    }
    const Pt &a = s[hi - 1];
    const Pt &b = s[hi];
    return a.*field + (b.*field - a.*field) * (t - a.t) / (b.t - a.t);
}

/// First (interpolated) time at which (pv - level) * dir >= 0.
std::optional<double> firstCrossing(const QList<Pt> &q, double level, double dir)
{
    for (qsizetype i = 0; i < q.size(); ++i) {
        if ((q[i].pv - level) * dir >= 0.0) {
            if (i == 0 || q[i].pv == level) {
                return q[i].t;
            }
            const Pt &a = q[i - 1];
            const Pt &b = q[i];
            return a.t + (level - a.pv) / (b.pv - a.pv) * (b.t - a.t);
        }
    }
    return std::nullopt;
}

struct Peak {
    double t;
    double f;
};

/// The first two peaks of f (a local maximum above `band`).
/// Noise robustness (zigzag with hysteresis `band`): a maximum is confirmed only after f has
/// fallen by at least `band` below it, and the next maximum is searched only after f has risen by
/// at least `band` above the valley that followed. A confirmed peak counts when its value is
/// above `band`. A plateau (equal samples) is one peak, located at its first sample.
QList<Peak> findPeaks(const QList<Pt> &q, const QList<double> &f, double band)
{
    QList<Peak> peaks;
    if (f.isEmpty() || !(band > 0.0)) {
        return peaks;
    }
    bool seekMax = true;
    double ext = f[0]; // running maximum (seekMax) or minimum
    qsizetype extIdx = 0;
    for (qsizetype i = 1; i < f.size() && peaks.size() < 2; ++i) {
        if (seekMax) {
            if (f[i] > ext) {
                ext = f[i];
                extIdx = i;
            } else if (ext - f[i] >= band) {
                if (ext > band) {
                    peaks.append({q[extIdx].t, ext});
                }
                seekMax = false;
                ext = f[i];
                extIdx = i;
            }
        } else {
            if (f[i] < ext) {
                ext = f[i];
                extIdx = i;
            } else if (f[i] - ext >= band) {
                seekMax = true;
                ext = f[i];
                extIdx = i;
            }
        }
    }
    return peaks;
}

ClosedLoopResult run(const ClosedLoopSeries &series, double t1, double t2,
                     const ClosedLoopSettings &settings, double coRangeMin, double coRangeMax,
                     bool startAtFirst)
{
    ClosedLoopResult r;
    r.test = settings.test;
    if (!std::isfinite(t1) || !std::isfinite(t2)) {
        r.error = QCoreApplication::translate("mb::ClosedLoopAnalysis", "Place both cursors");
        return r;
    }
    if (t1 > t2) {
        std::swap(t1, t2);
    }
    r.t1 = t1;
    r.t2 = t2;

    // Samples with a known time, PV and SP, in series order.
    const qsizetype n = std::min({series.t.size(), series.pv.size(), series.sp.size()});
    QList<Pt> all;
    all.reserve(n);
    for (qsizetype i = 0; i < n; ++i) {
        if (!std::isnan(series.t[i]) && !std::isnan(series.pv[i]) && !std::isnan(series.sp[i])) {
            all.append({series.t[i], series.pv[i], series.sp[i]});
        }
    }
    QList<Pt> win; // samples inside [t1, t2]
    for (const Pt &p : all) {
        if (p.t >= t1 && p.t <= t2) {
            win.append(p);
        }
    }
    if (win.size() < 2) {
        r.error = QCoreApplication::translate("mb::ClosedLoopAnalysis", "No data between the cursors");
        return r;
    }

    const bool stepTest = settings.test == ClosedLoopTest::SetpointStep;
    double d = 0.0;
    if (stepTest) {
        r.spBefore = interpolate(all, &Pt::sp, t1);
        r.spAfter = interpolate(all, &Pt::sp, t2);
        d = r.spAfter - r.spBefore;
        if (!(std::abs(d) >= 1e-12)) {
            r.error = QCoreApplication::translate("mb::ClosedLoopAnalysis", "The setpoint does not change between the cursors");
            r.checks.append(ClosedLoopCheck::NoSetpointStep);
            return r;
        }
        if (startAtFirst) {
            r.startTime = win.first().t;
        } else {
            r.startTime = win.first().t; // fallback when the SP never reaches the half level
            const double mid = r.spBefore + d / 2.0;
            const double dir = d > 0.0 ? 1.0 : -1.0;
            for (const Pt &p : win) {
                if ((p.sp - mid) * dir >= 0.0) {
                    r.startTime = p.t;
                    break;
                }
            }
        }
    } else {
        r.startTime = t1;
        r.spAfter = interpolate(all, &Pt::sp, t1);
        const double tol = std::max(1e-9 * std::abs(r.spAfter), 1e-9);
        for (const Pt &p : win) {
            if (std::abs(p.sp - r.spAfter) > tol) {
                r.checks.append(ClosedLoopCheck::SetpointMoved);
                break;
            }
        }
    }
    const double start = r.startTime;
    r.pvAtStart = interpolate(all, &Pt::pv, start);

    // Q: the point at startTime (interpolated) followed by the window samples after it.
    QList<Pt> q;
    q.append({start, r.pvAtStart, interpolate(all, &Pt::sp, start)});
    for (const Pt &p : win) {
        if (p.t > start) {
            q.append(p);
        }
    }

    // Disturbance: largest deviation.
    if (!stepTest) {
        double best = -1.0;
        for (const Pt &p : q) {
            if (std::abs(p.e()) > best) {
                best = std::abs(p.e());
                r.maxDeviation = p.e();
                r.maxDeviationTime = p.t - start;
            }
        }
    }

    // Overshoot, rise time.
    const double dir = d > 0.0 ? 1.0 : -1.0;
    if (stepTest) {
        double over = 0.0;
        double overT = start;
        for (const Pt &p : q) {
            const double o = (p.pv - r.spAfter) * dir;
            if (o > over) {
                over = o;
                overT = p.t;
            }
        }
        r.overshoot = over;
        r.overshootPct = over / std::abs(d) * 100.0;
        if (over > 0.0) {
            r.peakTime = overT - start;
        }
        const auto t10 = firstCrossing(q, r.spBefore + 0.1 * d, dir);
        const auto t90 = firstCrossing(q, r.spBefore + 0.9 * d, dir);
        if (t10 && t90) {
            r.riseTime = *t90 - *t10;
        }
    }

    // Settling band.
    double pct = settings.settlingBandPct;
    if (!(pct > 0.0)) {
        pct = 2.0;
    }
    pct = std::min(pct, 50.0);
    const double scale = stepTest ? std::abs(d) : std::abs(*r.maxDeviation);
    const double band = pct / 100.0 * scale;

    // Settling time: the first sample after the last sample (after startTime) outside the band.
    if (q.size() < 2) {
        r.checks.append(ClosedLoopCheck::NotSettled);
    } else {
        qsizetype last = -1;
        for (qsizetype i = 1; i < q.size(); ++i) {
            if (std::abs(q[i].e()) > band) {
                last = i;
            }
        }
        if (last < 0) {
            r.settlingTime = 0.0;
        } else if (last == q.size() - 1) {
            r.checks.append(ClosedLoopCheck::NotSettled);
        } else {
            r.settlingTime = q[last + 1].t - start;
        }
    }

    // Peaks of the error in the direction of the first excursion.
    {
        const double sgn = stepTest ? -dir : (*r.maxDeviation >= 0.0 ? 1.0 : -1.0);
        QList<double> f;
        f.reserve(q.size());
        for (const Pt &p : q) {
            f.append(p.e() * sgn);
        }
        const QList<Peak> peaks = findPeaks(q, f, band);
        if (peaks.size() >= 2 && peaks[0].f != 0.0) {
            r.decayRatio = peaks[1].f / peaks[0].f;
            r.period = peaks[1].t - peaks[0].t;
        }
    }

    // Steady-state error: mean PV over the last 10 % of [startTime, t2].
    {
        const double from = t2 - 0.1 * (t2 - start);
        double sum = 0.0;
        qsizetype count = 0;
        for (const Pt &p : q) {
            if (p.t >= from) {
                sum += p.pv;
                ++count;
            }
        }
        if (count == 0) {
            sum = q.last().pv;
            count = 1;
        }
        r.steadyStateError = r.spAfter - sum / double(count);
    }

    // Integrals (trapezoid) from startTime to t2.
    {
        double iae = 0.0;
        double ise = 0.0;
        double itae = 0.0;
        for (qsizetype i = 1; i < q.size(); ++i) {
            const double dt = q[i].t - q[i - 1].t;
            const double e0 = q[i - 1].e();
            const double e1 = q[i].e();
            iae += 0.5 * (std::abs(e0) + std::abs(e1)) * dt;
            ise += 0.5 * (e0 * e0 + e1 * e1) * dt;
            itae += 0.5 * ((q[i - 1].t - start) * std::abs(e0) + (q[i].t - start) * std::abs(e1)) * dt;
        }
        r.iae = iae;
        r.ise = ise;
        r.itae = itae;
    }

    // Output.
    const bool hasCo = !series.co.isEmpty() && series.co.size() == series.t.size();
    bool coUsed = false;
    if (hasCo) {
        const double span = std::abs(coRangeMax - coRangeMin);
        const double tol = 0.001 * span;
        double lo = 0.0;
        double hi = 0.0;
        double travel = 0.0;
        double prev = kNaN;
        bool saturated = false;
        for (qsizetype i = 0; i < series.t.size(); ++i) {
            const double t = series.t[i];
            const double c = series.co[i];
            if (std::isnan(t) || std::isnan(c) || t < start || t > t2) {
                continue;
            }
            if (!coUsed) {
                lo = hi = c;
                coUsed = true;
            } else {
                lo = std::min(lo, c);
                hi = std::max(hi, c);
                travel += std::abs(c - prev);
            }
            prev = c;
            if (span > 0.0 && (c <= coRangeMin + tol || c >= coRangeMax - tol)) {
                saturated = true;
            }
        }
        if (coUsed) {
            r.outMin = lo;
            r.outMax = hi;
            r.outTravel = travel;
            if (saturated) {
                r.checks.append(ClosedLoopCheck::OutputSaturated);
            }
        }
    }
    if (!coUsed) {
        r.checks.append(ClosedLoopCheck::NoOutput);
    }

    // Keep the checks in enum order.
    std::sort(r.checks.begin(), r.checks.end());
    r.valid = true;
    return r;
}

} // namespace

ClosedLoopResult analyzeClosedLoopSeries(const ClosedLoopSeries &series, double t1, double t2,
                                         const ClosedLoopSettings &settings, double coRangeMin,
                                         double coRangeMax, double /*bandOverride: fix task*/)
{
    return run(series, t1, t2, settings, coRangeMin, coRangeMax, false);
}

ClosedLoopResult analyzeClosedLoop(const Recorder &recorder, TagId pvTag, TagId spTag, TagId coTag,
                                   double t1, double t2, const ClosedLoopSettings &settings,
                                   double coRangeMin, double coRangeMax)
{
    if (!std::isfinite(t1) || !std::isfinite(t2)) {
        ClosedLoopResult r;
        r.test = settings.test;
        r.t1 = t1;
        r.t2 = t2;
        r.error = QCoreApplication::translate("mb::ClosedLoopAnalysis", "Place both cursors");
        return r;
    }
    if (t1 > t2) {
        std::swap(t1, t2);
    }
    const auto [first, last] = recorder.rowRange(t1, t2);
    ClosedLoopSeries s;
    const bool typed = spTag == 0;
    const bool stepTest = settings.test == ClosedLoopTest::SetpointStep;
    bool seenPv = false; // typed step: the step is at the first row with a PV value
    for (qsizetype row = first; row < last; ++row) {
        const double pv = recorder.valueAt(row, pvTag);
        s.t.append(recorder.timeAt(row));
        s.pv.append(pv);
        if (typed) {
            if (stepTest) {
                s.sp.append(seenPv ? settings.typedSpAfter : settings.typedSpBefore);
                if (!std::isnan(pv)) {
                    seenPv = true;
                }
            } else {
                s.sp.append(settings.typedSpAfter);
            }
        } else {
            s.sp.append(recorder.valueAt(row, spTag));
        }
        if (coTag != 0) {
            s.co.append(recorder.valueAt(row, coTag));
        }
    }
    return run(s, t1, t2, settings, coRangeMin, coRangeMax, typed && stepTest);
}

} // namespace mb
