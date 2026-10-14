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
#include <qwt_symbol.h>
#include <qwt_text.h>

#include <QCheckBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QShortcut>
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
    void staleDeltaCoTagMeansTypedValue();
    void loadedPercentsNormalised();
    void editingOneControlKeepsOthers();
    void evictionRecomputes();
    void modelCurve();
    void fitRmsCell();
    void guideLines();
    void rightAxis();
    void copyResults();
    void settingsNewBooleans();
    void manyRecomputes();
};

void TstAnalysisView::staleDeltaCoTagMeansTypedValue()
{
    Recorder rec;
    appendRows(rec, 0, 600);
    AnalysisView v;
    v.setRecorder(&rec);
    v.setTags(standardTags());
    AnalysisSettings s;
    s.deltaCoTagId = 99; // not in the project
    s.manualDeltaCo = 10.0;
    v.setSettings(s);
    QCOMPARE(v.settings().deltaCoTagId, TagId(0));
    v.setCursors(9.0, 40.0);
    QCOMPARE(v.results().size(), 1);
    QVERIFY(v.results().at(0).gain.has_value()); // the typed ΔCO is used
}

void TstAnalysisView::loadedPercentsNormalised()
{
    AnalysisView v;
    v.setTags(standardTags());
    AnalysisSettings s;
    s.percents = {63.2, 0.0, 63.2, 28.3, 150.0};
    v.setSettings(s);
    QCOMPARE(v.settings().percents, (QList<double>{0.0, 28.3, 63.2}));
    QCOMPARE(child<QListWidget>(v, "percentList")->count(), 3);
}

void TstAnalysisView::editingOneControlKeepsOthers()
{
    AnalysisView v;
    v.setTags(standardTags());
    AnalysisSettings s;
    s.referenceWindowS = 0.25; // the spin box shows it rounded
    s.smoothingS = 0.125;
    v.setSettings(s);
    child<QComboBox>(v, "referenceCombo")->setCurrentIndex(1);
    QCOMPARE(v.settings().reference, ReferenceMode::WindowMean);
    QCOMPARE(v.settings().referenceWindowS, 0.25);
    QCOMPARE(v.settings().smoothingS, 0.125);
}

void TstAnalysisView::evictionRecomputes()
{
    Recorder rec(400); // 40 s at 0.1 s
    appendRows(rec, 0, 399);
    AnalysisView v;
    v.setRecorder(&rec);
    v.setTags(standardTags());
    v.setCursors(9.0, 30.0);
    QCOMPARE(v.results().size(), 1);
    QVERIFY(v.results().at(0).valid);

    // Full ring: new rows evict the data around C1, so the result must change.
    appendRows(rec, 400, 499);
    QSignalSpy changed(&v, &AnalysisView::resultsChanged);
    v.refresh();
    QCOMPARE(changed.count(), 1);
    QVERIFY(!v.results().at(0).valid);
}

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
        // The point itself is not in the curve's colour: white disc, dark ring, above the curves.
        QCOMPARE(m->symbol()->brush().color(), QColor(Qt::white));
        QVERIFY(m->symbol()->pen().color().lightness() < 80);
        QVERIFY(m->symbol()->pen().color() != QColor(QStringLiteral("#0000ff")));
        QVERIFY(m->z() > 20.0);
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
    AnalysisSettings noModel; // showModel is on by default since v1.2.6
    noModel.showModel = false;
    v.setSettings(noModel);
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

static QwtPlotCurve *findCurve(AnalysisView &v, const QString &title)
{
    const QwtPlotItemList items = v.trendPlot()->plot()->itemList(QwtPlotItem::Rtti_PlotCurve);
    for (QwtPlotItem *item : items) {
        if (item->title().text() == title)
            return static_cast<QwtPlotCurve *>(item);
    }
    return nullptr;
}

static QwtPlotItemList guideItems(AnalysisView &v)
{
    return v.trendPlot()->plot()->itemList(QwtPlotItem::Rtti_PlotUserItem + 1);
}

