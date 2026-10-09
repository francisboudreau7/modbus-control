#pragma once

#include <QObject>

class QwtPlot;
class QwtPlotMarker;

namespace mb {

/// Two vertical cursors (C1, C2) on a QwtPlot's bottom (time) axis, drawn as
/// QwtPlotMarker lines labelled "C1"/"C2" in distinct colours.
///
/// Interaction (event filter on the plot canvas):
/// - while placing: left click sets C1, the next left click sets C2, then placing ends;
/// - anytime: left-press within 5 px of a visible cursor line and drag moves it
///   (the panner uses the right/middle button, so left-drag is free for cursors);
/// - Esc while placing cancels placing.
class PlotCursors : public QObject
{
    Q_OBJECT
public:
    explicit PlotCursors(QwtPlot *plot); ///< parent = plot

    bool hasC1() const;
    bool hasC2() const;
    double c1() const; ///< seconds; only meaningful if hasC1()
    double c2() const;
    void setC1(double t);
    void setC2(double t);
    bool isPlacing() const;

    /// Shows/hides the markers without forgetting them (used by image export).
    void setMarkersVisible(bool visible);

public slots:
    void setPlacing(bool on);
    /// Removes both cursors and ends placing.
    void clear();

signals:
    /// C1 or C2 was set, moved or cleared.
    void cursorsChanged();
    void placingChanged(bool placing);
    /// The user placed or dragged a cursor (the trend pauses auto-scroll on this).
    void interacted();

private:
    // Implementation-defined; the task may change the private section freely.
    QwtPlot *m_plot = nullptr;
    QwtPlotMarker *m_c1 = nullptr;
    QwtPlotMarker *m_c2 = nullptr;
    bool m_placing = false;
};

} // namespace mb
