#include "Recorder.h"
#include "SampleBatch.h"
#include "Tag.h"
#include "TrendPlot.h"

#include <QPushButton>
#include <QtTest>

#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_scale_div.h>
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

} // namespace

class TestTrendPlot : public QObject
{
    Q_OBJECT
private slots:
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

private:
    std::unique_ptr<Recorder> m_rec;
};

QTEST_MAIN(TestTrendPlot)
#include "tst_trendplot.moc"
