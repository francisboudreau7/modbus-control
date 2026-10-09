#pragma once

#include "Tag.h"

#include <QList>
#include <QWidget>

class QwtPlot;

namespace mb {

class PlotCursors;
class Recorder;

/// Live trend: a control row plus a QwtPlot.
///
/// Control row: time window combo (60 s, 5 min, 30 min, All), Pause/Run toggle,
/// "Place cursors" toggle (bound to PlotCursors::setPlacing/placingChanged),
/// "Clear cursors", "Fit Y".
///
/// Plot: x = seconds since the time base, y = scaled value; one QwtPlotCurve per plotted
/// tag (tag colour, legend title "name [unit]"); NaN samples break the line. Mouse wheel
/// zooms, right/middle-drag pans; zooming or panning pauses auto-scroll. While running,
/// the x axis shows [tLast - window, tLast] (or everything for All) and y auto-scales.
/// Curves are fed incrementally from the Recorder (Recorder::evictedCount() maps rows to
/// absolute sample numbers) and decimated so 20 tags x 36 000 points stay smooth.
class TrendPlot : public QWidget
{
    Q_OBJECT
public:
    explicit TrendPlot(QWidget *parent = nullptr);

    /// Not owned; may be null. Must outlive this widget or be reset to null.
    void setRecorder(const Recorder *recorder);
    /// Creates/updates/removes curves for the tags with plotted == true.
    void setTags(const QList<Tag> &tags);

    QwtPlot *plot() const;
    PlotCursors *cursors() const;
    double windowSeconds() const; ///< 0 = All
    bool isPaused() const;

public slots:
    /// Pulls rows appended since the last refresh and redraws. Cheap when nothing changed.
    /// Called by the app at ~10 Hz.
    void refresh();
    void setWindowSeconds(double seconds);
    void setPaused(bool paused);
    /// Forgets all curve data (call after the Recorder was cleared).
    void resetData();

signals:
    void pausedChanged(bool paused);

private:
    // Implementation-defined; the task may change the private section freely.
    QwtPlot *m_plot = nullptr;
    PlotCursors *m_cursors = nullptr;
    const Recorder *m_recorder = nullptr;
    double m_window = 60.0;
    bool m_paused = false;
};

} // namespace mb
