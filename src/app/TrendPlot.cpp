#include "TrendPlot.h"

#include "PlotCursors.h"
#include "Recorder.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QShortcut>
#include <QStringList>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <qwt_legend.h>
#include <qwt_legend_label.h>
#include <qwt_plot.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_plot_magnifier.h>
#include <qwt_plot_panner.h>
#include <qwt_scale_map.h>
#include <qwt_series_data.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace mb {

namespace {

constexpr double kWidth = 1.5;
constexpr double kThinWidth = 1.0; // wide pens are ~4x slower to stroke; used when very dense
constexpr qsizetype kDenseVisiblePoints = 150000;

/// Samples of one curve. x/y are stored with a dropped-prefix offset so that evicting a few
/// rows from the front is cheap; compacted lazily.
class TrendSeries : public QwtSeriesData<QPointF>
{
public:
    size_t size() const override { return size_t(m_x.size() - m_head); }
    QPointF sample(size_t i) const override { return {m_x[m_head + qsizetype(i)], m_y[m_head + qsizetype(i)]}; }
    QRectF boundingRect() const override { return QRectF(1.0, 1.0, -2.0, -2.0); } // invalid: we fit manually

    qsizetype count() const { return m_x.size() - m_head; }
    const double *xs() const { return m_x.constData() + m_head; }
    const double *ys() const { return m_y.constData() + m_head; }

    void append(double x, double y)
    {
        m_x.append(x);
        m_y.append(y);
    }
    void dropFront(qsizetype n)
    {
        m_head += std::min(n, count());
        if (m_head > 4096 && m_head > m_x.size() / 2) {
            m_x.remove(0, m_head);
            m_y.remove(0, m_head);
            m_head = 0;
        }
    }
    void clear()
    {
        m_x.clear();
        m_y.clear();
        m_head = 0;
    }

private:
    QList<double> m_x, m_y;
    qsizetype m_head = 0;
};

/// Curve drawing each finite run separately, so NaN breaks the line, and only the part of
/// the series inside the visible x range.
class TrendCurve : public QwtPlotCurve
{
public:
    explicit TrendCurve(const QString &title) : QwtPlotCurve(title)
    {
        m_series = new TrendSeries;
        setData(m_series);
        setPaintAttribute(QwtPlotCurve::FilterPointsAggressive, true);
    }

    TrendSeries *series() const { return m_series; }

    quint64 firstAbs = 0; ///< absolute sample number of series point 0
    quint64 nextAbs = 0;  ///< next absolute sample number to fetch

    void drawSeries(QPainter *painter, const QwtScaleMap &xMap, const QwtScaleMap &yMap, const QRectF &canvasRect,
                    int from, int to) const override
    {
        const qsizetype n = m_series->count();
        if (n <= 0)
            return;
        const double *x = m_series->xs();
        const double *y = m_series->ys();
        const double xlo = std::min(xMap.s1(), xMap.s2());
        const double xhi = std::max(xMap.s1(), xMap.s2());
        qsizetype lo = std::lower_bound(x, x + n, xlo) - x;
        qsizetype hi = std::upper_bound(x, x + n, xhi) - x; // exclusive
        if (lo > 0)
            --lo;
        if (hi < n)
            ++hi;
        if (to >= 0)
            hi = std::min<qsizetype>(hi, to + 1);
        lo = std::max<qsizetype>(lo, from);

        qsizetype i = lo;
        while (i < hi) {
            while (i < hi && std::isnan(y[i]))
                ++i;
            const qsizetype start = i;
            while (i < hi && !std::isnan(y[i]))
                ++i;
            if (i - start >= 2) {
                QwtPlotCurve::drawSeries(painter, xMap, yMap, canvasRect, int(start), int(i - 1));
            } else if (i - start == 1) {
                painter->save();
                QPen p = pen();
                p.setCapStyle(Qt::RoundCap);
                painter->setPen(p);
                painter->drawPoint(QPointF(xMap.transform(x[start]), yMap.transform(y[start])));
                painter->restore();
            }
        }
    }

private:
    TrendSeries *m_series = nullptr;
};

/// Wheel zoom: x axis normally, y axis with Shift. Reports every zoom.
class TrendMagnifier : public QwtPlotMagnifier
{
public:
    TrendMagnifier(QWidget *canvas, std::function<void(bool yAxis)> onZoom)
        : QwtPlotMagnifier(canvas), m_onZoom(std::move(onZoom))
    {
        setMouseButton(Qt::NoButton);
    }

protected:
    void widgetWheelEvent(QWheelEvent *e) override
    {
        m_y = e->modifiers().testFlag(Qt::ShiftModifier);
        setAxisEnabled(QwtPlot::xBottom, !m_y);
        setAxisEnabled(QwtPlot::yLeft, m_y);
        setAxisEnabled(QwtPlot::yRight, m_y);
        QwtPlotMagnifier::widgetWheelEvent(e);
    }
    void rescale(double factor) override
    {
        m_onZoom(m_y); // pause first so the new scale is not overwritten
        QwtPlotMagnifier::rescale(factor);
    }

private:
    std::function<void(bool)> m_onZoom;
    bool m_y = false;
};

} // namespace

