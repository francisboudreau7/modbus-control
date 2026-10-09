#include "AnalysisView.h"
#include "PlotCursors.h"
#include "Recorder.h"
#include "SampleBatch.h"
#include "StepAnalysis.h"
#include "Tag.h"
#include "TrendPlot.h"

#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_item.h>
#include <qwt_plot_marker.h>
#include <qwt_text.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QPen>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QtTest>

#include <cmath>
#include <optional>

using namespace mb;

namespace {

constexpr TagId kCo = 1;
constexpr TagId kPv = 2;
constexpr TagId kOther = 3;

TagSample good(TagId id, double value)
{
    TagSample s;
    s.tagId = id;
    s.value = value;
    s.quality = Quality::Good;
    return s;
}

Tag makeTag(TagId id, const QString &name, const QString &unit, const QString &color,
            bool analyzed)
{
    Tag t;
    t.id = id;
    t.name = name;
    t.unit = unit;
    t.color = color;
    t.plotted = true;
    t.analyzed = analyzed;
    return t;
}

QList<Tag> standardTags()
{
    return {
        makeTag(kCo, QStringLiteral("CO"), QStringLiteral("%"), QStringLiteral("#ff0000"), false),
        makeTag(kPv, QStringLiteral("PV"), QStringLiteral("degC"), QStringLiteral("#0000ff"), true),
        makeTag(kOther, QStringLiteral("Other"), QString(), QStringLiteral("#00aa00"), false),
    };
}

/// FOPDT: CO steps 20 -> 30 at t = 10; PV = 50 + 10 * (1 - exp(-(t - 11.5) / 4)) after 11.5.
void appendRows(Recorder &r, int fromIndex, int toIndex)
{
    for (int i = fromIndex; i <= toIndex; ++i) {
        const double t = i * 0.1;
        const double co = t >= 10.0 ? 30.0 : 20.0;
        const double pv = t < 11.5 ? 50.0 : 50.0 + 10.0 * (1.0 - std::exp(-(t - 11.5) / 4.0));
        SampleBatch b;
        b.t = t;
        b.samples = {good(kCo, co), good(kPv, pv), good(kOther, 1.0)};
        r.append(b);
    }
}

QTableWidget *table(AnalysisView &v, const char *name)
{
    return v.findChild<QTableWidget *>(QLatin1String(name));
}

template<class T>
T *child(AnalysisView &v, const char *name)
{
    return v.findChild<T *>(QLatin1String(name));
}

int symbolMarkerCount(AnalysisView &v)
{
    int n = 0;
    const QwtPlotItemList items = v.trendPlot()->plot()->itemList(QwtPlotItem::Rtti_PlotMarker);
    for (QwtPlotItem *item : items) {
        if (static_cast<QwtPlotMarker *>(item)->symbol())
            ++n;
    }
    return n;
}

} // namespace

class TstAnalysisView : public QObject
{
    Q_OBJECT
private slots:
    void fopdtResults();
    void noCursors();
    void addRemovePercent();
    void settingsRoundTrip();
    void modelPercentRevert();
    void deltaCoSelectionKept();
    void onlyAnalyzedTags();
    void refreshRecompute();
    void detailedLabels();
};

/// Labels of the setpoint markers, in plot item order.
static QStringList markerLabels(AnalysisView &v)
{
    QStringList out;
    const QwtPlotItemList items = v.trendPlot()->plot()->itemList(QwtPlotItem::Rtti_PlotMarker);
    for (QwtPlotItem *item : items) {
        auto *m = static_cast<QwtPlotMarker *>(item);
        if (m->symbol())
            out << m->label().text();
    }
    return out;
}

