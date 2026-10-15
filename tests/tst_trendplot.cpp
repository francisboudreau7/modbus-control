#include "Recorder.h"
#include "SampleBatch.h"
#include "Tag.h"
#include "TrendPlot.h"

#include <QAction>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QToolButton>
#include <QWheelEvent>
#include <QtTest>

#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_scale_div.h>
#include <qwt_scale_widget.h>
#include <qwt_text.h>

#include <cmath>
#include <limits>

using namespace mb;

namespace {

Tag makeTag(TagId id, const QString &name, const QString &unit, YAxis axis)
{
    Tag t;
    t.id = id;
    t.name = name;
    t.unit = unit;
    t.axis = axis;
    t.plotted = true;
    return t;
}

TagSample good(TagId id, double value)
{
    TagSample s;
    s.tagId = id;
    s.value = value;
    s.quality = Quality::Good;
    return s;
}

TagSample bad(TagId id)
{
    TagSample s;
    s.tagId = id;
    s.quality = Quality::Timeout;
    return s;
}

void addRow(Recorder &r, double t, QList<TagSample> samples)
{
    SampleBatch b;
    b.t = t;
    b.samples = std::move(samples);
    r.append(b);
}

QList<QwtPlotCurve *> curves(QwtPlot *plot)
{
    QList<QwtPlotCurve *> out;
    const QwtPlotItemList items = plot->itemList(QwtPlotItem::Rtti_PlotCurve);
    for (QwtPlotItem *i : items)
        out.append(static_cast<QwtPlotCurve *>(i));
    return out;
}

QwtPlotCurve *curveTitled(QwtPlot *plot, const QString &prefix)
{
    for (QwtPlotCurve *c : curves(plot))
        if (c->title().text().startsWith(prefix))
            return c;
    return nullptr;
}

QList<Tag> threeTags()
{
    return {makeTag(1, QStringLiteral("A"), QStringLiteral("V"), YAxis::Left),
            makeTag(2, QStringLiteral("B"), QStringLiteral("%"), YAxis::Left),
            makeTag(3, QStringLiteral("C"), QStringLiteral("degC"), YAxis::Right)};
}

QAction *findAction(QMenu *menu, const QString &text)
{
    for (QAction *a : menu->actions())
        if (a->text() == text)
            return a;
    return nullptr;
}

void sendWheel(QWidget *w, int delta, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    const QPointF pos(w->width() / 2.0, w->height() / 2.0);
    QWheelEvent e(pos, w->mapToGlobal(pos), QPoint(), QPoint(0, delta), Qt::NoButton, mods,
                  Qt::NoScrollPhase, false);
    QApplication::sendEvent(w, &e);
}

} // namespace

class TestTrendPlot : public QObject
{
    Q_OBJECT
private slots:
    void resetDataReloadsReplacedRecording();
    void init()
    {
        m_rec = std::make_unique<Recorder>();
        for (int i = 0; i < 20; ++i)
            addRow(*m_rec, i, {good(1, i / 20.0), good(2, 0.5), good(3, 1000.0 + 50.0 * i)});
    }

    void axesAndTitles()
    {
        TrendPlot tp;
        tp.setRecorder(m_rec.get());
        QList<Tag> tags = threeTags();
        tp.setTags(tags);
        QwtPlot *p = tp.plot();
        QCOMPARE(curves(p).size(), 3);
        QCOMPARE(curveTitled(p, "A")->yAxis(), QwtPlot::yLeft);
        QCOMPARE(curveTitled(p, "B")->yAxis(), QwtPlot::yLeft);
        QCOMPARE(curveTitled(p, "C")->yAxis(), QwtPlot::yRight);
        QVERIFY(p->isAxisVisible(QwtPlot::yRight));
        QCOMPARE(p->axisTitle(QwtPlot::yLeft).text(), QStringLiteral("V, %"));
        QCOMPARE(p->axisTitle(QwtPlot::yRight).text(), QStringLiteral("degC"));

        for (Tag &t : tags)
            t.axis = YAxis::Left;
        tp.setTags(tags);
        QVERIFY(!p->isAxisVisible(QwtPlot::yRight));
        QCOMPARE(p->axisTitle(QwtPlot::yLeft).text(), QStringLiteral("V, %, degC"));
        QCOMPARE(curves(p).size(), 3);
    }