struct TrendPlot::CurveEntry
{
    TagId id = 0;
    TrendCurve *curve = nullptr;
};

TrendPlot::TrendPlot(QWidget *parent) : QWidget(parent), m_plot(new QwtPlot(this))
{
    // Controls
    m_windowCombo = new QComboBox(this);
    m_windowCombo->addItem(tr("60 s"), 60.0);
    m_windowCombo->addItem(tr("5 min"), 300.0);
    m_windowCombo->addItem(tr("30 min"), 1800.0);
    m_windowCombo->addItem(tr("All"), 0.0);

    m_pauseButton = new QToolButton(this);
    m_pauseButton->setCheckable(true);
    m_pauseButton->setText(tr("Pause"));

    m_placeButton = new QPushButton(tr("Place cursors"), this);
    m_placeButton->setCheckable(true);
    auto *clearButton = new QPushButton(tr("Clear cursors"), this);
    auto *fitButton = new QPushButton(tr("Fit Y"), this);
    auto *clearDataButton = new QPushButton(tr("Clear data"), this);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_windowCombo);
    row->addWidget(m_pauseButton);
    row->addWidget(m_placeButton);
    row->addWidget(clearButton);
    row->addWidget(fitButton);
    row->addWidget(clearDataButton);
    row->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(row);
    layout->addWidget(m_plot, 1);

    // Plot
    auto *canvas = new QwtPlotCanvas;
    canvas->setPaintAttribute(QwtPlotCanvas::BackingStore, true);
    canvas->setFrameStyle(QFrame::Box | QFrame::Plain);
    m_plot->setCanvas(canvas);
    m_plot->setCanvasBackground(Qt::white);
    m_plot->setAutoReplot(false);
    m_plot->setAxisTitle(QwtPlot::xBottom, tr("Time (s)"));
    m_plot->setAxisScale(QwtPlot::xBottom, 0.0, 60.0);
    m_plot->setAxisScale(QwtPlot::yLeft, -1.0, 1.0);
    m_plot->setAxisScale(QwtPlot::yRight, -1.0, 1.0);
    m_plot->setAxisVisible(QwtPlot::yRight, false);

    auto *grid = new QwtPlotGrid;
    grid->setPen(QColor(200, 200, 200), 0.0, Qt::DotLine);
    grid->attach(m_plot);

    auto *legend = new QwtLegend;
    legend->setDefaultItemMode(QwtLegendData::Checkable);
    m_plot->insertLegend(legend, QwtPlot::BottomLegend);
    connect(legend, &QwtLegend::checked, this, [this](const QVariant &info, bool on, int) {
        if (QwtPlotItem *item = m_plot->infoToItem(info)) {
            item->setVisible(on);
            m_plot->replot();
        }
    });

    m_cursors = new PlotCursors(m_plot);

    new TrendMagnifier(canvas, [this](bool yAxis) {
        setPaused(true);
        if (yAxis)
            m_autoY = false;
    });
    auto *panner = new QwtPlotPanner(canvas);
    panner->setMouseButton(Qt::RightButton);
    connect(panner, &QwtPanner::panned, this, [this](int, int dy) {
        setPaused(true);
        if (dy != 0)
            m_autoY = false;
    });

    // Wiring
    connect(m_windowCombo, &QComboBox::currentIndexChanged, this,
            [this](int idx) { setWindowSeconds(m_windowCombo->itemData(idx).toDouble()); });
    connect(m_pauseButton, &QToolButton::toggled, this, &TrendPlot::setPaused);
    connect(m_placeButton, &QPushButton::toggled, m_cursors, &PlotCursors::setPlacing);
    connect(m_cursors, &PlotCursors::placingChanged, this, [this](bool on) {
        const QSignalBlocker b(m_placeButton);
        m_placeButton->setChecked(on);
    });
    connect(clearButton, &QPushButton::clicked, m_cursors, &PlotCursors::clear);
    connect(clearDataButton, &QPushButton::clicked, this, &TrendPlot::clearDataRequested);
    // Esc cancels placing wherever the focus is inside the trend (the cursors' own key
    // handling needs the plot to have focus).
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, m_cursors, [this] { m_cursors->setPlacing(false); });
    connect(m_cursors, &PlotCursors::interacted, this, [this] { setPaused(true); });
    connect(fitButton, &QPushButton::clicked, this, [this] {
        m_autoY = true;
        const QwtInterval xi = m_plot->axisInterval(QwtPlot::xBottom);
        fitY(xi.minValue(), xi.maxValue());
        m_plot->replot();
    });
}

