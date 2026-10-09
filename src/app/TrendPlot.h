#pragma once

#include "Tag.h"

#include <QList>
#include <QWidget>

#include <qwt_axis_id.h>

class QComboBox;
class QPushButton;
class QToolButton;
class QwtPlot;

namespace mb {

class PlotCursors;
class Recorder;

/// Live trend: a control row plus a QwtPlot.
///
/// Control row: time window combo (60 s, 5 min, 30 min, All), Pause/Run toggle,
/// "Place cursors" toggle (bound to PlotCursors::setPlacing/placingChanged),
/// "Clear cursors", "Fit Y", "Clear data" (only emits clearDataRequested()).
///
/// Plot: x = seconds since the time base, y = scaled value; one QwtPlotCurve per plotted
/// tag (tag colour, legend title "name [unit]"); NaN samples break the line. Each curve is
/// attached to QwtAxis::YLeft or QwtAxis::YRight from Tag::axis; the right axis is visible
/// only while at least one plotted tag uses it. Each Y axis auto-scales (and "Fit Y" fits)
/// to its own curves only; an axis title lists the distinct units of its tags (", "
/// separated, empty when none). Mouse wheel zooms (time axis and both Y axes), right-drag
/// pans; zooming or panning pauses auto-scroll. While running, the x axis shows
/// [tLast - window, tLast] (or everything for All) and the Y axes auto-scale.
/// Curves are fed incrementally from the Recorder (Recorder::evictedCount() maps rows to
/// absolute sample numbers) and decimated so 20 tags x 36 000 points stay smooth.
class TrendPlot : public QWidget
{
    Q_OBJECT
public:
    explicit TrendPlot(QWidget *parent = nullptr);
    ~TrendPlot() override;

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
    /// The "Clear data" button was pressed; the owner decides whether to clear the recording.
    void clearDataRequested();

private:
    // Implementation-defined; the task may change the private section freely.
    QwtPlot *m_plot = nullptr;
    PlotCursors *m_cursors = nullptr;
    const Recorder *m_recorder = nullptr;
    double m_window = 60.0;
    bool m_paused = false;
    bool m_autoY = true;

    struct CurveEntry;
    bool pull(); ///< appends new recorder rows to the curves; true if anything changed
    void fitY(double x1, double x2);
    void fitAxis(QwtAxisId axis, double x1, double x2);
    void adaptPens();  ///< thinner pens when very many points are visible (speed)
    bool m_thinPens = false;
    void applyAxes(); ///< running: x window + y fit

    QList<CurveEntry *> m_entries;
    QComboBox *m_windowCombo = nullptr;
    QToolButton *m_pauseButton = nullptr;
    QPushButton *m_placeButton = nullptr;
};

} // namespace mb