    void separateAutoScale()
    {
        TrendPlot tp;
        tp.setRecorder(m_rec.get());
        tp.setTags(threeTags());
        tp.refresh();
        QwtPlot *p = tp.plot();
        const QwtScaleDiv l = p->axisScaleDiv(QwtPlot::yLeft);
        const QwtScaleDiv r = p->axisScaleDiv(QwtPlot::yRight);
        QVERIFY(l.lowerBound() <= 0.0 && l.upperBound() >= 0.95);
        QVERIFY(l.upperBound() < 100.0);
        QVERIFY(r.lowerBound() <= 1000.0 && r.upperBound() >= 1950.0);
        QVERIFY(r.lowerBound() > 500.0);
    }

    void fitYFitsBothAxes()
    {
        TrendPlot tp;
        tp.setRecorder(m_rec.get());
        tp.setTags(threeTags());
        QwtPlot *p = tp.plot();
        p->setAxisScale(QwtPlot::yLeft, -500.0, 500.0);
        p->setAxisScale(QwtPlot::yRight, 0.0, 10.0);
        QPushButton *fit = nullptr;
        for (QPushButton *b : tp.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Fit Y"))
                fit = b;
        QVERIFY(fit);
        fit->click();
        const QwtScaleDiv l = p->axisScaleDiv(QwtPlot::yLeft);
        const QwtScaleDiv r = p->axisScaleDiv(QwtPlot::yRight);
        QVERIFY(l.upperBound() < 10.0 && l.lowerBound() > -10.0);
        QVERIFY(r.lowerBound() <= 1000.0 && r.upperBound() >= 1950.0);
        QVERIFY(r.upperBound() < 3000.0);
    }

    void movingAxisKeepsData()
    {
        TrendPlot tp;
        tp.setRecorder(m_rec.get());
        QList<Tag> tags = threeTags();
        tp.setTags(tags);
        QwtPlotCurve *c = curveTitled(tp.plot(), "A");
        QCOMPARE(c->dataSize(), size_t(20));
        tags[0].axis = YAxis::Right;
        tp.setTags(tags);
        QCOMPARE(curveTitled(tp.plot(), "A"), c);
        QCOMPARE(c->yAxis(), QwtPlot::yRight);
        QCOMPARE(c->dataSize(), size_t(20));
        QCOMPARE(tp.plot()->axisTitle(QwtPlot::yRight).text(), QStringLiteral("V, degC"));
    }

    void nanGapsAndCurveCount()
    {
        Recorder rec;
        addRow(rec, 0, {good(1, 1.0), good(3, 1000.0)});
        addRow(rec, 1, {bad(1), bad(3)});
        addRow(rec, 2, {good(1, 2.0), good(3, 1100.0)});
        TrendPlot tp;
        tp.setRecorder(&rec);
        tp.setTags(threeTags());
        tp.refresh();
        tp.grab();
        QCOMPARE(curves(tp.plot()).size(), 3);
        QList<Tag> tags = threeTags();
        tags[1].plotted = false;
        tp.setTags(tags);
        QCOMPARE(curves(tp.plot()).size(), 2);
    }

    void axisRangeRoundTrip()
    {
        TrendPlot tp;
        tp.setRecorder(m_rec.get());
        tp.setTags(threeTags());
        QSignalSpy spy(&tp, &TrendPlot::axisRangesChanged);
        QVERIFY(!tp.axisRange(QwtAxis::YLeft));
        tp.setAxisRange(QwtAxis::YLeft, std::make_pair(-5.0, 7.0));
        tp.setAxisRange(QwtAxis::YRight, std::make_pair(0.0, 3000.0));
        QCOMPARE(*tp.axisRange(QwtAxis::YLeft), std::make_pair(-5.0, 7.0));
        QCOMPARE(*tp.axisRange(QwtAxis::YRight), std::make_pair(0.0, 3000.0));
        QCOMPARE(tp.plot()->axisInterval(QwtAxis::YLeft).minValue(), -5.0);
        QCOMPARE(tp.plot()->axisInterval(QwtAxis::YLeft).maxValue(), 7.0);
        // Invalid ranges are ignored.
        tp.setAxisRange(QwtAxis::YLeft, std::make_pair(3.0, 3.0));
        tp.setAxisRange(QwtAxis::YLeft, std::make_pair(4.0, 1.0));
        tp.setAxisRange(QwtAxis::YLeft, std::make_pair(0.0, std::numeric_limits<double>::infinity()));
        tp.setAxisRange(QwtAxis::YLeft, std::make_pair(std::nan(""), 1.0));
        QCOMPARE(*tp.axisRange(QwtAxis::YLeft), std::make_pair(-5.0, 7.0));
        tp.setAxisRange(QwtAxis::YLeft, std::nullopt);
        QVERIFY(!tp.axisRange(QwtAxis::YLeft));
        QVERIFY(tp.plot()->axisInterval(QwtAxis::YLeft).maxValue() < 10.0); // refitted
        QCOMPARE(spy.count(), 0);
    }

    void fixedAxisSurvivesRefreshAndTags()
    {
        TrendPlot tp;
        tp.setRecorder(m_rec.get());
        tp.setTags(threeTags());
        tp.setAxisRange(QwtAxis::YLeft, std::make_pair(-10.0, 10.0));
        addRow(*m_rec, 20, {good(1, 5000.0), good(2, 0.5), good(3, 9000.0)});
        tp.refresh();
        QCOMPARE(tp.plot()->axisInterval(QwtAxis::YLeft).minValue(), -10.0);
        QCOMPARE(tp.plot()->axisInterval(QwtAxis::YLeft).maxValue(), 10.0);
        QVERIFY(tp.plot()->axisInterval(QwtAxis::YRight).maxValue() >= 9000.0); // Auto follows
        tp.setTags(threeTags());
        tp.setPaused(true);
        tp.setPaused(false); // Run keeps fixed ranges
        QCOMPARE(tp.plot()->axisInterval(QwtAxis::YLeft).maxValue(), 10.0);
    }

    void fitYSetsAutoAndEmitsOnce()
    {
        TrendPlot tp;
        tp.setRecorder(m_rec.get());
        tp.setTags(threeTags());
        tp.setAxisRange(QwtAxis::YLeft, std::make_pair(-500.0, 500.0));
        tp.setAxisRange(QwtAxis::YRight, std::make_pair(0.0, 10.0));
        QSignalSpy spy(&tp, &TrendPlot::axisRangesChanged);
        for (QPushButton *b : tp.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Fit Y"))
                b->click();
        QCOMPARE(spy.count(), 1);
        QVERIFY(!tp.axisRange(QwtAxis::YLeft));
        QVERIFY(!tp.axisRange(QwtAxis::YRight));
        QVERIFY(tp.plot()->axisInterval(QwtAxis::YLeft).maxValue() < 10.0);
        QVERIFY(tp.plot()->axisInterval(QwtAxis::YRight).maxValue() >= 1950.0);
    }

    void fitYWithoutFixedAxesDoesNotEmit()
    {
        TrendPlot tp;
        tp.setRecorder(m_rec.get());
        tp.setTags(threeTags());
        QSignalSpy spy(&tp, &TrendPlot::axisRangesChanged);
        for (QPushButton *b : tp.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Fit Y"))
                b->click();
        QCOMPARE(spy.count(), 0);
    }

    void autoWithoutDataDropsStaleFixedScale()
    {
        TrendPlot tp; // no recorder, no data
        tp.setAxisRange(QwtAxis::YLeft, std::make_pair(-500.0, 500.0));
        QCOMPARE(tp.plot()->axisInterval(QwtAxis::YLeft).maxValue(), 500.0);
        tp.setAxisRange(QwtAxis::YLeft, std::nullopt);
        QVERIFY(!tp.axisRange(QwtAxis::YLeft));
        QCOMPARE(tp.plot()->axisInterval(QwtAxis::YLeft).minValue(), 0.0);
        QCOMPARE(tp.plot()->axisInterval(QwtAxis::YLeft).maxValue(), 1.0);
    }

    void ctrlWheelDoesNotZoom()
    {
        TrendPlot tp;
        tp.resize(800, 600);
        tp.setRecorder(m_rec.get());
        tp.setTags(threeTags());
        tp.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tp));
        const QwtInterval x0 = tp.plot()->axisInterval(QwtAxis::XBottom);
        const QwtInterval y0 = tp.plot()->axisInterval(QwtAxis::YLeft);
        for (Qt::KeyboardModifiers m : {Qt::KeyboardModifiers(Qt::ControlModifier), Qt::KeyboardModifiers(Qt::AltModifier)})
            sendWheel(tp.plot()->canvas(), 120, m);
        QCOMPARE(tp.plot()->axisInterval(QwtAxis::XBottom).minValue(), x0.minValue());
        QCOMPARE(tp.plot()->axisInterval(QwtAxis::XBottom).maxValue(), x0.maxValue());
        QCOMPARE(tp.plot()->axisInterval(QwtAxis::YLeft).minValue(), y0.minValue());
        QCOMPARE(tp.plot()->axisInterval(QwtAxis::YLeft).maxValue(), y0.maxValue());
        QVERIFY(!tp.axisRange(QwtAxis::YLeft));
    }

    void rightDragWobbleDoesNotFixYAxes()
    {
        TrendPlot tp;
        tp.resize(800, 600);
        tp.setRecorder(m_rec.get());
        tp.setTags(threeTags());
        tp.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tp));
        QWidget *canvas = tp.plot()->canvas();
        const QPoint start(canvas->width() / 2, canvas->height() / 2);
        // Mostly horizontal drag with a 2 px vertical wobble: a time pan only.
        QTest::mousePress(canvas, Qt::RightButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas, start + QPoint(-40, 1));
        QTest::mouseMove(canvas, start + QPoint(-80, 2));
        QTest::mouseRelease(canvas, Qt::RightButton, Qt::NoModifier, start + QPoint(-80, 2));
        QVERIFY(!tp.axisRange(QwtAxis::YLeft));
        QVERIFY(!tp.axisRange(QwtAxis::YRight));
        // A real vertical drag does fix them.
        QTest::mousePress(canvas, Qt::RightButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas, start + QPoint(0, 20));
        QTest::mouseMove(canvas, start + QPoint(0, 40));
        QTest::mouseRelease(canvas, Qt::RightButton, Qt::NoModifier, start + QPoint(0, 40));
        QVERIFY(tp.axisRange(QwtAxis::YLeft));
    }

    void axisMenuActions()
    {
        TrendPlot tp;
        tp.setRecorder(m_rec.get());
        tp.setTags(threeTags());
        QSignalSpy spy(&tp, &TrendPlot::axisRangesChanged);
        QMenu *menu = tp.axisMenu(QwtAxis::YLeft);
        QVERIFY(menu);
        QAction *set = findAction(menu, QStringLiteral("Set range..."));
        QAction *zero = findAction(menu, QStringLiteral("0 to 100"));
        QAction *autoA = findAction(menu, QStringLiteral("Auto"));
        QVERIFY(set && zero && autoA);

        int asked = 0;
        std::pair<double, double> seen;
        tp.m_askRange = [&](QwtAxisId, std::pair<double, double> cur) {
            ++asked;
            seen = cur;
            return std::optional<std::pair<double, double>>(std::make_pair(2.0, 4.0));
        };
        set->trigger();
        QCOMPARE(asked, 1);
        QVERIFY(seen.first < seen.second);
        QCOMPARE(*tp.axisRange(QwtAxis::YLeft), std::make_pair(2.0, 4.0));
        QCOMPARE(spy.count(), 1);

        tp.m_askRange = [&](QwtAxisId, std::pair<double, double>) {
            ++asked;
            return std::optional<std::pair<double, double>>();
        };
        set->trigger(); // cancelled
        QCOMPARE(asked, 2);
        QCOMPARE(*tp.axisRange(QwtAxis::YLeft), std::make_pair(2.0, 4.0));
        QCOMPARE(spy.count(), 1);

        zero->trigger();
        QCOMPARE(*tp.axisRange(QwtAxis::YLeft), std::make_pair(0.0, 100.0));
        QCOMPARE(tp.plot()->axisInterval(QwtAxis::YLeft).maxValue(), 100.0);
        QCOMPARE(spy.count(), 2);
        QVERIFY(!tp.axisRange(QwtAxis::YRight));

        autoA->trigger();
        QVERIFY(!tp.axisRange(QwtAxis::YLeft));
        QVERIFY(tp.plot()->axisInterval(QwtAxis::YLeft).maxValue() < 10.0);
        QCOMPARE(spy.count(), 3);
    }

    void yAxesButtonSubmenus()
    {
        TrendPlot tp;
        tp.setRecorder(m_rec.get());
        QList<Tag> tags = threeTags();
        tags[2].axis = YAxis::Left;
        tp.setTags(tags);
        auto *button = tp.findChild<QToolButton *>(QStringLiteral("yAxesButton"));
        QVERIFY(button && button->menu());
        QAction *left = findAction(button->menu(), QStringLiteral("Left"));
        QAction *right = findAction(button->menu(), QStringLiteral("Right"));
        QVERIFY(left && right && left->menu() && right->menu());
        QVERIFY(left->isEnabled());
        QVERIFY(!right->isEnabled());
        tp.setTags(threeTags());
        QVERIFY(right->isEnabled());
        QSignalSpy spy(&tp, &TrendPlot::axisRangesChanged);
        findAction(right->menu(), QStringLiteral("0 to 100"))->trigger();
        QCOMPARE(*tp.axisRange(QwtAxis::YRight), std::make_pair(0.0, 100.0));
        QVERIFY(!tp.axisRange(QwtAxis::YLeft));
        QCOMPARE(spy.count(), 1);
    }

    void wheelOverAxisZoomsOnlyThatAxis()
    {
        TrendPlot tp;
        tp.resize(800, 600);
        tp.setRecorder(m_rec.get());
        tp.setTags(threeTags());
        tp.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tp));
        const QwtInterval before = tp.plot()->axisInterval(QwtAxis::YLeft);
        const QwtInterval rightBefore = tp.plot()->axisInterval(QwtAxis::YRight);
        QSignalSpy spy(&tp, &TrendPlot::axisRangesChanged);
        QWidget *axisWidget = tp.plot()->axisWidget(QwtAxis::YLeft);
        sendWheel(axisWidget, 120); // zoom in
        QCOMPARE(spy.count(), 1);
        QVERIFY(tp.axisRange(QwtAxis::YLeft));
        QVERIFY(!tp.axisRange(QwtAxis::YRight));
        const auto r = *tp.axisRange(QwtAxis::YLeft);
        QVERIFY(qAbs((r.second - r.first) - (before.maxValue() - before.minValue()) * 0.8) < 1e-9);
        QVERIFY(r.first >= before.minValue() && r.second <= before.maxValue());
        QCOMPARE(tp.plot()->axisInterval(QwtAxis::YRight).minValue(), rightBefore.minValue());
        QCOMPARE(tp.plot()->axisInterval(QwtAxis::YRight).maxValue(), rightBefore.maxValue());
        sendWheel(axisWidget, -120); // zoom out
        QCOMPARE(spy.count(), 2);
        const auto r2 = *tp.axisRange(QwtAxis::YLeft);
        QVERIFY(r2.second - r2.first > r.second - r.first);
    }

    void shiftWheelOverCanvasFixesBoth()
    {
        TrendPlot tp;
        tp.resize(800, 600);
        tp.setRecorder(m_rec.get());
        tp.setTags(threeTags());
        tp.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tp));
        QSignalSpy spy(&tp, &TrendPlot::axisRangesChanged);
        sendWheel(tp.plot()->canvas(), 120, Qt::ShiftModifier);
        QCOMPARE(spy.count(), 1);
        QVERIFY(tp.axisRange(QwtAxis::YLeft));
        QVERIFY(tp.axisRange(QwtAxis::YRight));
        // Plain wheel zooms time only.
        TrendPlot tp2;
        tp2.resize(800, 600);
        tp2.setRecorder(m_rec.get());
        tp2.setTags(threeTags());
        tp2.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tp2));
        sendWheel(tp2.plot()->canvas(), 120);
        QVERIFY(!tp2.axisRange(QwtAxis::YLeft));
        QVERIFY(!tp2.axisRange(QwtAxis::YRight));
    }

private:
    std::unique_ptr<Recorder> m_rec;
};

// Regression: opening a session replaces the recorder's contents and then calls resetData();
// the curves must show the loaded rows, not start empty after them.
void TestTrendPlot::resetDataReloadsReplacedRecording()
{
    Recorder rec;
    for (int i = 0; i < 10; ++i)
        addRow(rec, i * 0.1, {good(1, i)});
    TrendPlot plot;
    plot.setRecorder(&rec);
    plot.setTags({makeTag(1, QStringLiteral("A"), QStringLiteral("V"), YAxis::Left)});
    plot.refresh();
    QCOMPARE(curves(plot.plot()).at(0)->dataSize(), size_t(10));

    Recorder loaded;
    for (int i = 0; i < 25; ++i)
        addRow(loaded, 100.0 + i * 0.1, {good(1, -i)});
    rec = std::move(loaded);
    plot.resetData();
    plot.refresh();
    QCOMPARE(curves(plot.plot()).at(0)->dataSize(), size_t(25));
}

QTEST_MAIN(TestTrendPlot)
#include "tst_trendplot.moc"