TrendPlot::~TrendPlot()
{
    // Curves are owned by the plot (deleted with it); only free our bookkeeping.
    qDeleteAll(m_entries);
}

void TrendPlot::setRecorder(const Recorder *recorder)
{
    m_recorder = recorder;
    for (CurveEntry *e : std::as_const(m_entries)) {
        e->curve->series()->clear();
        e->curve->firstAbs = e->curve->nextAbs = m_recorder ? m_recorder->evictedCount() : 0;
    }
    pull();
    applyAxes();
    m_plot->replot();
}

void TrendPlot::setTags(const QList<Tag> &tags)
{
    QList<CurveEntry *> next;
    QStringList leftUnits, rightUnits;
    bool anyRight = false;
    for (const Tag &tag : tags) {
        if (!tag.plotted)
            continue;
        CurveEntry *entry = nullptr;
        for (CurveEntry *e : std::as_const(m_entries)) {
            if (e->id == tag.id) {
                entry = e;
                break;
            }
        }
        if (entry) {
            m_entries.removeOne(entry);
        } else {
            entry = new CurveEntry;
            entry->id = tag.id;
            entry->curve = new TrendCurve(QString());
            entry->curve->attach(m_plot);
            entry->curve->firstAbs = entry->curve->nextAbs = m_recorder ? m_recorder->evictedCount() : 0;
        }
        const QString title = tag.unit.isEmpty() ? tag.name : QStringLiteral("%1 [%2]").arg(tag.name, tag.unit);
        entry->curve->setTitle(title);
        entry->curve->setYAxis(tag.axis == YAxis::Right ? QwtPlot::yRight : QwtPlot::yLeft);
        QStringList &units = tag.axis == YAxis::Right ? rightUnits : leftUnits;
        if (!tag.unit.isEmpty() && !units.contains(tag.unit))
            units.append(tag.unit);
        if (tag.axis == YAxis::Right)
            anyRight = true;
        QColor c(tag.color);
        if (!c.isValid())
            c = Qt::black;
        entry->curve->setPen(c, m_thinPens ? kThinWidth : kWidth);
        next.append(entry);
    }
    for (CurveEntry *e : std::as_const(m_entries)) {
        e->curve->detach();
        delete e->curve;
        delete e;
    }
    m_entries = next;

    m_plot->setAxisTitle(QwtPlot::yLeft, leftUnits.join(QStringLiteral(", ")));
    m_plot->setAxisTitle(QwtPlot::yRight, rightUnits.join(QStringLiteral(", ")));
    m_plot->setAxisVisible(QwtPlot::yRight, anyRight);

    pull(); // new curves backfill from the recorder
    if (auto *legend = qobject_cast<QwtLegend *>(m_plot->legend())) {
        for (CurveEntry *e : std::as_const(m_entries)) {
            if (auto *label = qobject_cast<QwtLegendLabel *>(legend->legendWidget(m_plot->itemToInfo(e->curve))))
                label->setChecked(e->curve->isVisible());
        }
    }
    applyAxes();
    adaptPens();
    m_plot->replot();
}

QwtPlot *TrendPlot::plot() const { return m_plot; }
PlotCursors *TrendPlot::cursors() const { return m_cursors; }
double TrendPlot::windowSeconds() const { return m_window; }
bool TrendPlot::isPaused() const { return m_paused; }

bool TrendPlot::pull()
{
    if (!m_recorder)
        return false;
    const quint64 evicted = m_recorder->evictedCount();
    const qsizetype size = m_recorder->size();
    const quint64 end = evicted + quint64(size);
    bool changed = false;
    for (CurveEntry *e : std::as_const(m_entries)) {
        TrendCurve *c = e->curve;
        TrendSeries *s = c->series();
        if (c->nextAbs > end) { // recorder was cleared behind our back
            s->clear();
            c->firstAbs = c->nextAbs = evicted;
            changed = true;
        }
        if (c->nextAbs < evicted) { // rows we never saw are gone
            s->clear();
            c->firstAbs = c->nextAbs = evicted;
            changed = true;
        } else if (c->firstAbs < evicted) {
            s->dropFront(qsizetype(std::min<quint64>(evicted - c->firstAbs, quint64(s->count()))));
            c->firstAbs = evicted;
            changed = true;
        }
        if (c->nextAbs < end) {
            for (quint64 abs = c->nextAbs; abs < end; ++abs) {
                const qsizetype row = qsizetype(abs - evicted);
                s->append(m_recorder->timeAt(row), m_recorder->valueAt(row, e->id));
            }
            c->nextAbs = end;
            changed = true;
        }
    }
    return changed;
}

