#include "ClosedLoopAnalysis.h"

namespace mb {

// Stubs until the closed-loop analysis task.
ClosedLoopResult analyzeClosedLoopSeries(const ClosedLoopSeries &, double t1, double t2,
                                         const ClosedLoopSettings &settings, double, double)
{
    ClosedLoopResult r;
    r.test = settings.test;
    r.t1 = t1;
    r.t2 = t2;
    return r;
}

ClosedLoopResult analyzeClosedLoop(const Recorder &, TagId, TagId, TagId, double t1, double t2,
                                   const ClosedLoopSettings &settings, double, double)
{
    ClosedLoopResult r;
    r.test = settings.test;
    r.t1 = t1;
    r.t2 = t2;
    return r;
}

} // namespace mb
