#pragma once

#include <QColor>
#include <QCursor>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QWidget>

#include <array>
#include <optional>

class QwtPlot;
class QwtPlotMarker;

namespace mb {

/// Up to kMaxCursors vertical cursors C1..C6 on a QwtPlot's bottom (time) axis, drawn as
/// QwtPlotMarker lines labelled "C1".."C6" in distinct colours (colorOf). C1 and C2 keep their
/// roles (Analysis, setpoints, Δt); C3..C6 are extra reading cursors (v1.3.3).
///
/// Interaction (event filter on the plot canvas):
/// - while placing, each left click sets the lowest-numbered missing cursor. Placing started with
///   C1 or C2 missing ends once both exist (as before: two clicks from nothing); placing started
///   with C1 and C2 present adds ONE cursor (the lowest missing of C3..C6) and ends. With all six
///   present setPlacing(true) does nothing (placing stays off, placingChanged not emitted);
/// - anytime: left-press within 5 px of a visible cursor line and drag moves it
///   (the panner uses the right/middle button, so left-drag is free for cursors); the
///   highest-numbered line wins when several are within 5 px;
/// - Esc while placing cancels placing.
/// Cursors are removed individually with removeCursor (the Cursors panel's remove buttons) or all
/// with clear().
class PlotCursors : public QObject
{
    Q_OBJECT
public:
    static constexpr int kMaxCursors = 6;

    explicit PlotCursors(QwtPlot *plot); ///< parent = plot
    ~PlotCursors() override;

    /// n in 1..kMaxCursors; false / nullopt / no-op for other n.
    bool hasCursor(int n) const;
    std::optional<double> cursor(int n) const;
    void setCursor(int n, double t);
    void removeCursor(int n);
    /// Numbers of the existing cursors, ascending.
    QList<int> cursorNumbers() const;
    /// Line and label colour of cursor n (fixed palette; C1 and C2 keep their old colours).
    static QColor colorOf(int n);

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
    /// Any cursor was set, moved, removed or cleared.
    void cursorsChanged();
    void placingChanged(bool placing);
    /// The user placed or dragged a cursor (the trend pauses auto-scroll on this).
    void interacted();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    // Implementation-defined; T242 may change the private section freely.
    void setMarker(int which, double t);
    int markerAt(double px) const;
    void updateHoverCursor(bool near);

    QwtPlot *m_plot = nullptr;
    QPointer<QWidget> m_canvas;
    QwtPlotMarker *m_c1 = nullptr;
    QwtPlotMarker *m_c2 = nullptr;
    bool m_has1 = false;
    bool m_has2 = false;
    std::array<std::optional<double>, kMaxCursors> m_extra{}; ///< T240 stub storage (index n-1)
    bool m_visible = true;
    bool m_placing = false;
    int m_placeStep = 0;
    int m_drag = 0; ///< 0 none, 1 = C1, 2 = C2
    bool m_swallowRelease = false;
    bool m_cursorShown = false;
    QCursor m_savedCursor; ///< canvas cursor to restore after the hover shape
};

} // namespace mb