void TrendPlot::fitY(double x1, double x2)
{
    fitAxis(QwtPlot::yLeft, x1, x2);
    fitAxis(QwtPlot::yRight, x1, x2);
}

void TrendPlot::fitAxis(QwtAxisId axis, double x1, double x2)
{
    double lo = std::numeric_limits<double>::infinity();
    double hi = -lo;
    for (CurveEntry *e : std::as_const(m_entries)) {
        if (!e->curve->isVisible() || e->curve->yAxis() != axis)
            continue;
        const TrendSeries *s = e->curve->series();
        const qsizetype n = s->count();
        const double *x = s->xs();
        const double *y = s->ys();
        qsizetype a = std::lower_bound(x, x + n, x1) - x;
        const qsizetype b = std::upper_bound(x, x + n, x2) - x;
        for (; a < b; ++a) {
            if (y[a] < lo)
                lo = y[a];
            if (y[a] > hi)
                hi = y[a]; // NaN fails both comparisons
        }
    }
    if (!(lo <= hi))
        return;
    const double span = hi - lo;
    if (span < 1e-9) {
        lo -= 1.0;
        hi += 1.0;
    } else {
        lo -= span * 0.05;
        hi += span * 0.05;
    }
    m_plot->setAxisScale(axis, lo, hi);
}

void TrendPlot::applyAxes()
{
    if (m_paused || !m_recorder || m_recorder->size() == 0)
        return;
    const double tLast = m_recorder->timeAt(m_recorder->size() - 1);
    const double x1 = m_window > 0.0 ? tLast - m_window : m_recorder->timeAt(0);
    double x2 = tLast;
    if (x2 - x1 < 1e-6)
        x2 = x1 + 1.0;
    m_plot->setAxisScale(QwtPlot::xBottom, x1, x2);
    if (m_autoY)
        fitY(x1, x2);
}

void TrendPlot::adaptPens()
{
    const QwtInterval xi = m_plot->axisInterval(QwtPlot::xBottom);
    qsizetype visible = 0;
    for (CurveEntry *e : std::as_const(m_entries)) {
        const TrendSeries *s = e->curve->series();
        const double *x = s->xs();
        const qsizetype n = s->count();
        visible += (std::upper_bound(x, x + n, xi.maxValue()) - x) - (std::lower_bound(x, x + n, xi.minValue()) - x);
    }
    const bool thin = visible > kDenseVisiblePoints;
    if (thin == m_thinPens)
        return;
    m_thinPens = thin;
    for (CurveEntry *e : std::as_const(m_entries)) {
        QPen p = e->curve->pen();
        p.setWidthF(thin ? kThinWidth : kWidth);
        e->curve->setPen(p);
    }
}

void TrendPlot::refresh()
{
    if (!m_recorder)
        return;
    if (!pull())
        return;
    applyAxes();
    adaptPens();
    m_plot->replot();
}

void TrendPlot::setWindowSeconds(double seconds)
{
    m_window = seconds < 0.0 ? 0.0 : seconds;
    const int idx = m_windowCombo->findData(m_window);
    if (idx >= 0 && idx != m_windowCombo->currentIndex()) {
        const QSignalBlocker b(m_windowCombo);
        m_windowCombo->setCurrentIndex(idx);
    }
    if (!m_paused) {
        applyAxes();
        m_plot->replot();
    }
}

void TrendPlot::setPaused(bool paused)
{
    if (m_pauseButton->isChecked() != paused) {
        const QSignalBlocker b(m_pauseButton);
        m_pauseButton->setChecked(paused);
    }
    m_pauseButton->setText(paused ? tr("Run") : tr("Pause"));
    if (paused == m_paused)
        return;
    m_paused = paused;
    if (!paused) {
        m_autoY = true;
        applyAxes();
        m_plot->replot();
    }
    emit pausedChanged(paused);
}

void TrendPlot::resetData()
{
    // Start at the oldest row still held, so pull() reloads everything the recorder has.
    const quint64 mark = m_recorder ? m_recorder->evictedCount() : 0;
    for (CurveEntry *e : std::as_const(m_entries)) {
        e->curve->series()->clear();
        e->curve->firstAbs = e->curve->nextAbs = mark;
    }
    pull();
    m_plot->replot();
}

// Stubs created by T220; implemented by T221.
std::optional<std::pair<double, double>> TrendPlot::axisRange(QwtAxisId axis) const
{
    return axis == QwtAxis::YRight ? m_rightRange : m_leftRange;
}

void TrendPlot::setAxisRange(QwtAxisId axis, std::optional<std::pair<double, double>> range)
{
    (axis == QwtAxis::YRight ? m_rightRange : m_leftRange) = range;
}

QMenu *TrendPlot::axisMenu(QwtAxisId)
{
    return nullptr;
}

} // namespace mb
