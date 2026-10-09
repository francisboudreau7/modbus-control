#include "StepAnalysis.h"

// Stub created by T90; implemented by T91.

namespace mb {

QList<double> smoothValues(const QList<double> &, const QList<double> &values, double)
{
    return values;
}

std::optional<double> referenceLevel(const Recorder &, TagId, double, ReferenceMode, double)
{
    return std::nullopt;
}

std::optional<double> findCrossingTime(const QList<double> &, const QList<double> &, double,
                                       double, double)
{
    return std::nullopt;
}

std::optional<StepInput> analyzeStepInput(const Recorder &, TagId, double, double,
                                          const AnalysisSettings &)
{
    return std::nullopt;
}

StepResult analyzeStep(const Recorder &, TagId pvTag, double, double, const AnalysisSettings &)
{
    StepResult r;
    r.tagId = pvTag;
    r.error = QStringLiteral("Not implemented");
    return r;
}

} // namespace mb
