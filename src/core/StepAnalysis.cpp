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
/// Recorded value at time t, linearly interpolated between the neighbouring rows (the row at
/// t itself when there is one). Falls back to the row at or before t when the next value is
/// missing; NaN when that one is missing too.
double valueAtTime(const Recorder &recorder, TagId tag, double t)
{
    const qsizetype row = recorder.rowAtOrBefore(t);
    if (row < 0)
        return kNaN;
    const double v0 = recorder.valueAt(row, tag);
    const double t0 = recorder.timeAt(row);
    if (t0 == t || row + 1 >= recorder.size() || std::isnan(v0))
        return v0;
    const double v1 = recorder.valueAt(row + 1, tag);
    const double t1 = recorder.timeAt(row + 1);
    if (std::isnan(v1) || t1 <= t0)
        return v0;
    return v0 + (v1 - v0) * (t - t0) / (t1 - t0);
}

/// The series searched for crossings: the cursor points (t1, y0) and (t2, y100) around the
/// (smoothed) rows of [t1, t2]. The search therefore never leaves the cursors: 0 % is at or
/// after C1 and 100 % at or before C2.
Series searchSeries(const Series &rows, const QList<double> &values, double t1, double y0,
                    double t2, double y100)
{
    Series s;
    s.t.reserve(rows.t.size() + 2);
    s.v.reserve(rows.t.size() + 2);
    s.t.append(t1);
    s.v.append(y0);
    s.t.append(rows.t);
    s.v.append(values);
    s.t.append(t2);
    s.v.append(y100);
    return s;
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
        const double v = valueAtTime(recorder, tag, t);
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
    const Series rows = extractRows(recorder, coTag, t1, t2);
    const Series search = searchSeries(rows, rows.v, t1, *y0, t2, *y100);
    const auto crossing = findCrossingTime(search.t, search.v, *y0, *y100, 50.0);
    if (!crossing) {
        return std::nullopt;
    }
    StepInput in;
    in.delta = *y100 - *y0;
    in.stepTime = *crossing;
    return in;
}

double fopdtModelValue(const StepResult &result, double t)
{
    if (!result.valid || !result.deadTime || !result.timeConstant) {
        return kNaN;
    }
    const double start = result.stepTime + *result.deadTime;
    if (t <= start) {
        return result.y0;
    }
    return result.y0
           + result.deltaPv * (1.0 - std::exp(-(t - start) / *result.timeConstant));
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

    const Series rows = extractRows(recorder, pvTag, t1, t2);
    const Series raw = dropNaN(rows.t, rows.v); // recorded samples only (fit RMS)
    const Series search = searchSeries(rows, smoothValues(rows.t, rows.v, settings.smoothingS),
                                       t1, r.y0, t2, r.y100);
    // Recorded values for "measured", including the interpolated values at the cursors.
    const Series rawAtCursors = [&] {
        Series s = searchSeries(rows, rows.v, t1, valueAtTime(recorder, pvTag, t1), t2,
                                valueAtTime(recorder, pvTag, t2));
        return dropNaN(s.t, s.v);
    }();

    for (const double percent : settings.percents) {
        SetpointResult sp;
        sp.percent = percent;
        sp.target = r.y0 + percent / 100.0 * r.deltaPv;
        if (const auto tc = findCrossingTime(search.t, search.v, r.y0, r.y100, percent)) {
            sp.found = true;
            sp.time = *tc;
            sp.measured = interpolateAt(rawAtCursors, *tc);
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
        const auto ta = findCrossingTime(search.t, search.v, r.y0, r.y100, settings.modelPercentA);
        const auto tb = findCrossingTime(search.t, search.v, r.y0, r.y100, settings.modelPercentB);
        if (ta && tb) {
            const double denom = std::log(1.0 - a) - std::log(1.0 - b);
            const double T = (*tb - *ta) / denom;
            if (T > 0.0) {
                r.timeConstant = T;
                r.deadTime = *ta - T * (-std::log(1.0 - a)) - r.stepTime;
            }
        }
    }

    // Model fit residual over the raw (unsmoothed) non-NaN samples between the cursors.
    if (r.deadTime && r.timeConstant) {
        double sumSq = 0.0;
        qsizetype count = 0;
        for (qsizetype i = 0; i < raw.t.size(); ++i) {
            if (raw.t[i] < t1 || raw.t[i] > t2) {
                continue;
            }
            const double e = raw.v[i] - fopdtModelValue(r, raw.t[i]);
            sumSq += e * e;
            ++count;
        }
        if (count > 0) {
            const double rms = std::sqrt(sumSq / double(count));
            r.fitRms = rms;
            r.fitRmsPercent = rms / std::abs(r.deltaPv) * 100.0;
        }
    }
    return r;
}

// Stubs created by T190; implemented by T191.
QString toString(ModelMethod method)
{
    switch (method) {
    case ModelMethod::ZeroTo632:
        return QStringLiteral("0 % / 63.2 % (two points)");
    case ModelMethod::Smith:
        return QStringLiteral("Smith (28.3 % / 63.2 %)");
    case ModelMethod::Sundaresan:
        return QStringLiteral("Sundaresan-Krishnaswamy (35.3 % / 85.3 %)");
    case ModelMethod::Custom:
        return QStringLiteral("Custom");
    }
    return {};
}

std::optional<std::pair<double, double>> methodPercents(ModelMethod method)
{
    switch (method) {
    case ModelMethod::ZeroTo632:
        return std::pair{0.0, 63.2};
    case ModelMethod::Smith:
        return std::pair{28.3, 63.2};
    case ModelMethod::Sundaresan:
        return std::pair{35.3, 85.3};
    case ModelMethod::Custom:
        break;
    }
    return std::nullopt;
}

std::pair<double, double> modelPercents(const AnalysisSettings &settings)
{
    if (const auto p = methodPercents(settings.method))
        return *p;
    return {settings.modelPercentA, settings.modelPercentB};
}

std::optional<StepWindow> findStep(const Recorder &, TagId, TagId, double, double)
{
    return std::nullopt;
}

FitQuality fitQuality(double fitRmsPercent)
{
    if (fitRmsPercent < 3.0)
        return FitQuality::Good;
    if (fitRmsPercent < 8.0)
        return FitQuality::Acceptable;
    return FitQuality::Poor;
}

QList<StepCheck> checkStep(const Recorder &, const StepResult &)
{
    return {};
}

} // namespace mb