static void setupFopdt(Recorder &rec, AnalysisView &v, const QList<Tag> &tags)
{
    appendRows(rec, 0, 600);
    v.setRecorder(&rec);
    v.setTags(tags);
    AnalysisSettings s;
    s.deltaCoTagId = kCo;
    s.showModel = false; // on by default since v1.2.6
    v.setSettings(s);
    v.setCursors(9.0, 40.0);
}

void TstAnalysisView::modelCurve()
{
    Recorder rec;
    AnalysisView v;
    setupFopdt(rec, v, standardTags());
    QVERIFY(!findCurve(v, QStringLiteral("PV model")));

    QSignalSpy changed(&v, &AnalysisView::settingsChanged);
    child<QCheckBox>(v, "showModelCheck")->setChecked(true);
    QCOMPARE(changed.count(), 1);
    QVERIFY(v.settings().showModel);
    QwtPlotCurve *c = findCurve(v, QStringLiteral("PV model"));
    QVERIFY(c);
    QVERIFY(!c->testItemAttribute(QwtPlotItem::Legend));
    QVERIFY(c->dataSize() >= 400);
    QCOMPARE(c->pen().style(), Qt::DashLine);
    const StepResult r = v.results().at(0);
    QVERIFY(r.deadTime && r.timeConstant);
    const double tq = r.stepTime + *r.deadTime + *r.timeConstant;
    double best = 1e9;
    double yAt = 0.0;
    for (size_t i = 0; i < c->dataSize(); ++i) {
        const QPointF p = c->sample(int(i));
        if (std::abs(p.x() - tq) < best) {
            best = std::abs(p.x() - tq);
            yAt = p.y();
        }
    }
    QVERIFY(best < 0.1);
    QVERIFY2(std::abs(yAt - (r.y0 + 0.632 * r.deltaPv)) < 0.2, qPrintable(QString::number(yAt)));

    child<QCheckBox>(v, "showModelCheck")->setChecked(false);
    QVERIFY(!findCurve(v, QStringLiteral("PV model")));

    // Absent when the model is missing.
    AnalysisSettings s = v.settings();
    s.showModel = true;
    v.setSettings(s);
    QVERIFY(findCurve(v, QStringLiteral("PV model")));
    v.setCursors(9.0, 11.0);
    QVERIFY(!v.results().isEmpty());
    if (!v.results().at(0).deadTime)
        QVERIFY(!findCurve(v, QStringLiteral("PV model")));
    v.setCursors(std::nullopt, std::nullopt);
    QVERIFY(!findCurve(v, QStringLiteral("PV model")));
}

void TstAnalysisView::fitRmsCell()
{
    Recorder rec;
    AnalysisView v;
    setupFopdt(rec, v, standardTags());
    QTableWidget *t = table(v, "modelTable");
    QCOMPARE(t->columnCount(), 9);
    QCOMPARE(t->horizontalHeaderItem(8)->text(), QStringLiteral("Fit RMS"));
    const QString text = t->item(0, 8)->text();
    QVERIFY2(QRegularExpression(QStringLiteral(R"(^[0-9.e+-]+ \([0-9.e+-]+ %\)$)"))
                 .match(text).hasMatch(),
             qPrintable(text));

    // Invalid result: error text spans the value cells.
    v.setCursors(9.0, 9.0);
    QVERIFY(!v.results().at(0).valid);
    QCOMPARE(t->columnSpan(0, 1), 8);
}

void TstAnalysisView::guideLines()
{
    Recorder rec;
    AnalysisView v;
    setupFopdt(rec, v, standardTags());
    QCOMPARE(guideItems(v).size(), 1);
    QVERIFY(guideItems(v).at(0)->title().text().contains(QStringLiteral("(2 points)")));
    QVERIFY(!guideItems(v).at(0)->testItemAttribute(QwtPlotItem::Legend));
    v.resize(900, 500);
    v.show();
    QVERIFY(!v.trendPlot()->grab().isNull());

    QSignalSpy changed(&v, &AnalysisView::settingsChanged);
    child<QCheckBox>(v, "guideLinesCheck")->setChecked(false);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(guideItems(v).size(), 0);
    QVERIFY(!v.settings().guideLines);
    QVERIFY(!v.trendPlot()->grab().isNull());
    child<QCheckBox>(v, "guideLinesCheck")->setChecked(true);
    QCOMPARE(guideItems(v).size(), 1);
}

