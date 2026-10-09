#include "PlotCursors.h"

#include <qwt_plot.h>

// Stub: implemented by its task (see TASKS.md).

namespace mb {

PlotCursors::PlotCursors(QwtPlot *plot) : QObject(plot), m_plot(plot) {}
bool PlotCursors::hasC1() const { return false; }
bool PlotCursors::hasC2() const { return false; }
double PlotCursors::c1() const { return 0.0; }
double PlotCursors::c2() const { return 0.0; }
void PlotCursors::setC1(double) {}
void PlotCursors::setC2(double) {}
bool PlotCursors::isPlacing() const { return m_placing; }
void PlotCursors::setMarkersVisible(bool) {}
void PlotCursors::setPlacing(bool on) { m_placing = on; }
void PlotCursors::clear() {}

} // namespace mb