void TstAnalysisView::detailedLabels()
{
    Recorder rec;
    appendRows(rec, 0, 600);
    AnalysisView v;
    v.setRecorder(&rec);
    v.setTags(standardTags());
    v.setCursors(9.0, 40.0);

    QStringList labels = markerLabels(v);
    QCOMPARE(labels.size(), 2);
    QVERIFY(labels.contains(QStringLiteral("63.2 %")));

    // Readable over curves: opaque box, dark text, border in the tag colour.
    const QwtPlotItemList items = v.trendPlot()->plot()->itemList(QwtPlotItem::Rtti_PlotMarker);
    for (QwtPlotItem *item : items) {
        auto *m = static_cast<QwtPlotMarker *>(item);
        if (!m->symbol())
            continue; // the C1/C2 cursor lines
        const QwtText l = m->label();
        QVERIFY(l.backgroundBrush().style() != Qt::NoBrush);
        QVERIFY(l.backgroundBrush().color().alpha() >= 200);
        QCOMPARE(l.borderPen().color(), QColor(QStringLiteral("#0000ff")));
        QVERIFY(l.color().lightness() < 80);
    }

    QSignalSpy changed(&v, &AnalysisView::settingsChanged);
    auto *check = child<QCheckBox>(v, "detailedLabelsCheck");
    QVERIFY(check);
    check->setChecked(true);
    QCOMPARE(changed.count(), 1);
    QVERIFY(v.settings().detailedLabels);

    labels = markerLabels(v);
    QCOMPARE(labels.size(), 2);
    QString detailed;
    for (const QString &l : labels)
        if (l.startsWith(QStringLiteral("63.2 %")))
            detailed = l;
    // 63.2 % of 50 -> ~60 is ~56.31 degC, reached at about 11.5 + 4 = 15.5 s.
    const QStringList lines = detailed.split(u'\n');
    QCOMPARE(lines.size(), 3);
    QVERIFY2(lines.at(1).startsWith(QStringLiteral("t = 15.")), qPrintable(lines.at(1)));
    QVERIFY(lines.at(1).endsWith(QStringLiteral(" s")));
    QVERIFY2(lines.at(2).startsWith(QStringLiteral("y = 56.3")), qPrintable(lines.at(2)));
    QVERIFY(lines.at(2).endsWith(QStringLiteral(" degC")));

    // setSettings updates the check box without emitting settingsChanged.
    AnalysisSettings s = v.settings();
    s.detailedLabels = false;
    v.setSettings(s);
    QVERIFY(!check->isChecked());
    QCOMPARE(changed.count(), 1);
    QVERIFY(markerLabels(v).contains(QStringLiteral("63.2 %")));
}

void TstAnalysisView::fopdtResults()
{
    Recorder rec;
    appendRows(rec, 0, 600);
    AnalysisView v;
    v.setRecorder(&rec);
    v.setTags(standardTags());
    v.resize(1200, 700);
    v.setCursors(9.0, 40.0);

    auto *sp = table(v, "setpointTable");
    auto *mt = table(v, "modelTable");
    QVERIFY(sp && mt);
    QCOMPARE(sp->rowCount(), 2);
    QCOMPARE(sp->item(0, 1)->text(), QStringLiteral("0"));
    QCOMPARE(sp->item(1, 1)->text(), QStringLiteral("63.2"));
    QVERIFY(std::abs(sp->item(0, 4)->text().toDouble() - 11.5) < 0.15);
    QVERIFY(std::abs(sp->item(1, 4)->text().toDouble() - 15.5) < 0.15);

    // Choose the CO tag as dCO.
    auto *combo = child<QComboBox>(v, "deltaCoCombo");
    QVERIFY(combo);
    const int idx = combo->findData(kCo);
    QVERIFY(idx > 0);
    QSignalSpy changed(&v, &AnalysisView::settingsChanged);
    combo->setCurrentIndex(idx);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(v.settings().deltaCoTagId, kCo);

    const QList<StepResult> res = v.results();
    QCOMPARE(res.size(), 1);
    const StepResult expected = analyzeStep(rec, kPv, 9.0, 40.0, v.settings());
    QVERIFY(res[0].valid);
    QCOMPARE(res[0].tagId, kPv);
    QCOMPARE(res[0].setpoints.size(), expected.setpoints.size());
    QCOMPARE(res[0].setpoints[1].time, expected.setpoints[1].time);
    QVERIFY(res[0].gain && res[0].deadTime && res[0].timeConstant);
    QCOMPARE(*res[0].gain, *expected.gain);
    QVERIFY(std::abs(*res[0].timeConstant - 4.0) < 0.3);
    QVERIFY(std::abs(*res[0].gain - 1.0) < 0.05);

    QCOMPARE(mt->rowCount(), 1);
    for (int c = 1; c < 8; ++c) {
        QVERIFY(!mt->item(0, c)->text().isEmpty());
        QVERIFY(mt->item(0, c)->text() != QStringLiteral(u"—"));
    }
    QCOMPARE(symbolMarkerCount(v), 2);
    const QString text = v.readoutText();
    QVERIFY(!text.isEmpty());
    QVERIFY(text.contains(QStringLiteral("63.2 %")));
    QVERIFY(text.contains(QStringLiteral("C1 = 9.000 s, C2 = 40.000 s")));
    QVERIFY(child<QPushButton>(v, "exportResultsButton")->isEnabled());
}

