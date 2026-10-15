#pragma once

#include "Tag.h"

#include <QList>
#include <QWidget>
#include <utility>
#include <optional>
#include <functional>

#include <qwt_axis_id.h>

class QAction;
class QComboBox;
class QMenu;
class TestTrendPlot;
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
///
/// Y-axis ranges (v1.3.1). Each Y axis is Auto (auto-scaled as above) or Fixed at [min, max]:
/// - a fixed axis keeps its range while running (only the time axis scrolls) and when tags change;
/// - right-click on a Y axis (the axis widget) opens axisMenu(axis): "Set range..." (asks min/max
///   through the m_askRange seam, default a QDialog with two QDoubleSpinBox and OK/Cancel, OK only
///   when min < max; prefilled with the current visible range), "0 to 100", "Auto"; the "Y axes"
///   tool button in the control row ("yAxesButton") has submenus "Left" and "Right" with the same
///   actions ("Right" disabled while no plotted tag uses the right axis);
/// - mouse wheel over a Y axis widget zooms that axis only, around the value under the mouse
///   (factor 0.8 per notch in, 1.25 out), and makes it Fixed at the new range; Shift+wheel over the
///   canvas zooms both Y axes and makes both Fixed; plain wheel over the canvas zooms time as today;
///   right-drag pan shifts fixed ranges too;
/// - "Fit Y" fits both axes to the visible data and sets both to Auto; Run (unpause) keeps fixed
///   ranges.
/// Every change made by the user (menu, wheel, pan, Fit Y) emits axisRangesChanged() once;
/// setAxisRange() does not.
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
    /// Y-axis range of QwtAxis::YLeft or QwtAxis::YRight: nullopt = Auto.
    std::optional<std::pair<double, double>> axisRange(QwtAxisId axis) const;
    /// Sets an axis to Fixed [min, max] (min < max, finite; anything else is ignored) or to Auto
    /// (nullopt) and replots. Does not emit axisRangesChanged().
    void setAxisRange(QwtAxisId axis, std::optional<std::pair<double, double>> range);
    /// The axis context menu described above (new each call, parented to this widget).
    QMenu *axisMenu(QwtAxisId axis);
    PlotCursors *cursors() const;
    double windowSeconds() const; ///< 0 = All
    bool isPaused() const;

public slots:
    /// Pulls rows appended since the last refresh and redraws. Cheap when nothing changed.
    /// Called by the app at ~10 Hz.
    void refresh();
    void setWindowSeconds(double seconds);
    void setPaused(bool paused);
    /// Forgets the curve data and reloads every row the Recorder holds now (call after the
    /// Recorder was cleared or replaced, e.g. by an opened session).
    void resetData();

signals:
    void pausedChanged(bool paused);
    /// The "Clear data" button was pressed; the owner decides whether to clear the recording.
    void clearDataRequested();
    /// The user changed a Y-axis range (see above).
    void axisRangesChanged();

private:
    friend class ::TestTrendPlot;
    // Implementation-defined; the task may change the private section freely (keep the seam).
    /// Asks for a range: returns nullopt when cancelled. Arguments: axis, current visible range.
    std::function<std::optional<std::pair<double, double>>(QwtAxisId, std::pair<double, double>)>
        m_askRange;
    std::optional<std::pair<double, double>> m_leftRange;
    std::optional<std::pair<double, double>> m_rightRange;
    QwtPlot *m_plot = nullptr;
    PlotCursors *m_cursors = nullptr;
    const Recorder *m_recorder = nullptr;
    double m_window = 60.0;
    bool m_paused = false;
    QToolButton *m_yAxesButton = nullptr;
    QAction *m_rightMenuAction = nullptr;
    void userSetRange(QwtAxisId axis, std::optional<std::pair<double, double>> range); ///< + emit
    void fixVisibleAxes(); ///< current Y scales become Fixed ranges (zoom/pan) + emit
    bool eventFilter(QObject *watched, QEvent *event) override;

    struct CurveEntry;
    bool pull(); ///< appends new recorder rows to the curves; true if anything changed
    void fitY(double x1, double x2);
    /// Auto-scales one axis to the data in [x1, x2]; false when there was no data (scale unchanged).
    bool fitAxis(QwtAxisId axis, double x1, double x2);
    void adaptPens();  ///< thinner pens when very many points are visible (speed)
    bool m_thinPens = false;
    void applyAxes(); ///< running: x window + y fit

    QList<CurveEntry *> m_entries;
    QComboBox *m_windowCombo = nullptr;
    QToolButton *m_pauseButton = nullptr;
    QPushButton *m_placeButton = nullptr;
};

} // namespace mb
