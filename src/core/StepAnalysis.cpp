#include "StepAnalysis.h"

#include <algorithm>
#include <cmath>

namespace mb {

namespace {

struct Series {
    QList<double> t;
    QList<double> v;
};

/// Rows of [t1, t2] (t1 <= t2) of one tag, including NaN values.
Series extractRows(const Recorder &recorder, TagId tag, double t1, double t2)
{
    Series s;
    const auto [first, last] = recorder.rowRange(t1, t2);
    const qsizetype n = last > first ? last - first : 0;
    s.t.reserve(n);
    s.v.reserve(n);
    for (qsizetype row = first; row < last; ++row) {
        s.t.append(recorder.timeAt(row));
        s.v.append(recorder.valueAt(row, tag));
    }
    return s;
}

/// Start of the rows searched for crossings: the samples that define the 0 % level are
/// included, so the 0 % point always has a sample at or before it (the one at C1 for
/// CursorValue, the mean window [t1 - W, t1] for WindowMean).
double searchStart(const Recorder &recorder, double t1, const AnalysisSettings &settings)
{
    if (settings.reference == ReferenceMode::WindowMean)
        return t1 - settings.referenceWindowS;
    const qsizetype row = recorder.rowAtOrBefore(t1);
    return row >= 0 ? std::min(t1, recorder.timeAt(row)) : t1;
}

/// Series without its NaN samples.
Series dropNaN(const QList<double> &times, const QList<double> &values)
{
    Series s;
    const qsizetype n = std::min(times.size(), values.size());
    s.t.reserve(n);
    s.v.reserve(n);
    for (qsizetype i = 0; i < n; ++i) {
        if (!std::isnan(values[i])) {
            s.t.append(times[i]);
            s.v.append(values[i]);
        }
    }
    return s;
}

/// Linear interpolation of the (NaN-free) series at time `t`; clamps outside the range.
double interpolateAt(const Series &s, double t)
{
    const qsizetype n = s.t.size();
    if (n == 0 || std::isnan(t)) {
        return kNaN;
    }
    if (t <= s.t.first()) {
        return s.v.first();
    }
    if (t >= s.t.last()) {
        return s.v.last();
    }
    const auto it = std::upper_bound(s.t.begin(), s.t.end(), t); // first time > t
    const qsizetype hi = it - s.t.begin();
    const qsizetype lo = hi - 1;
    const double dt = s.t[hi] - s.t[lo];
    if (dt <= 0.0) {
        return s.v[lo];
    }
    return s.v[lo] + (s.v[hi] - s.v[lo]) * (t - s.t[lo]) / dt;
}

/// Time between samples a and b at which the series reaches `target`.
double interpolateCrossing(double ta, double va, double tb, double vb, double target)
{
    const double dv = vb - va;
    if (dv == 0.0) {
        return ta;
    }
    return ta + (target - va) / dv * (tb - ta);
}

} // namespace

QList<double> smoothValues(const QList<double> &times, const QList<double> &values, double widthS)
{
    if (!(widthS > 0.0)) {
        return values;
    }
    const qsizetype n = std::min(times.size(), values.size());
    const double half = widthS / 2.0;
    QList<double> out;
    out.reserve(n);

    qsizetype lo = 0; // first index inside the window
    qsizetype hi = 0; // one past the last index inside the window
    double sum = 0.0;
    qsizetype count = 0;
    for (qsizetype i = 0; i < n; ++i) {
        while (hi < n && times[hi] - times[i] <= half) {
            if (!std::isnan(values[hi])) {
                sum += values[hi];
                ++count;
            }
            ++hi;
        }
        while (lo < hi && times[i] - times[lo] > half) {
            if (!std::isnan(values[lo])) {
                sum -= values[lo];
                --count;
            }
            ++lo;
        }
        if (count == 0) {
            sum = 0.0; // avoid accumulating rounding residue
            out.append(kNaN);
        } else {
            out.append(sum / double(count));
        }
    }
    return out;
}

std::optional<double> referenceLevel(const Recorder &recorder, TagId tag, double t,
                                     ReferenceMode mode, double windowS)
{
    if (std::isnan(t)) {
        return std::nullopt;
    }
    if (mode == ReferenceMode::CursorValue) {
        const double v = recorder.sampleAt(tag, t);
        if (std::isnan(v)) {
            return std::nullopt;
        }
        return v;
    }
    const auto [first, last] = recorder.rowRange(t - windowS, t);
    double sum = 0.0;
    qsizetype count = 0;
    for (qsizetype row = first; row < last; ++row) {
        const double v = recorder.valueAt(row, tag);
        if (!std::isnan(v)) {
            sum += v;
            ++count;
        }
    }
    if (count == 0) {
        return std::nullopt;
    }
    return sum / double(count);
}

std::optional<double> findCrossingTime(const QList<double> &times, const QList<double> &values,
                                       double y0, double y100, double percent)
{
    const double span = y100 - y0;
    if (span == 0.0 || std::isnan(span)) {
        return std::nullopt;
    }
    const double dir = span > 0.0 ? 1.0 : -1.0;
    const Series s = dropNaN(times, values);
    const qsizetype n = s.t.size();

    const double level50 = y0 + 0.5 * span;
    qsizetype anchor = -1;
    for (qsizetype i = 0; i < n; ++i) {
        if ((s.v[i] - level50) * dir >= 0.0) {
            anchor = i;
            break;
        }
    }
    if (anchor < 0) {
        return std::nullopt;
    }

    const double target = y0 + percent / 100.0 * span;
    if (percent >= 50.0) {
        for (qsizetype k = anchor; k < n; ++k) {
            if ((s.v[k] - target) * dir >= 0.0) {
                if (k == 0 || s.v[k] == target) {
                    return s.t[k];
                }
                return interpolateCrossing(s.t[k - 1], s.v[k - 1], s.t[k], s.v[k], target);
            }
        }
        return std::nullopt;
    }

    for (qsizetype k = anchor - 1; k >= 0; --k) {
        if ((s.v[k] - target) * dir <= 0.0) {
            if (s.v[k] == target) {
                return s.t[k];
            }
            return interpolateCrossing(s.t[k], s.v[k], s.t[k + 1], s.v[k + 1], target);
        }
    }
    return std::nullopt;
}

std::optional<StepInput> analyzeStepInput(const Recorder &recorder, TagId coTag, double t1,
                                          double t2, const AnalysisSettings &settings)
{
    if (std::isnan(t1) || std::isnan(t2)) {
        return std::nullopt;
    }
    if (t1 > t2) {
        std::swap(t1, t2);
    }
    const auto y0 = referenceLevel(recorder, coTag, t1, settings.reference,
                                   settings.referenceWindowS);
    const auto y100 = referenceLevel(recorder, coTag, t2, settings.reference,
                                     settings.referenceWindowS);
    if (!y0 || !y100 || *y0 == *y100) {
        return std::nullopt;
    }
    const Series rows = extractRows(recorder, coTag, searchStart(recorder, t1, settings), t2);
    const auto crossing = findCrossingTime(rows.t, rows.v, *y0, *y100, 50.0);
    if (!crossing) {
        return std::nullopt;
    }
    StepInput in;
    in.delta = *y100 - *y0;
    in.stepTime = *crossing;
    return in;
}

double fopdtModelValue(const StepResult &, double)
{
    return kNaN; // T100 stub; implemented by T101
}

StepResult analyzeStep(const Recorder &recorder, TagId pvTag, double t1, double t2,
                       const AnalysisSettings &settings)
{
    StepResult r;
    r.tagId = pvTag;
    if (std::isnan(t1) || std::isnan(t2)) {
        r.error = QStringLiteral("Place both cursors");
        return r;
    }
    if (t1 > t2) {
        std::swap(t1, t2);
    }
    r.t1 = t1;
    r.t2 = t2;

    const auto y0 = referenceLevel(recorder, pvTag, t1, settings.reference,
                                   settings.referenceWindowS);
    if (!y0) {
        r.error = QStringLiteral("No data at C1");
        return r;
    }
    const auto y100 = referenceLevel(recorder, pvTag, t2, settings.reference,
                                     settings.referenceWindowS);
    if (!y100) {
        r.error = QStringLiteral("No data at C2");
        return r;
    }
    if (*y0 == *y100) {
        r.error = QStringLiteral("No change between C1 and C2");
        return r;
    }
    r.valid = true;
    r.y0 = *y0;
    r.y100 = *y100;
    r.deltaPv = r.y100 - r.y0;

    const Series rows = extractRows(recorder, pvTag, searchStart(recorder, t1, settings), t2);
    const Series raw = dropNaN(rows.t, rows.v);
    const QList<double> smooth = smoothValues(rows.t, rows.v, settings.smoothingS);

    for (const double percent : settings.percents) {
        SetpointResult sp;
        sp.percent = percent;
        sp.target = r.y0 + percent / 100.0 * r.deltaPv;
        if (const auto tc = findCrossingTime(rows.t, smooth, r.y0, r.y100, percent)) {
            sp.found = true;
            sp.time = *tc;
            sp.measured = interpolateAt(raw, *tc);
        }
        r.setpoints.append(sp);
    }

    // Input step.
    r.stepTime = t1;
    if (settings.deltaCoTagId != 0 && settings.deltaCoTagId != pvTag) {
        if (const auto in = analyzeStepInput(recorder, settings.deltaCoTagId, t1, t2, settings)) {
            r.deltaCo = in->delta;
            r.stepTime = in->stepTime;
        }
    } else if (settings.deltaCoTagId == 0 && settings.manualDeltaCo != 0.0
               && !std::isnan(settings.manualDeltaCo)) {
        r.deltaCo = settings.manualDeltaCo;
    }
    if (r.deltaCo && *r.deltaCo != 0.0) {
        r.gain = r.deltaPv / *r.deltaCo;
    }

    // First-order plus dead-time model.
    const double a = settings.modelPercentA / 100.0;
    const double b = settings.modelPercentB / 100.0;
    if (a >= 0.0 && a < b && b < 1.0) {
        const auto ta = findCrossingTime(rows.t, smooth, r.y0, r.y100, settings.modelPercentA);
        const auto tb = findCrossingTime(rows.t, smooth, r.y0, r.y100, settings.modelPercentB);
        if (ta && tb) {
            const double denom = std::log(1.0 - a) - std::log(1.0 - b);
            const double T = (*tb - *ta) / denom;
            if (T > 0.0) {
                r.timeConstant = T;
                r.deadTime = *ta - T * (-std::log(1.0 - a)) - r.stepTime;
            }
        }
    }
    return r;
}

} // namespace mb