void TstAnalysisView::noCursors()
{
    Recorder rec;
    appendRows(rec, 0, 600);
    AnalysisView v;
    v.setRecorder(&rec);
    v.setTags(standardTags());
    QVERIFY(v.results().isEmpty());
    QCOMPARE(table(v, "setpointTable")->rowCount(), 0);
    QCOMPARE(table(v, "modelTable")->rowCount(), 0);
    QCOMPARE(symbolMarkerCount(v), 0);
    QVERIFY(v.readoutText().isEmpty());
    QVERIFY(!child<QPushButton>(v, "exportResultsButton")->isEnabled());

    v.setCursors(9.0, std::nullopt);
    QVERIFY(v.results().isEmpty());
    QVERIFY(v.readoutText().isEmpty());

    v.setCursors(9.0, 40.0);
    QCOMPARE(v.results().size(), 1);
    v.setCursors(std::nullopt, std::nullopt);
    QVERIFY(v.results().isEmpty());
    QCOMPARE(symbolMarkerCount(v), 0);
}

void TstAnalysisView::addRemovePercent()
{
    Recorder rec;
    appendRows(rec, 0, 600);
    AnalysisView v;
    v.setRecorder(&rec);
    v.setTags(standardTags());
    v.setCursors(9.0, 40.0);

    auto *list = child<QListWidget>(v, "percentList");
    auto *spin = child<QDoubleSpinBox>(v, "percentSpin");
    auto *add = child<QPushButton>(v, "addPercentButton");
    auto *remove = child<QPushButton>(v, "removePercentButton");
    QVERIFY(list && spin && add && remove);
    QCOMPARE(list->count(), 2);

    QSignalSpy changed(&v, &AnalysisView::settingsChanged);
    spin->setValue(28.3);
    add->click();
    QCOMPARE(changed.count(), 1);
    QCOMPARE(list->count(), 3);
    QCOMPARE(list->item(1)->text(), QStringLiteral("28.3 %"));
    QCOMPARE(v.settings().percents, (QList<double>{0.0, 28.3, 63.2}));
    QCOMPARE(table(v, "setpointTable")->rowCount(), 3);

    add->click(); // duplicate
    QCOMPARE(changed.count(), 1);
    QCOMPARE(list->count(), 3);

    list->setCurrentRow(1);
    remove->click();
    QCOMPARE(changed.count(), 2);
    QCOMPARE(v.settings().percents, (QList<double>{0.0, 63.2}));
    QCOMPARE(table(v, "setpointTable")->rowCount(), 2);

    // Fill up to 20, the 21st is rejected.
    for (int i = 1; list->count() < 20; ++i) {
        spin->setValue(i + 0.5);
        add->click();
    }
    QCOMPARE(list->count(), 20);
    const qsizetype before = changed.count();
    spin->setValue(99.0);
    add->click();
    QCOMPARE(list->count(), 20);
    QCOMPARE(changed.count(), before);
    QCOMPARE(v.settings().percents.size(), 20);
}

