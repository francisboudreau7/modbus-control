#include "TrendPlot.h"

#include "PlotCursors.h"

#include <QVBoxLayout>

#include <qwt_plot.h>

// Stub: implemented by its task (see TASKS.md).

namespace mb {

TrendPlot::TrendPlot(QWidget *parent) : QWidget(parent), m_plot(new QwtPlot(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_plot);
    m_cursors = new PlotCursors(m_plot);
}
void TrendPlot::setRecorder(const Recorder *recorder) { m_recorder = recorder; }
void TrendPlot::setTags(const QList<Tag> &) {}
QwtPlot *TrendPlot::plot() const { return m_plot; }
PlotCursors *TrendPlot::cursors() const { return m_cursors; }
double TrendPlot::windowSeconds() const { return m_window; }
bool TrendPlot::isPaused() const { return m_paused; }
void TrendPlot::refresh() {}
void TrendPlot::setWindowSeconds(double seconds) { m_window = seconds; }
void TrendPlot::setPaused(bool paused) { m_paused = paused; }
void TrendPlot::resetData() {}

} // namespace mb