void TstAnalysisView::rightAxis()
{
    Recorder rec;
    AnalysisView v;
    QList<Tag> tags = standardTags();
    tags[1].axis = YAxis::Right;
    setupFopdt(rec, v, tags);
    AnalysisSettings s = v.settings();
    s.showModel = true;
    v.setSettings(s);
    int markers = 0;
    const QwtPlotItemList items = v.trendPlot()->plot()->itemList(QwtPlotItem::Rtti_PlotMarker);
    for (QwtPlotItem *item : items) {
        if (static_cast<QwtPlotMarker *>(item)->symbol()) {
            ++markers;
            QVERIFY(item->yAxis() == QwtPlot::yRight);
        }
    }
    QVERIFY(markers >= 2);
    QVERIFY(findCurve(v, QStringLiteral("PV model"))->yAxis() == QwtPlot::yRight);
    QCOMPARE(guideItems(v).size(), 1);
    QVERIFY(guideItems(v).at(0)->yAxis() == QwtPlot::yRight);
    v.resize(900, 500);
    v.show();
    QVERIFY(!v.trendPlot()->grab().isNull());
}

void TstAnalysisView::copyResults()
{
    Recorder rec;
    AnalysisView v;
    setupFopdt(rec, v, standardTags());
    auto *button = child<QPushButton>(v, "copyResultsButton");
    QVERIFY(button->isEnabled());
    QGuiApplication::clipboard()->setText(QString());
    button->click();
    const QString text = QGuiApplication::clipboard()->text();
    QVERIFY(text.contains(QStringLiteral("Tag\t%")));
    QVERIFY(text.contains(QStringLiteral("Tag\ty0")));
    QVERIFY(text.contains(QStringLiteral("63.2")));
    QVERIFY(text.contains(QStringLiteral("\r\n\r\n")));

    for (const char *name : {"setpointTable", "modelTable"}) {
        QTableWidget *t = table(v, name);
        QCOMPARE(t->selectionMode(), QAbstractItemView::ExtendedSelection);
        bool found = false;
        const auto shortcuts = t->findChildren<QShortcut *>();
        for (QShortcut *sc : shortcuts)
            found = found || sc->key() == QKeySequence(QKeySequence::Copy);
        QVERIFY(found);
    }

    v.setCursors(std::nullopt, std::nullopt);
    QVERIFY(!button->isEnabled());
}

void TstAnalysisView::settingsNewBooleans()
{
    AnalysisView v;
    v.setTags(standardTags());
    QSignalSpy changed(&v, &AnalysisView::settingsChanged);
    AnalysisSettings s;
    s.showModel = true;
    s.guideLines = false;
    v.setSettings(s);
    QCOMPARE(changed.count(), 0);
    QVERIFY(child<QCheckBox>(v, "showModelCheck")->isChecked());
    QVERIFY(!child<QCheckBox>(v, "guideLinesCheck")->isChecked());
    QCOMPARE(v.settings(), s);
}

void TstAnalysisView::manyRecomputes()
{
    Recorder rec;
    auto *v = new AnalysisView;
    setupFopdt(rec, *v, standardTags());
    AnalysisSettings s = v->settings();
    s.showModel = true;
    v->setSettings(s);
    for (int i = 0; i < 50; ++i)
        v->setCursors(9.0 + 0.01 * i, 40.0);
    QCOMPARE(guideItems(*v).size(), 1);
    QCOMPARE(v->trendPlot()->plot()->itemList(QwtPlotItem::Rtti_PlotCurve).size(), 2); // PV + model
    delete v; // no crash / double delete
}

QTEST_MAIN(TstAnalysisView)
#include "tst_analysisview.moc"