void TstAnalysisView::settingsRoundTrip()
{
    AnalysisView v;
    v.setTags(standardTags());
    AnalysisSettings s;
    s.percents = {10.0, 50.0, 90.0};
    s.reference = ReferenceMode::WindowMean;
    s.referenceWindowS = 2.5;
    s.smoothingS = 0.6;
    s.modelPercentA = 10.0;
    s.modelPercentB = 70.0;
    s.deltaCoTagId = kCo;
    s.manualDeltaCo = 12.5;
    QSignalSpy changed(&v, &AnalysisView::settingsChanged);
    v.setSettings(s);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(v.settings(), s);
    QCOMPARE(child<QListWidget>(v, "percentList")->count(), 3);
    QCOMPARE(child<QComboBox>(v, "referenceCombo")->currentIndex(), 1);
    QVERIFY(child<QDoubleSpinBox>(v, "windowSpin")->isEnabled());
    QCOMPARE(child<QDoubleSpinBox>(v, "smoothingSpin")->value(), 0.6);
    QCOMPARE(child<QDoubleSpinBox>(v, "modelFromSpin")->value(), 10.0);
    QCOMPARE(child<QComboBox>(v, "deltaCoCombo")->currentData().toUInt(), kCo);

    // A real edit emits exactly once.
    child<QDoubleSpinBox>(v, "smoothingSpin")->setValue(1.0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(v.settings().smoothingS, 1.0);
}

void TstAnalysisView::modelPercentRevert()
{
    AnalysisView v;
    auto *from = child<QDoubleSpinBox>(v, "modelFromSpin");
    auto *to = child<QDoubleSpinBox>(v, "modelToSpin");
    QSignalSpy changed(&v, &AnalysisView::settingsChanged);
    from->setValue(70.0); // would make a >= b
    QCOMPARE(from->value(), 0.0);
    QCOMPARE(to->value(), 63.2);
    QCOMPARE(changed.count(), 0);
    to->setValue(0.0);
    QCOMPARE(to->value(), 63.2);
    QCOMPARE(changed.count(), 0);
    from->setValue(10.0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(v.settings().modelPercentA, 10.0);
}

void TstAnalysisView::deltaCoSelectionKept()
{
    AnalysisView v;
    v.setTags(standardTags());
    auto *combo = child<QComboBox>(v, "deltaCoCombo");
    QCOMPARE(combo->count(), 4);
    combo->setCurrentIndex(combo->findData(kCo));
    QCOMPARE(v.settings().deltaCoTagId, kCo);

    QList<Tag> tags = standardTags();
    tags.removeAt(2); // drop "Other"; CO stays
    v.setTags(tags);
    QCOMPARE(combo->count(), 3);
    QCOMPARE(combo->currentData().toUInt(), kCo);
    QCOMPARE(v.settings().deltaCoTagId, kCo);
    QVERIFY(!child<QDoubleSpinBox>(v, "deltaCoSpin")->isEnabled());

    tags.removeAt(0); // drop CO
    v.setTags(tags);
    QCOMPARE(combo->currentIndex(), 0);
    QCOMPARE(v.settings().deltaCoTagId, TagId(0));
    QVERIFY(child<QDoubleSpinBox>(v, "deltaCoSpin")->isEnabled());
}

void TstAnalysisView::onlyAnalyzedTags()
{
    Recorder rec;
    appendRows(rec, 0, 600);
    AnalysisView v;
    v.setRecorder(&rec);
    v.setTags(standardTags());
    v.setCursors(9.0, 40.0);
    v.refresh();

    int curves = 0;
    const QwtPlotItemList items = v.trendPlot()->plot()->itemList(QwtPlotItem::Rtti_PlotCurve);
    for (QwtPlotItem *item : items) {
        Q_UNUSED(item);
        ++curves;
    }
    QCOMPARE(curves, 1);
    QCOMPARE(v.results().size(), 1);
    QCOMPARE(v.results()[0].tagId, kPv);
    QCOMPARE(table(v, "modelTable")->rowCount(), 1);
}

void TstAnalysisView::refreshRecompute()
{
    // Data ends inside the cursor range: new rows change the result.
    {
        Recorder rec;
        appendRows(rec, 0, 300); // to t = 30
        AnalysisView v;
        v.setRecorder(&rec);
        v.setTags(standardTags());
        v.setCursors(9.0, 40.0);
        v.refresh();
        QSignalSpy results(&v, &AnalysisView::resultsChanged);
        appendRows(rec, 301, 320);
        v.refresh();
        QCOMPARE(results.count(), 1);
        v.refresh(); // nothing new
        QCOMPARE(results.count(), 1);
    }
    // Data already beyond max(C1, C2) + S/2: appended rows cannot change anything.
    {
        Recorder rec;
        appendRows(rec, 0, 600); // to t = 60
        AnalysisView v;
        v.setRecorder(&rec);
        v.setTags(standardTags());
        v.setCursors(9.0, 40.0);
        v.refresh();
        QSignalSpy results(&v, &AnalysisView::resultsChanged);
        appendRows(rec, 601, 650);
        v.refresh();
        QCOMPARE(results.count(), 0);

        // Cleared recorder: recompute.
        rec.clear();
        v.resetData();
        QVERIFY(results.count() >= 1);
        QVERIFY(!v.results().isEmpty() ? !v.results()[0].valid : true);
    }
}

QTEST_MAIN(TstAnalysisView)
#include "tst_analysisview.moc"
