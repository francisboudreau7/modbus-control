#include "PlotCursors.h"

#include <qwt_axis.h>
#include <qwt_plot.h>
#include <qwt_plot_marker.h>
#include <qwt_interval.h>
#include <qwt_scale_div.h>
#include <qwt_scale_map.h>
#include <qwt_text.h>

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPen>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace mb {

namespace {
constexpr double kGrabPx = 5.0;

QwtPlotMarker *makeMarker(const char *label, const QColor &color)
{
    auto *m = new QwtPlotMarker(QString::fromLatin1(label));
    m->setLineStyle(QwtPlotMarker::VLine);
    QPen pen(color, 1.5, Qt::DashLine);
    m->setLinePen(pen);
    QwtText text(QString::fromLatin1(label));
    text.setColor(color);
    m->setLabel(text);
    m->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
    m->setZ(100.0);
    return m;
}
} // namespace

PlotCursors::PlotCursors(QwtPlot *plot) : QObject(plot), m_plot(plot)
{
    for (int n = 1; n <= kMaxCursors; ++n) {
        const QByteArray label = "C" + QByteArray::number(n);
        m_markers[size_t(n - 1)] = makeMarker(label.constData(), colorOf(n));
    }
    if (m_plot && m_plot->canvas()) {
        m_canvas = m_plot->canvas();
        m_plot->canvas()->setMouseTracking(true);
        m_plot->canvas()->installEventFilter(this);
        m_plot->installEventFilter(this);
    }
}

PlotCursors::~PlotCursors()
{
    // Attached markers are owned (and deleted) by the plot; only free detached ones.
    for (int n = 1; n <= kMaxCursors; ++n) {
        if (!m_has[size_t(n - 1)])
            delete m_markers[size_t(n - 1)];
    }
}

bool PlotCursors::hasC1() const { return hasCursor(1); }
bool PlotCursors::hasC2() const { return hasCursor(2); }
double PlotCursors::c1() const { return m_markers[0]->xValue(); }
double PlotCursors::c2() const { return m_markers[1]->xValue(); }

void PlotCursors::setMarker(int n, double t)
{
    QwtPlotMarker *m = m_markers[size_t(n - 1)];
    m->setXValue(t);
    if (!m_has[size_t(n - 1)]) {
        m_has[size_t(n - 1)] = true;
        m->setVisible(m_visible);
        m->attach(m_plot);
    }
    m_plot->replot();
    emit cursorsChanged();
}

void PlotCursors::setC1(double t) { setMarker(1, t); }
void PlotCursors::setC2(double t) { setMarker(2, t); }

bool PlotCursors::hasCursor(int n) const
{
    return n >= 1 && n <= kMaxCursors && m_has[size_t(n - 1)];
}

std::optional<double> PlotCursors::cursor(int n) const
{
    if (!hasCursor(n))
        return std::nullopt;
    return m_markers[size_t(n - 1)]->xValue();
}

void PlotCursors::setCursor(int n, double t)
{
    if (n >= 1 && n <= kMaxCursors)
        setMarker(n, t);
}

void PlotCursors::removeCursor(int n)
{
    if (!hasCursor(n))
        return;
    if (m_drag == n) {
        m_drag = 0;
        m_swallowRelease = false;
    }
    m_markers[size_t(n - 1)]->detach();
    m_has[size_t(n - 1)] = false;
    m_plot->replot();
    emit cursorsChanged();
}

QList<int> PlotCursors::cursorNumbers() const
{
    QList<int> out;
    for (int n = 1; n <= kMaxCursors; ++n) {
        if (hasCursor(n))
            out.append(n);
    }
    return out;
}

QColor PlotCursors::colorOf(int n)
{
    static const QColor colors[kMaxCursors] = {
        QColor(0xd6, 0x27, 0x28), QColor(0x1f, 0x77, 0xb4), QColor(0x2c, 0xa0, 0x2c),
        QColor(0x94, 0x67, 0xbd), QColor(0xff, 0x7f, 0x0e), QColor(0x8c, 0x56, 0x4b)};
    return n >= 1 && n <= kMaxCursors ? colors[n - 1] : QColor(Qt::black);
}

bool PlotCursors::isPlacing() const { return m_placing; }

int PlotCursors::lowestMissing() const
{
    for (int n = 1; n <= kMaxCursors; ++n) {
        if (!hasCursor(n))
            return n;
    }
    return 0;
}

void PlotCursors::setMarkersVisible(bool visible)
{
    if (m_visible == visible)
        return;
    m_visible = visible;
    for (QwtPlotMarker *m : m_markers)
        m->setVisible(visible);
    m_plot->replot();
}

void PlotCursors::setPlacing(bool on)
{
    if (m_placing == on)
        return;
    if (on && lowestMissing() == 0)
        return; // all six exist: nothing to place
    m_placing = on;
    m_placeSingle = on && hasC1() && hasC2();
    emit placingChanged(on);
}

void PlotCursors::clear()
{
    m_drag = 0;
    m_swallowRelease = false;
    for (int n = 1; n <= kMaxCursors; ++n) {
        m_markers[size_t(n - 1)]->detach();
        m_has[size_t(n - 1)] = false;
    }
    setPlacing(false);
    m_plot->replot();
    emit cursorsChanged();
}

int PlotCursors::markerAt(double px) const
{
    if (!m_visible)
        return 0;
    const QwtScaleMap map = m_plot->canvasMap(QwtAxis::XBottom);
    double best = kGrabPx + 1.0;
    int which = 0;
    for (int n = 1; n <= kMaxCursors; ++n) {
        if (!hasCursor(n))
            continue;
        const double d = std::abs(map.transform(m_markers[size_t(n - 1)]->xValue()) - px);
        if (d <= kGrabPx && d <= best) { // ties: the highest number wins
            best = d;
            which = n;
        }
    }
    return which;
}

void PlotCursors::updateHoverCursor(bool near)
{
    QWidget *canvas = m_plot->canvas();
    if (near && !m_cursorShown) {
        m_savedCursor = canvas->cursor(); // Qwt's canvas sets its own CrossCursor
        canvas->setCursor(Qt::SizeHorCursor);
        m_cursorShown = true;
    } else if (!near && m_cursorShown) {
        canvas->setCursor(m_savedCursor);
        m_cursorShown = false;
    }
}

bool PlotCursors::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape && m_placing) {
            setPlacing(false);
            return true;
        }
        return false;
    }

    if (!m_canvas || obj != m_canvas.data())
        return false;

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::LeftButton)
            return false;
        const double px = me->position().x();
        if (m_placing) {
            const double t = m_plot->canvasMap(QwtAxis::XBottom).invTransform(px);
            const int n = lowestMissing();
            if (n != 0) {
                setMarker(n, t);
                if (m_placeSingle || (hasC1() && hasC2()))
                    setPlacing(false);
            }
            m_swallowRelease = true;
            emit interacted();
            return true;
        }
        const int which = markerAt(px);
        if (which) {
            m_drag = which;
            updateHoverCursor(true);
            emit interacted();
            return true;
        }
        return false;
    }
    case QEvent::MouseMove: {
        auto *me = static_cast<QMouseEvent *>(event);
        const double px = me->position().x();
        if (m_drag) {
            const QwtScaleMap map = m_plot->canvasMap(QwtAxis::XBottom);
            const QwtInterval iv = m_plot->axisScaleDiv(QwtAxis::XBottom).interval();
            const double t = std::clamp(map.invTransform(px), iv.minValue(), iv.maxValue());
            setMarker(m_drag, t);
            return true;
        }
        updateHoverCursor(markerAt(px) != 0);
        return false;
    }
    case QEvent::MouseButtonRelease: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::LeftButton)
            return false;
        if (m_drag) {
            m_drag = 0;
            updateHoverCursor(markerAt(me->position().x()) != 0);
            return true;
        }
        if (m_swallowRelease) {
            m_swallowRelease = false;
            return true;
        }
        return false;
    }
    case QEvent::Leave:
        if (!m_drag)
            updateHoverCursor(false);
        return false;
    default:
        return false;
    }
}

} // namespace mb
