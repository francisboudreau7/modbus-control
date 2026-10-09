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
    m_c1 = makeMarker("C1", QColor(0xd6, 0x27, 0x28));
    m_c2 = makeMarker("C2", QColor(0x1f, 0x77, 0xb4));
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
    if (!m_has1)
        delete m_c1;
    if (!m_has2)
        delete m_c2;
}

bool PlotCursors::hasC1() const { return m_has1; }
bool PlotCursors::hasC2() const { return m_has2; }
double PlotCursors::c1() const { return m_c1->xValue(); }
double PlotCursors::c2() const { return m_c2->xValue(); }

void PlotCursors::setMarker(int which, double t)
{
    QwtPlotMarker *m = which == 1 ? m_c1 : m_c2;
    bool &has = which == 1 ? m_has1 : m_has2;
    m->setXValue(t);
    if (!has) {
        has = true;
        m->setVisible(m_visible);
        m->attach(m_plot);
    }
    m_plot->replot();
    emit cursorsChanged();
}

void PlotCursors::setC1(double t) { setMarker(1, t); }
void PlotCursors::setC2(double t) { setMarker(2, t); }

bool PlotCursors::isPlacing() const { return m_placing; }

void PlotCursors::setMarkersVisible(bool visible)
{
    if (m_visible == visible)
        return;
    m_visible = visible;
    m_c1->setVisible(visible);
    m_c2->setVisible(visible);
    m_plot->replot();
}

void PlotCursors::setPlacing(bool on)
{
    if (m_placing == on)
        return;
    m_placing = on;
    m_placeStep = 0;
    emit placingChanged(on);
}

void PlotCursors::clear()
{
    m_drag = 0;
    m_swallowRelease = false;
    m_c1->detach();
    m_c2->detach();
    m_has1 = m_has2 = false;
    setPlacing(false);
    m_plot->replot();
    emit cursorsChanged();
}

int PlotCursors::markerAt(double px) const
{
    const QwtScaleMap map = m_plot->canvasMap(QwtAxis::XBottom);
    double best = kGrabPx + 1.0;
    int which = 0;
    if (m_has1 && m_visible) {
        const double d = std::abs(map.transform(m_c1->xValue()) - px);
        if (d <= kGrabPx && d < best) {
            best = d;
            which = 1;
        }
    }
    if (m_has2 && m_visible) {
        const double d = std::abs(map.transform(m_c2->xValue()) - px);
        if (d <= kGrabPx && d <= best)
            which = 2;
    }
    return which;
}

void PlotCursors::updateHoverCursor(bool near)
{
    QWidget *canvas = m_plot->canvas();
    if (near && !m_cursorShown) {
        canvas->setCursor(Qt::SizeHorCursor);
        m_cursorShown = true;
    } else if (!near && m_cursorShown) {
        canvas->unsetCursor();
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
            const int step = m_placeStep;
            if (step == 0) {
                m_placeStep = 1;
                setC1(t);
            } else {
                setC2(t);
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
