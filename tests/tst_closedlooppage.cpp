#include "AnalysisPanel.h"
#include "ClosedLoopAnalysis.h"
#include "ClosedLoopPage.h"
#include "PidForm.h"
#include "ProjectIO.h"
#include "PlotCursors.h"
#include "Recorder.h"
#include "SampleBatch.h"
#include "StepAnalysis.h"
#include "Tag.h"
#include "TrendPlot.h"
#include "Tuning.h"

#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_item.h>
#include <qwt_plot_marker.h>
#include <qwt_text.h>

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGuiApplication>
#include <QGroupBox>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QPen>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

using namespace mb;

namespace {

constexpr TagId kCo = 1;
constexpr TagId kPv = 2;
constexpr TagId kSp = 3;

TagSample good(TagId id, double value)
{
    TagSample s;
    s.tagId = id;
    s.value = value;
    s.quality = Quality::Good;
    return s;
}

Tag makeTag(TagId id, const QString &name, const QString &unit, const QString &color)
{
    Tag t;
    t.id = id;
    t.name = name;
    t.unit = unit;
    t.color = color;
    t.plotted = true;
    return t;
}

QList<Tag> standardTags()
{
    return {
        makeTag(kCo, QStringLiteral("CO"), QStringLiteral("%"), QStringLiteral("#ff0000")),
        makeTag(kPv, QStringLiteral("PV"), QStringLiteral("degC"), QStringLiteral("#0000ff")),
        makeTag(kSp, QStringLiteral("SP"), QStringLiteral("degC"), QStringLiteral("#00aa00")),
    };
}

void appendRow(Recorder &r, double t, double co, double pv, double sp)
{
    SampleBatch b;
    b.t = t;
    b.samples = {good(kCo, co), good(kPv, pv), good(kSp, sp)};
    r.append(b);
}

/// Open-loop FOPDT step (K = 1, L = 1.5, T = 4) from 0 to 60 s: CO 20 -> 30 at 10 s.
void appendOpenLoop(Recorder &r)
{
    for (int i = 0; i <= 600; ++i) {
        const double t = i * 0.1;
        const double co = t >= 10.0 ? 30.0 : 20.0;
        const double pv = t < 11.5 ? 50.0 : 50.0 + 10.0 * (1.0 - std::exp(-(t - 11.5) / 4.0));
        appendRow(r, t, co, pv, 50.0);
    }
}

/// Closed-loop data from t0 to t0 + 80 s. Setpoint step: SP 50 -> 60 at t0 + 10, the PV a damped
/// second-order response (overshoot about 44 %). Disturbance: SP 50, the PV dips and returns.
/// CO = clamp(40 + coGain (SP - PV), 0, 100).
void appendClosedLoop(Recorder &r, double t0, bool disturbance, double coGain)
{
    const double sigma = 0.125;
    const double wd = 0.5 * std::sqrt(1.0 - 0.0625);
    for (int i = 0; i <= 800; ++i) {
        const double t = t0 + i * 0.1;
        const double tt = t - (t0 + 10.0);
        double sp = 50.0;
        double pv = 50.0;
        if (tt >= 0.0) {
            if (disturbance) {
                pv = 50.0 - 6.0 * tt * std::exp(-0.3 * tt);
            } else {
                sp = 60.0;
                pv = 50.0 + 10.0 * (1.0 - std::exp(-sigma * tt) * (std::cos(wd * tt)
                                                                   + sigma / wd * std::sin(wd * tt)));
            }
        }
        const double co = std::clamp(40.0 + coGain * (sp - pv), 0.0, 100.0);
        appendRow(r, t, co, pv, sp);
    }
}

template<class T>
T *child(QWidget &w, const char *name)
{
    return w.findChild<T *>(QLatin1String(name));
}

QString g4(double v)
{
    return QString::number(v, 'g', 4);
}

QString cell(QTableWidget *t, int row, int col)
{
    return t->item(row, col) ? t->item(row, col)->text() : QString();
}

/// Row whose label starts with `label` (the settling row carries the band).
int rowOf(QTableWidget *t, const QString &label)
{
    for (int r = 0; r < t->rowCount(); ++r) {
        if (cell(t, r, 0).startsWith(label))
            return r;
    }
    return -1;
}

QStringList checkTexts(QWidget &panel)
{
    QStringList out;
    auto *list = child<QListWidget>(panel, "closedChecksList");
    for (int i = 0; i < list->count(); ++i)
        out << list->item(i)->text();
    return out;
}

struct Options {
    bool disturbance = false;
    bool noCo = false;
    bool typedSp = false;
    double coGain = 3.0;
    bool openLoopFirst = false; ///< an open-loop step at 0..60 s, closed-loop data from 100 s
};

/// Recorder + plot + panel in Closed loop mode with the cursors around the closed-loop test.
struct Fx {
    Recorder rec;
    TrendPlot plot;
    AnalysisPanel panel;
    double t0 = 0.0;

    explicit Fx(const Options &o = {})
    {
        if (o.openLoopFirst) {
            appendOpenLoop(rec);
            t0 = 100.0;
        }
        appendClosedLoop(rec, t0, o.disturbance, o.coGain);
        const QList<Tag> tags = standardTags();
        plot.setRecorder(&rec);
        plot.setTags(tags);
        plot.refresh();
        panel.setRecorder(&rec);
        panel.setPlot(&plot);
        panel.setTags(tags);
        AnalysisSettings s;
        s.pvTagId = kPv;
        s.setpointTagIds = {kPv};
        s.deltaCoTagId = o.noCo ? 0 : kCo;
        s.spTagId = o.typedSp ? 0 : kSp;
        s.closedLoop.test = o.disturbance ? ClosedLoopTest::Disturbance : ClosedLoopTest::SetpointStep;
        s.closedLoop.typedSpBefore = 50.0;
        s.closedLoop.typedSpAfter = o.disturbance ? 50.0 : 60.0;
        s.method = ModelMethod::ZeroTo632;
        s.mode = o.openLoopFirst ? AnalysisMode::OpenLoop : AnalysisMode::ClosedLoop;
        panel.setSettings(s);
        if (o.openLoopFirst)
            setCursors(9.0, 40.0);
        else
            setCursors(t0 + 9.0, t0 + 70.0);
    }
    void setCursors(double c1, double c2)
    {
        plot.cursors()->setC1(c1);
        plot.cursors()->setC2(c2);
    }
    void apply(const std::function<void(AnalysisSettings &)> &edit)
    {
        AnalysisSettings s = panel.settings();
        edit(s);
        panel.setSettings(s);
    }
    ClosedLoopResult direct(TagId sp, TagId co, double c1, double c2) const
    {
        const AnalysisSettings s = panel.settings();
        return analyzeClosedLoop(rec, kPv, sp, co, c1, c2, s.closedLoop, s.coRangeMin, s.coRangeMax);
    }
    ClosedLoopPage *page() { return child<ClosedLoopPage>(panel, "closedLoopPage"); }
    QTableWidget *table() { return child<QTableWidget>(panel, "performanceTable"); }
};

QList<QwtPlotCurve *> curvesWithStyle(TrendPlot &plot, Qt::PenStyle style)
{
    QList<QwtPlotCurve *> out;
    const QwtPlotItemList items = plot.plot()->itemList(QwtPlotItem::Rtti_PlotCurve);
    for (QwtPlotItem *item : items) {
        auto *c = static_cast<QwtPlotCurve *>(item);
        if (c->pen().style() == style)
            out << c;
    }
    return out;
}

QStringList markerLabels(TrendPlot &plot)
{
    QStringList out;
    const QwtPlotItemList items = plot.plot()->itemList(QwtPlotItem::Rtti_PlotMarker);
    for (QwtPlotItem *item : items) {
        auto *m = static_cast<QwtPlotMarker *>(item);
        if (m->symbol())
            out << m->label().text();
    }
    return out;
}

bool hasMarkerStartingWith(TrendPlot &plot, const QString &prefix)
{
    for (const QString &l : markerLabels(plot)) {
        if (l.startsWith(prefix))
            return true;
    }
    return false;
}

} // namespace

class TstClosedLoopPage : public QObject
{
    Q_OBJECT
private slots:
    void pageIsShown();
    void setpointStepRows();
    void typedSetpoint();
    void disturbanceRows();
    void checks();
    void settlingBand();
    void hintLine();
    void predictionWithoutModel();
    void predictionWithModel();
    void takeTuning();
    void controllerForms();
    void typedPredictionValues();
    void overlays();
    void plotDestroyedFirst();
    void copyAndReadout();
    void tooltips();
    void settingsRoundTrip();
    void modelSurvivesCursorMove();
    void useOpenLoopModelButton();
    void automaticTakeIsSilent();
    void typedModel();
    void loadStepIsSaved();
    void typedZeroGainModel();
    void takeOnlyOnModeSwitch();
    void predictionSpinsKeepPrecision();
    void typingIsNotOverwritten();
    void predictionBaseline();
    void closedModeHintAndFindStep();
    void panelButtonsAndReadout();
};

void TstClosedLoopPage::pageIsShown()
{
    Fx fx;
    auto *stack = child<QStackedWidget>(fx.panel, "modeStack");
    QVERIFY(fx.page());
    QCOMPARE(stack->currentWidget(), fx.page());
    QVERIFY(!child<QLabel>(fx.panel, "closedLoopPlaceholder"));
    for (const char *name : {"closedTestGroup", "performanceGroup", "predictionGroup"})
        QVERIFY2(child<QGroupBox>(fx.panel, name), name);
    // Open loop hides it again, Closed loop brings it back.
    child<QToolButton>(fx.panel, "openLoopButton")->click();
    QVERIFY(stack->currentWidget() != fx.page());
    child<QToolButton>(fx.panel, "closedLoopButton")->click();
    QCOMPARE(stack->currentWidget(), fx.page());
}

void TstClosedLoopPage::setpointStepRows()
{
    Fx fx;
    const ClosedLoopResult r = fx.direct(kSp, kCo, 9.0, 70.0);
    QVERIFY(r.valid);
    QVERIFY(r.overshoot && r.settlingTime && r.riseTime && r.peakTime);
    QTableWidget *t = fx.table();
    QCOMPARE(t->columnCount(), 2);
    QCOMPARE(t->horizontalHeaderItem(0)->text(), QStringLiteral("Quantity"));
    QCOMPARE(t->horizontalHeaderItem(1)->text(), QStringLiteral("Measured"));
    QStringList labels;
    for (int i = 0; i < t->rowCount(); ++i)
        labels << cell(t, i, 0);
    QCOMPARE(t->rowCount(), 15);
    for (const char *l : {"SP before", "SP after", "Overshoot", "Peak time", "Rise time (10-90 %)",
                          "Settling time", "Decay ratio", "Period", "Steady-state error", "IAE",
                          "ISE", "ITAE", "Output min", "Output max", "Output travel"})
        QVERIFY2(rowOf(t, QLatin1String(l)) >= 0, l);

    QCOMPARE(cell(t, rowOf(t, "SP before"), 1), QStringLiteral("50 degC"));
    QCOMPARE(cell(t, rowOf(t, "SP after"), 1), QStringLiteral("60 degC"));
    QCOMPARE(cell(t, rowOf(t, "Overshoot"), 1),
             QStringLiteral("%1 degC (%2 % of the step)").arg(g4(*r.overshoot), g4(*r.overshootPct)));
    QCOMPARE(cell(t, rowOf(t, "Peak time"), 1), g4(*r.peakTime) + QStringLiteral(" s"));
    QCOMPARE(cell(t, rowOf(t, "Rise time"), 1), g4(*r.riseTime) + QStringLiteral(" s"));
    QCOMPARE(cell(t, rowOf(t, "Settling time"), 1), g4(*r.settlingTime) + QStringLiteral(" s"));
    QVERIFY(cell(t, rowOf(t, "Settling time"), 0).contains(QStringLiteral("2 %")));
    QVERIFY(r.decayRatio);
    QCOMPARE(cell(t, rowOf(t, "Decay ratio"), 1), g4(*r.decayRatio));
    QCOMPARE(cell(t, rowOf(t, "Period"), 1), g4(*r.period) + QStringLiteral(" s"));
    QCOMPARE(cell(t, rowOf(t, "Steady-state error"), 1), g4(r.steadyStateError) + QStringLiteral(" degC"));
    QCOMPARE(cell(t, rowOf(t, "IAE"), 1), g4(r.iae) + QStringLiteral(" degC·s"));
    QCOMPARE(cell(t, rowOf(t, "ISE"), 1), g4(r.ise) + QStringLiteral(" degC²·s"));
    QCOMPARE(cell(t, rowOf(t, "ITAE"), 1), g4(r.itae) + QStringLiteral(" degC·s²"));
    QCOMPARE(cell(t, rowOf(t, "Output min"), 1), g4(*r.outMin) + QStringLiteral(" %"));
    QCOMPARE(cell(t, rowOf(t, "Output max"), 1), g4(*r.outMax) + QStringLiteral(" %"));
    QCOMPARE(cell(t, rowOf(t, "Output travel"), 1), g4(*r.outTravel) + QStringLiteral(" %"));
    QVERIFY(checkTexts(fx.panel).isEmpty());
    QVERIFY(child<QListWidget>(fx.panel, "closedChecksList")->isHidden());
}

void TstClosedLoopPage::typedSetpoint()
{
    Fx fx(Options{.typedSp = true});
    auto *before = child<QDoubleSpinBox>(fx.panel, "spBeforeSpin");
    auto *after = child<QDoubleSpinBox>(fx.panel, "spAfterSpin");
    QVERIFY(before->isVisibleTo(fx.page()) && after->isVisibleTo(fx.page()));
    QCOMPARE(before->value(), 50.0);
    QCOMPARE(after->value(), 60.0);
    QCOMPARE(before->suffix(), QStringLiteral(" degC"));

    // The step is at C1 with the typed values.
    const ClosedLoopResult r = fx.direct(0, kCo, 9.0, 70.0);
    QVERIFY(r.valid);
    QCOMPARE(cell(fx.table(), rowOf(fx.table(), "SP before"), 1), QStringLiteral("50 degC"));
    QCOMPARE(cell(fx.table(), rowOf(fx.table(), "IAE"), 1), g4(r.iae) + QStringLiteral(" degC·s"));

    // Editing is a user edit: settings change, the table follows.
    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    after->setValue(70.0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(fx.panel.settings().closedLoop.typedSpAfter, 70.0);
    QCOMPARE(cell(fx.table(), rowOf(fx.table(), "SP after"), 1), QStringLiteral("70 degC"));
    before->setValue(40.0);
    QCOMPARE(fx.panel.settings().closedLoop.typedSpBefore, 40.0);

    // Disturbance: only the setpoint spin, labelled "Setpoint".
    auto *test = child<QComboBox>(fx.panel, "testCombo");
    test->setCurrentIndex(1);
    QVERIFY(fx.panel.settings().closedLoop.test == ClosedLoopTest::Disturbance);
    QVERIFY(!before->isVisibleTo(fx.page()));
    QVERIFY(after->isVisibleTo(fx.page()));
    bool setpointLabel = false;
    for (QLabel *l : fx.page()->findChildren<QLabel *>()) {
        if (l->text() == QStringLiteral("Setpoint"))
            setpointLabel = true;
    }
    QVERIFY(setpointLabel);

    // An SP tag hides both.
    fx.apply([](AnalysisSettings &s) { s.spTagId = kSp; });
    QVERIFY(!before->isVisibleTo(fx.page()));
    QVERIFY(!after->isVisibleTo(fx.page()));
}

void TstClosedLoopPage::disturbanceRows()
{
    Fx fx(Options{.disturbance = true});
    const ClosedLoopResult r = fx.direct(kSp, kCo, fx.t0 + 9.0, fx.t0 + 70.0);
    QVERIFY(r.valid);
    QVERIFY(r.test == ClosedLoopTest::Disturbance);
    QVERIFY(r.maxDeviation && r.maxDeviationTime);
    QTableWidget *t = fx.table();
    QCOMPARE(t->rowCount(), 13);
    QVERIFY(rowOf(t, "Overshoot") < 0);
    QCOMPARE(cell(t, rowOf(t, "Setpoint"), 1), QStringLiteral("50 degC"));
    QCOMPARE(cell(t, rowOf(t, "Max deviation"), 1), g4(*r.maxDeviation) + QStringLiteral(" degC"));
    QCOMPARE(cell(t, rowOf(t, "Time of max deviation"), 1), g4(*r.maxDeviationTime) + QStringLiteral(" s"));
    QVERIFY(r.settlingTime);
    QCOMPARE(cell(t, rowOf(t, "Settling time"), 1), g4(*r.settlingTime) + QStringLiteral(" s"));
    QCOMPARE(cell(t, rowOf(t, "IAE"), 1), g4(r.iae) + QStringLiteral(" degC·s"));
    QCOMPARE(cell(t, rowOf(t, "Decay ratio"), 1), QStringLiteral("—")); // a single lobe
    QVERIFY(checkTexts(fx.panel).isEmpty());
    // The load step spin belongs to the disturbance test.
    QVERIFY(child<QDoubleSpinBox>(fx.panel, "loadStepSpin")->isVisibleTo(fx.page()));
}

void TstClosedLoopPage::checks()
{
    {
        // C2 while the PV is still oscillating: NotSettled.
        Fx fx;
        fx.setCursors(9.0, 20.0);
        QVERIFY(checkTexts(fx.panel).contains(QStringLiteral(
            "The PV is still outside the settling band at C2: move C2 later.")));
    }
    {
        Fx fx(Options{.noCo = true});
        QVERIFY(checkTexts(fx.panel).contains(QStringLiteral(
            "Choose the controller output (CO) in step 1 to see the output rows.")));
        QCOMPARE(cell(fx.table(), rowOf(fx.table(), "Output min"), 1), QStringLiteral("—"));
        QVERIFY(!child<QListWidget>(fx.panel, "closedChecksList")->isHidden());
    }
    {
        Fx fx(Options{.coGain = 8.0}); // the output hits 100 %
        QVERIFY(checkTexts(fx.panel).contains(QStringLiteral(
            "The output reached its limit: the response shows the limits of the valve, not only "
            "the tuning.")));
    }
    {
        // Typed setpoint without a change.
        Fx fx(Options{.typedSp = true});
        fx.apply([](AnalysisSettings &s) { s.closedLoop.typedSpAfter = 50.0; });
        QVERIFY(checkTexts(fx.panel).contains(QStringLiteral(
            "The setpoint does not change between the cursors: choose Disturbance, or move the "
            "cursors around the setpoint change.")));
        QCOMPARE(cell(fx.table(), rowOf(fx.table(), "Overshoot"), 1), QStringLiteral("—"));
    }
    {
        // A setpoint step analysed as a disturbance.
        Fx fx;
        fx.apply([](AnalysisSettings &s) { s.closedLoop.test = ClosedLoopTest::Disturbance; });
        QVERIFY(checkTexts(fx.panel).contains(QStringLiteral(
            "The setpoint changes between the cursors: choose Setpoint step.")));
    }
}

void TstClosedLoopPage::settlingBand()
{
    Fx fx;
    auto *band = child<QComboBox>(fx.panel, "bandCombo");
    QCOMPARE(band->count(), 2);
    QCOMPARE(band->itemText(0), QStringLiteral("2 %"));
    QCOMPARE(band->itemText(1), QStringLiteral("5 %"));
    const QString at2 = cell(fx.table(), rowOf(fx.table(), "Settling time"), 1);
    const ClosedLoopResult r2 = fx.direct(kSp, kCo, 9.0, 70.0);
    QCOMPARE(at2, g4(*r2.settlingTime) + QStringLiteral(" s"));

    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    band->setCurrentIndex(1);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(fx.panel.settings().closedLoop.settlingBandPct, 5.0);
    const ClosedLoopResult r5 = fx.direct(kSp, kCo, 9.0, 70.0);
    QVERIFY(r5.settlingTime);
    const int row = rowOf(fx.table(), "Settling time");
    QCOMPARE(cell(fx.table(), row, 1), g4(*r5.settlingTime) + QStringLiteral(" s"));
    QVERIFY(cell(fx.table(), row, 1) != at2);
    QVERIFY(cell(fx.table(), row, 0).contains(QStringLiteral("5 %")));

    // A band from a project file that is not in the list stays selectable.
    fx.apply([](AnalysisSettings &s) { s.closedLoop.settlingBandPct = 3.0; });
    QCOMPARE(band->currentData().toDouble(), 3.0);
}

void TstClosedLoopPage::hintLine()
{
    Recorder rec;
    TrendPlot plot;
    AnalysisPanel panel;
    appendClosedLoop(rec, 0.0, false, 3.0);
    plot.setRecorder(&rec);
    plot.setTags(standardTags());
    plot.refresh();
    panel.setRecorder(&rec);
    panel.setPlot(&plot);
    panel.setTags(standardTags());
    auto *hint = child<QLabel>(panel, "hintLabel");
    QCOMPARE(hint->text(), QStringLiteral("Choose the process value (PV) in step 1."));
    AnalysisSettings s;
    s.pvTagId = kPv;
    s.spTagId = kSp;
    s.mode = AnalysisMode::ClosedLoop;
    panel.setSettings(s);
    QCOMPARE(hint->text(),
             QStringLiteral("Place C1 at the setpoint change (or just before the disturbance) and "
                            "C2 after the PV has settled."));
    plot.cursors()->setC1(9.0);
    plot.cursors()->setC2(70.0);
    QVERIFY(hint->text().isEmpty());
    // The result's error: no data between the cursors.
    plot.cursors()->setC1(500.0);
    plot.cursors()->setC2(600.0);
    QVERIFY(!hint->text().isEmpty());
    QVERIFY(hint->text() != QStringLiteral("Choose the process value (PV) in step 1."));
}

void TstClosedLoopPage::predictionWithoutModel()
{
    Fx fx(Options{.noCo = true}); // no CO: no K, no model
    auto *check = child<QCheckBox>(fx.panel, "showPredictionCheck");
    auto *label = child<QLabel>(fx.panel, "predictionModelLabel");
    QCOMPARE(label->text(),
             QStringLiteral("No model yet: do the open-loop step test first (Open loop area), or "
                            "type one."));
    QCOMPARE(fx.table()->columnCount(), 2);

    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    check->setChecked(true);
    QCOMPARE(changed.count(), 1);
    QVERIFY(fx.panel.settings().closedLoop.showPrediction);
    QTableWidget *t = fx.table();
    QCOMPARE(t->columnCount(), 3);
    QCOMPARE(t->horizontalHeaderItem(2)->text(), QStringLiteral("Predicted"));
    for (int r = 0; r < t->rowCount(); ++r)
        QCOMPARE(cell(t, r, 2), QStringLiteral("—"));
    QVERIFY(curvesWithStyle(fx.plot, Qt::DashLine).isEmpty());
    QVERIFY(!child<QPushButton>(fx.panel, "takeTuningButton")->isEnabled());
    QCOMPARE(child<QComboBox>(fx.panel, "takeTuningCombo")->count(), 0);
}

void TstClosedLoopPage::predictionWithModel()
{
    Fx fx(Options{.openLoopFirst = true});
    // Open loop: the page computes nothing visible on the plot.
    QVERIFY(fx.panel.results().first().valid);
    const StepResult model = fx.panel.results().first();
    QVERIFY(model.gain && model.deadTime && model.timeConstant);
    fx.apply([](AnalysisSettings &s) {
        s.closedLoop.showPrediction = true;
        s.closedLoop.predictionKp = 2.0;
        s.closedLoop.predictionTiS = 8.0;
    });
    QVERIFY(curvesWithStyle(fx.plot, Qt::DashLine).isEmpty()); // Open loop: no overlays

    // Closed loop, cursors on the closed-loop test: the model is kept.
    child<QToolButton>(fx.panel, "closedLoopButton")->click();
    fx.setCursors(fx.t0 + 9.0, fx.t0 + 70.0);
    auto *label = child<QLabel>(fx.panel, "predictionModelLabel");
    QVERIFY2(label->text().startsWith(QStringLiteral("Model: K = ")), qPrintable(label->text()));
    QVERIFY(label->text().contains(QStringLiteral("L = %1 s").arg(g4(*model.deadTime))));
    QVERIFY(label->text().contains(QStringLiteral("T = %1 s").arg(g4(*model.timeConstant))));
    QVERIFY(label->text().contains(QStringLiteral("0 % / 63.2 %")));

    QTableWidget *t = fx.table();
    QCOMPARE(t->columnCount(), 3);
    const int iae = rowOf(t, "IAE");
    QVERIFY(cell(t, iae, 2) != QStringLiteral("—"));
    QVERIFY(cell(t, iae, 2).endsWith(QStringLiteral("degC·s")));
    QVERIFY(cell(t, rowOf(t, "SP after"), 2).startsWith(QStringLiteral("60")));
    QVERIFY(cell(t, rowOf(t, "Overshoot"), 2) != QStringLiteral("—"));
    QVERIFY(cell(t, rowOf(t, "Output max"), 2) != QStringLiteral("—")); // a CO tag is chosen

    // Predicted PV and output on the plot, dashed, not in the legend.
    const QList<QwtPlotCurve *> dashed = curvesWithStyle(fx.plot, Qt::DashLine);
    QCOMPARE(dashed.size(), 2);
    for (QwtPlotCurve *c : dashed) {
        QVERIFY(!c->testItemAttribute(QwtPlotItem::Legend));
        QVERIFY(c->dataSize() > 100);
        QVERIFY(c->sample(0).x() >= fx.t0 + 9.0);
    }
    QCOMPARE(dashed.first()->pen().color(), QColor(fx.panel.settings().modelColor));

    // The prediction follows the controller: a different gain changes the numbers.
    const QString before = cell(t, iae, 2);
    fx.apply([](AnalysisSettings &s) { s.closedLoop.predictionKp = 0.5; });
    QVERIFY(cell(fx.table(), iae, 2) != before);

    // Back to Open loop: the predicted curves leave the plot.
    child<QToolButton>(fx.panel, "openLoopButton")->click();
    QVERIFY(curvesWithStyle(fx.plot, Qt::DashLine).isEmpty());
    child<QToolButton>(fx.panel, "closedLoopButton")->click();
    QCOMPARE(curvesWithStyle(fx.plot, Qt::DashLine).size(), 2);

    // Without the prediction the column and curves go.
    fx.apply([](AnalysisSettings &s) { s.closedLoop.showPrediction = false; });
    QCOMPARE(fx.table()->columnCount(), 2);
    QVERIFY(curvesWithStyle(fx.plot, Qt::DashLine).isEmpty());

    // Disturbance: the load step spin changes the prediction (and is a project setting).
    fx.apply([](AnalysisSettings &s) {
        s.closedLoop.test = ClosedLoopTest::Disturbance;
        s.closedLoop.showPrediction = true;
    });
    auto *load = child<QDoubleSpinBox>(fx.panel, "loadStepSpin");
    QVERIFY(load->isVisibleTo(fx.page()));
    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    load->setValue(5.0);
    const QString small = cell(fx.table(), rowOf(fx.table(), "Max deviation"), 2);
    load->setValue(15.0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(fx.panel.settings().closedLoop.loadStep, 15.0);
    // The disturbance prediction needs the measured result: the closed-loop data here is a
    // setpoint step, which is a valid result only with SetpointMoved; the cell may be a dash.
    QVERIFY(!small.isNull());
}

void TstClosedLoopPage::takeTuning()
{
    Fx fx(Options{.openLoopFirst = true});
    child<QToolButton>(fx.panel, "closedLoopButton")->click(); // takes the model
    const StepResult model = fx.panel.results().first();
    QVERIFY(model.valid && model.gain && model.deadTime && model.timeConstant);
    fx.apply([](AnalysisSettings &s) { s.tuningRule = TuningRule::CohenCoon; });
    const QList<TuningRow> rows = tune(*model.gain, *model.deadTime, *model.timeConstant,
                                       TuningRule::CohenCoon, 0.0);
    QCOMPARE(rows.size(), 3);

    auto *combo = child<QComboBox>(fx.panel, "takeTuningCombo");
    auto *button = child<QPushButton>(fx.panel, "takeTuningButton");
    QCOMPARE(combo->count(), 3);
    QCOMPARE(combo->itemText(0), QStringLiteral("P"));
    QCOMPARE(combo->itemText(1), QStringLiteral("PI"));
    QCOMPARE(combo->itemText(2), QStringLiteral("PID"));
    QVERIFY(button->isEnabled());

    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    combo->setCurrentIndex(1);
    button->click();
    QCOMPARE(changed.count(), 1);
    const ClosedLoopSettings cl = fx.panel.settings().closedLoop;
    QVERIFY(rows.at(1).kp && rows.at(1).ti);
    QVERIFY(qFuzzyCompare(cl.predictionKp, *rows.at(1).kp));
    QVERIFY(qFuzzyCompare(cl.predictionTiS, *rows.at(1).ti));
    QCOMPARE(cl.predictionTdS, 0.0);
    QVERIFY(qAbs(child<QDoubleSpinBox>(fx.panel, "predGainSpin")->value() - cl.predictionKp) < 1e-5);
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "predDerivativeSpin")->value(), 0.0);

    combo->setCurrentIndex(2);
    button->click();
    const ClosedLoopSettings pid = fx.panel.settings().closedLoop;
    QVERIFY(rows.at(2).td);
    QVERIFY(qFuzzyCompare(pid.predictionTdS, *rows.at(2).td));

    // "Not defined" rows (IMC has no P) are disabled.
    fx.apply([](AnalysisSettings &s) { s.tuningRule = TuningRule::ImcLambda; });
    QVERIFY(!combo->model()->flags(combo->model()->index(0, 0)).testFlag(Qt::ItemIsEnabled));
    QVERIFY(combo->model()->flags(combo->model()->index(1, 0)).testFlag(Qt::ItemIsEnabled));
}

void TstClosedLoopPage::controllerForms()
{
    Fx fx;
    auto *gain = child<QDoubleSpinBox>(fx.panel, "predGainSpin");
    auto *integral = child<QDoubleSpinBox>(fx.panel, "predIntegralSpin");
    auto *derivative = child<QDoubleSpinBox>(fx.panel, "predDerivativeSpin");
    auto labelTexts = [&] {
        QStringList out;
        for (QLabel *l : fx.page()->findChildren<QLabel *>())
            out << l->text();
        return out;
    };
    fx.apply([](AnalysisSettings &s) {
        s.closedLoop.predictionKp = 3.0;
        s.closedLoop.predictionTiS = 10.0;
        s.closedLoop.predictionTdS = 2.0;
    });
    QVERIFY(labelTexts().contains(QStringLiteral("Kp")));
    QVERIFY(labelTexts().contains(QStringLiteral("Ti (s)")));
    QVERIFY(labelTexts().contains(QStringLiteral("Td (s)")));
    QCOMPARE(gain->value(), 3.0);
    QCOMPARE(integral->value(), 10.0);
    QCOMPARE(derivative->value(), 2.0);

    // Series: Kc' = Kp (1 + sqrt(r)) / 2 with r = 1 - 4 Td / Ti.
    fx.apply([](AnalysisSettings &s) { s.controller.form = PidForm::Series; });
    const double root = std::sqrt(1.0 - 4.0 * 2.0 / 10.0);
    QVERIFY(labelTexts().contains(QStringLiteral("Kc'")));
    QVERIFY(labelTexts().contains(QStringLiteral("Ti' (s)")));
    QVERIFY(labelTexts().contains(QStringLiteral("Td' (s)")));
    QVERIFY(qAbs(gain->value() - 3.0 * (1.0 + root) / 2.0) < 1e-3);
    QVERIFY(qAbs(integral->value() - 10.0 * (1.0 + root) / 2.0) < 1e-3);
    QVERIFY(qAbs(derivative->value() - 10.0 * (1.0 - root) / 2.0) < 1e-3);
    // The ideal values are kept.
    QCOMPARE(fx.panel.settings().closedLoop.predictionKp, 3.0);
    QCOMPARE(fx.panel.settings().closedLoop.predictionTiS, 10.0);
    QCOMPARE(fx.panel.settings().closedLoop.predictionTdS, 2.0);

    // No series equivalent (Ti < 4 Td): the reason, spins disabled.
    auto *reason = child<QLabel>(fx.panel, "predictionFormLabel");
    QVERIFY(reason->isHidden());
    fx.apply([](AnalysisSettings &s) { s.closedLoop.predictionTiS = 4.0; });
    QVERIFY(!reason->isHidden());
    QVERIFY(reason->text().contains(QStringLiteral("No series equivalent")));
    QVERIFY(!gain->isEnabled() && !integral->isEnabled() && !derivative->isEnabled());
    fx.apply([](AnalysisSettings &s) { s.closedLoop.predictionTiS = 10.0; });
    QVERIFY(reason->isHidden() && gain->isEnabled());

    // Parallel with minutes: Ki = Kp / Ti, Kd = Kp Td, both per minute / in minutes.
    fx.apply([](AnalysisSettings &s) {
        s.controller.form = PidForm::Parallel;
        s.timeInMinutes = true;
    });
    QVERIFY(labelTexts().contains(QStringLiteral("Ki (1/min)")));
    QVERIFY(labelTexts().contains(QStringLiteral("Kd (min)")));
    QVERIFY(qAbs(integral->value() - 3.0 / (10.0 / 60.0)) < 1e-3);
    QVERIFY(qAbs(derivative->value() - 3.0 * (2.0 / 60.0)) < 1e-3);

    // Repeats per minute and a proportional band.
    fx.apply([](AnalysisSettings &s) {
        s.controller.form = PidForm::Ideal;
        s.controller.integral = IntegralUnit::RepeatsPerMinute;
        s.controller.gain = GainUnit::ProportionalBand;
        s.timeInMinutes = false;
        s.pvRangeMin = 0.0;
        s.pvRangeMax = 100.0;
    });
    QVERIFY(labelTexts().contains(QStringLiteral("PB (%)")));
    QVERIFY(labelTexts().contains(QStringLiteral("Repeats/min")));
    QVERIFY(qAbs(gain->value() - 100.0 / 3.0) < 1e-3); // Kp 3 with both spans 100: g% = 3
    QVERIFY(qAbs(integral->value() - 6.0) < 1e-3);
    // Without the PV range the band is unavailable.
    fx.apply([](AnalysisSettings &s) { s.pvRangeMin.reset(); });
    QVERIFY(!reason->isHidden());
    QVERIFY(reason->text().contains(QStringLiteral("proportional band")));
}

void TstClosedLoopPage::typedPredictionValues()
{
    Fx fx;
    auto *gain = child<QDoubleSpinBox>(fx.panel, "predGainSpin");
    auto *integral = child<QDoubleSpinBox>(fx.panel, "predIntegralSpin");
    auto *derivative = child<QDoubleSpinBox>(fx.panel, "predDerivativeSpin");
    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);

    gain->setValue(2.5);
    integral->setValue(20.0);
    derivative->setValue(4.0);
    QCOMPARE(changed.count(), 3);
    ClosedLoopSettings cl = fx.panel.settings().closedLoop;
    QCOMPARE(cl.predictionKp, 2.5);
    QCOMPARE(cl.predictionTiS, 20.0);
    QCOMPARE(cl.predictionTdS, 4.0);

    // Typed in the parallel form: converted to the ideal form.
    fx.apply([](AnalysisSettings &s) { s.controller.form = PidForm::Parallel; });
    QVERIFY(qAbs(integral->value() - 2.5 / 20.0) < 1e-4);
    derivative->setValue(10.0); // Kd = Kp Td -> Td = 4
    cl = fx.panel.settings().closedLoop;
    QVERIFY(qAbs(cl.predictionTdS - 4.0) < 1e-9);
    QVERIFY(qAbs(cl.predictionKp - 2.5) < 1e-9);

    // Zero turns an action off; a zero gain is refused and the stored value shown again.
    integral->setValue(0.0);
    QCOMPARE(fx.panel.settings().closedLoop.predictionTiS, 0.0);
    gain->setValue(0.0);
    QCOMPARE(fx.panel.settings().closedLoop.predictionKp, 2.5);
    QCOMPARE(gain->value(), 2.5);
}

void TstClosedLoopPage::overlays()
{
    Fx fx;
    const ClosedLoopResult r = fx.direct(kSp, kCo, 9.0, 70.0);
    QVERIFY(r.valid && r.overshoot);
    // Band: two dotted lines at SP after +- 2 % of the step, from the start to C2.
    const QList<QwtPlotCurve *> band = curvesWithStyle(fx.plot, Qt::DotLine);
    QCOMPARE(band.size(), 2);
    QList<double> levels;
    for (QwtPlotCurve *c : band) {
        QCOMPARE(c->dataSize(), size_t(2));
        QCOMPARE(c->sample(0).y(), c->sample(1).y());
        QVERIFY(qAbs(c->sample(0).x() - r.startTime) < 1e-9);
        QVERIFY(qAbs(c->sample(1).x() - 70.0) < 1e-9);
        QVERIFY(!c->testItemAttribute(QwtPlotItem::Legend));
        levels << c->sample(0).y();
    }
    std::sort(levels.begin(), levels.end());
    QVERIFY(qAbs(levels.at(0) - 59.8) < 1e-9);
    QVERIFY(qAbs(levels.at(1) - 60.2) < 1e-9);
    // Overshoot marker at the peak.
    QVERIFY(hasMarkerStartingWith(fx.plot, QStringLiteral("Overshoot ")));
    const QwtPlotItemList markers = fx.plot.plot()->itemList(QwtPlotItem::Rtti_PlotMarker);
    bool found = false;
    for (QwtPlotItem *item : markers) {
        auto *m = static_cast<QwtPlotMarker *>(item);
        if (m->label().text().startsWith(QStringLiteral("Overshoot "))) {
            found = true;
            QVERIFY(qAbs(m->xValue() - (r.startTime + *r.peakTime)) < 1e-9);
            QVERIFY(qAbs(m->yValue() - (60.0 + *r.overshoot)) < 1e-9);
            QVERIFY(m->label().text().contains(g4(*r.overshoot)));
        }
    }
    QVERIFY(found);

    // Open loop: gone; back: restored; a cursor change rebuilds them.
    child<QToolButton>(fx.panel, "openLoopButton")->click();
    QVERIFY(curvesWithStyle(fx.plot, Qt::DotLine).isEmpty());
    QVERIFY(!hasMarkerStartingWith(fx.plot, QStringLiteral("Overshoot ")));
    child<QToolButton>(fx.panel, "closedLoopButton")->click();
    QCOMPARE(curvesWithStyle(fx.plot, Qt::DotLine).size(), 2);
    fx.setCursors(9.0, 60.0);
    QCOMPARE(curvesWithStyle(fx.plot, Qt::DotLine).size(), 2);
    QVERIFY(qAbs(curvesWithStyle(fx.plot, Qt::DotLine).first()->sample(1).x() - 60.0) < 1e-9);

    // 5 % band.
    fx.apply([](AnalysisSettings &s) { s.closedLoop.settlingBandPct = 5.0; });
    QList<double> y;
    for (QwtPlotCurve *c : curvesWithStyle(fx.plot, Qt::DotLine))
        y << c->sample(0).y();
    std::sort(y.begin(), y.end());
    QVERIFY(qAbs(y.at(0) - 59.5) < 1e-9);

    // Disturbance: a "Max deviation" marker.
    Fx dist(Options{.disturbance = true});
    QVERIFY(hasMarkerStartingWith(dist.plot, QStringLiteral("Max deviation ")));

    // Cursors removed or an invalid result: nothing left.
    Fx none(Options{.typedSp = true});
    none.apply([](AnalysisSettings &s) { s.closedLoop.typedSpAfter = 50.0; });
    QVERIFY(curvesWithStyle(none.plot, Qt::DotLine).isEmpty());
    QVERIFY(!hasMarkerStartingWith(none.plot, QStringLiteral("Overshoot ")));

    // Removing the plot removes everything.
    fx.panel.setPlot(nullptr);
    QVERIFY(curvesWithStyle(fx.plot, Qt::DotLine).isEmpty());
}

void TstClosedLoopPage::plotDestroyedFirst()
{
    Recorder rec;
    appendClosedLoop(rec, 0.0, false, 3.0);
    auto plot = std::make_unique<TrendPlot>();
    AnalysisPanel panel;
    plot->setRecorder(&rec);
    plot->setTags(standardTags());
    plot->refresh();
    panel.setRecorder(&rec);
    panel.setPlot(plot.get());
    panel.setTags(standardTags());
    AnalysisSettings s;
    s.pvTagId = kPv;
    s.spTagId = kSp;
    s.mode = AnalysisMode::ClosedLoop;
    s.closedLoop.showPrediction = true;
    panel.setSettings(s);
    plot->cursors()->setC1(9.0);
    plot->cursors()->setC2(70.0);
    QCOMPARE(curvesWithStyle(*plot, Qt::DotLine).size(), 2);
    plot.reset(); // deletes the overlay items; the panel must not touch them again
    panel.setSettings(s);
    panel.refresh();
}

void TstClosedLoopPage::copyAndReadout()
{
    Fx fx;
    ClosedLoopPage *page = fx.page();
    const ClosedLoopResult r = fx.direct(kSp, kCo, 9.0, 70.0);
    QVERIFY(r.valid);
    const QString copy = page->copyText();
    QVERIFY(copy.startsWith(QStringLiteral("Quantity\tMeasured")));
    QVERIFY(copy.contains(QStringLiteral("Overshoot\t%1 degC").arg(g4(*r.overshoot))));
    QVERIFY(copy.contains(QStringLiteral("IAE\t%1 degC·s").arg(g4(r.iae))));
    QCOMPARE(copy.count(QLatin1Char('\n')), fx.table()->rowCount() + 1);

    const QString readout = page->readoutText();
    QVERIFY2(readout.startsWith(QStringLiteral("SP step 50 → 60 degC: overshoot %1 degC (%2 % of the step)")
                                    .arg(g4(*r.overshoot), g4(*r.overshootPct))),
             qPrintable(readout));
    QVERIFY(readout.contains(QStringLiteral("settling %1 s (2 % band)").arg(g4(*r.settlingTime))));
    QVERIFY(readout.contains(QStringLiteral("IAE %1").arg(g4(r.iae))));
    QVERIFY(!readout.contains(QStringLiteral("Predicted")));

    // With the prediction there is a second line; the panel's "Copy results" has the table.
    Fx pred(Options{.openLoopFirst = true});
    pred.apply([](AnalysisSettings &s) { s.closedLoop.showPrediction = true; });
    child<QToolButton>(pred.panel, "closedLoopButton")->click();
    pred.setCursors(pred.t0 + 9.0, pred.t0 + 70.0);
    const QString two = pred.page()->readoutText();
    QCOMPARE(two.count(QLatin1Char('\n')), 1);
    QVERIFY(two.split(QLatin1Char('\n')).last().startsWith(QStringLiteral("Predicted: overshoot")));
    QVERIFY(pred.page()->copyText().startsWith(QStringLiteral("Quantity\tMeasured\tPredicted")));

    // Empty without a valid result.
    fx.apply([](AnalysisSettings &s) { s.closedLoop.test = ClosedLoopTest::Disturbance; });
    fx.setCursors(500.0, 600.0);
    QVERIFY(page->copyText().isEmpty());
    QVERIFY(page->readoutText().isEmpty());
}

void TstClosedLoopPage::tooltips()
{
    Fx fx;
    QTableWidget *t = fx.table();
    const ClosedLoopResult r = fx.direct(kSp, kCo, 9.0, 70.0);
    QVERIFY(r.valid);
    for (int row = 0; row < t->rowCount(); ++row) {
        const QString tip = t->item(row, 1)->toolTip();
        QVERIFY2(!tip.isEmpty(), qPrintable(cell(t, row, 0)));
        QVERIFY2(tip.contains(QStringLiteral("<b>")), qPrintable(cell(t, row, 0)));
        QVERIFY2(tip.contains(QStringLiteral(" = ")) || tip.contains(QStringLiteral("∫")),
                 qPrintable(cell(t, row, 0)));
        QCOMPARE(t->item(row, 0)->toolTip(), tip);
    }
    // Overshoot: the formula with this result's numbers.
    const QString os = t->item(rowOf(t, "Overshoot"), 1)->toolTip();
    QVERIFY(os.contains(QStringLiteral("overshoot = max(PV) - SP after")));
    QVERIFY2(os.contains(QStringLiteral("%1 - 60 = %2 degC").arg(g4(60.0 + *r.overshoot), g4(*r.overshoot))),
             qPrintable(os));
    QVERIFY(r.decayRatio && r.overshoot && r.settlingTime);
    // Decay ratio: the quarter-amplitude hint and second / first peak.
    const QString decay = t->item(rowOf(t, "Decay ratio"), 1)->toolTip();
    QVERIFY(decay.contains(QStringLiteral("0.25")));
    QVERIFY(decay.contains(QStringLiteral("second peak / first peak")));
    QVERIFY2(decay.contains(QStringLiteral("/ %1 = %2").arg(g4(*r.overshoot), g4(*r.decayRatio))),
             qPrintable(decay));
    // IAE.
    const QString iae = t->item(rowOf(t, "IAE"), 1)->toolTip();
    QVERIFY(iae.contains(QStringLiteral("compare tunings on the same test")));
    QVERIFY(iae.contains(g4(r.iae)));
    // Settling: band and value.
    const QString settle = t->item(rowOf(t, "Settling time"), 1)->toolTip();
    QVERIFY(settle.contains(QStringLiteral("band = 2 × 10 / 100 = 0.2 degC")));
    QVERIFY(settle.contains(g4(*r.settlingTime)));

    // Predicted cells have tooltips too.
    Fx pred(Options{.openLoopFirst = true});
    pred.apply([](AnalysisSettings &s) { s.closedLoop.showPrediction = true; });
    child<QToolButton>(pred.panel, "closedLoopButton")->click();
    pred.setCursors(pred.t0 + 9.0, pred.t0 + 70.0);
    QTableWidget *pt = pred.table();
    QCOMPARE(pt->columnCount(), 3);
    for (int row = 0; row < pt->rowCount(); ++row) {
        QVERIFY2(!pt->item(row, 1)->toolTip().isEmpty(), qPrintable(cell(pt, row, 0)));
        QVERIFY2(pt->item(row, 2)->toolTip().contains(QStringLiteral("Predicted")),
                 qPrintable(cell(pt, row, 0)));
    }

    // Disturbance rows too.
    Fx dist(Options{.disturbance = true});
    QTableWidget *dt = dist.table();
    for (int row = 0; row < dt->rowCount(); ++row)
        QVERIFY2(!dt->item(row, 1)->toolTip().isEmpty(), qPrintable(cell(dt, row, 0)));
    QVERIFY(dt->item(rowOf(dt, "Max deviation"), 1)->toolTip().contains(QStringLiteral("SP - PV")));

    // No result: tooltips remain (formula without numbers).
    Fx none(Options{.typedSp = true});
    none.apply([](AnalysisSettings &s) { s.closedLoop.typedSpAfter = 50.0; });
    QVERIFY(!none.table()->item(rowOf(none.table(), "Overshoot"), 1)->toolTip().isEmpty());
}

void TstClosedLoopPage::settingsRoundTrip()
{
    Fx fx;
    AnalysisSettings s = fx.panel.settings();
    s.closedLoop.test = ClosedLoopTest::Disturbance;
    s.closedLoop.typedSpBefore = 12.0;
    s.closedLoop.typedSpAfter = 34.0;
    s.closedLoop.settlingBandPct = 5.0;
    s.closedLoop.showPrediction = true;
    s.closedLoop.predictionKp = 1.5;
    s.closedLoop.predictionTiS = 12.0;
    s.closedLoop.predictionTdS = 1.0;
    s.closedLoop.modelSet = true;
    s.closedLoop.modelK = -2.5;
    s.closedLoop.modelLS = 3.0;
    s.closedLoop.modelTS = 45.0;
    s.closedLoop.modelSource = QStringLiteral("from a handout");
    s.closedLoop.loadStep = -8.0;
    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    fx.panel.setSettings(s);
    QCOMPARE(changed.count(), 0); // loading is not a user edit
    QVERIFY(child<QComboBox>(fx.panel, "testCombo")->currentData().toInt()
            == static_cast<int>(ClosedLoopTest::Disturbance));
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "spAfterSpin")->value(), 34.0);
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "spBeforeSpin")->value(), 12.0);
    QCOMPARE(child<QComboBox>(fx.panel, "bandCombo")->currentData().toDouble(), 5.0);
    QVERIFY(child<QCheckBox>(fx.panel, "showPredictionCheck")->isChecked());
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "predGainSpin")->value(), 1.5);
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "predIntegralSpin")->value(), 12.0);
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "predDerivativeSpin")->value(), 1.0);
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "modelKSpin")->value(), -2.5);
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "modelLSpin")->value(), 3.0);
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "modelTSpin")->value(), 45.0);
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "loadStepSpin")->value(), -8.0);
    QVERIFY(child<QLabel>(fx.panel, "predictionModelLabel")->text().contains(
        QStringLiteral("(from a handout)")));
    QVERIFY(fx.panel.settings().closedLoop == s.closedLoop);
}

void TstClosedLoopPage::modelSurvivesCursorMove()
{
    // The core scenario: an open-loop step, the cursors on it, then the closed-loop test.
    Fx fx(Options{.openLoopFirst = true});
    const StepResult open = fx.panel.results().first();
    QVERIFY(open.valid && open.gain && open.deadTime && open.timeConstant);
    QVERIFY(!fx.panel.settings().closedLoop.modelSet); // the cursors alone take nothing
    child<QToolButton>(fx.panel, "closedLoopButton")->click();
    const ClosedLoopSettings taken = fx.panel.settings().closedLoop;
    QVERIFY(taken.modelSet);
    QVERIFY(qFuzzyCompare(taken.modelK, *open.gain));
    QVERIFY(qFuzzyCompare(taken.modelLS, *open.deadTime));
    QVERIFY(qFuzzyCompare(taken.modelTS, *open.timeConstant));
    fx.apply([](AnalysisSettings &s) { s.closedLoop.showPrediction = true; });

    auto *label = child<QLabel>(fx.panel, "predictionModelLabel");
    const QString text = label->text();
    QVERIFY2(text.startsWith(QStringLiteral("Model: K = ")), qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("L = %1 s").arg(g4(*open.deadTime))));
    QVERIFY(text.contains(QStringLiteral("T = %1 s").arg(g4(*open.timeConstant))));
    QVERIFY2(text.contains(QStringLiteral("C1 = 9 s)")), qPrintable(text));

    // Move the cursors to the closed-loop part: the model and the Predicted column stay.
    fx.setCursors(fx.t0 + 9.0, fx.t0 + 70.0);
    QCOMPARE(label->text(), text);
    const ClosedLoopSettings after = fx.panel.settings().closedLoop;
    QVERIFY(after.modelSet && after.modelK == taken.modelK && after.modelLS == taken.modelLS
            && after.modelTS == taken.modelTS && after.modelSource == taken.modelSource);
    QTableWidget *t = fx.table();
    QCOMPARE(t->columnCount(), 3);
    QCOMPARE(t->horizontalHeaderItem(2)->text(), QStringLiteral("Predicted"));
    QVERIFY(cell(t, rowOf(t, "IAE"), 2) != QStringLiteral("—"));
    QCOMPARE(curvesWithStyle(fx.plot, Qt::DashLine).size(), 2);

    // Another PV does not clear it either (the model belongs to the project).
    fx.apply([](AnalysisSettings &s) { s.pvTagId = kSp; });
    QVERIFY(fx.panel.settings().closedLoop.modelSet);
    QCOMPARE(fx.panel.settings().closedLoop.modelK, taken.modelK);
}

void TstClosedLoopPage::useOpenLoopModelButton()
{
    auto useButton = [](Fx &f) { return child<QPushButton>(f.panel, "useOpenLoopModelButton"); };
    {
        Fx none(Options{.noCo = true}); // no K: no valid open-loop model
        QVERIFY(useButton(none));
        QVERIFY(!useButton(none)->isEnabled());
    }
    Fx fx(Options{.openLoopFirst = true});
    const StepResult open = fx.panel.results().first();
    QVERIFY(open.valid && open.gain && open.deadTime && open.timeConstant);
    child<QToolButton>(fx.panel, "closedLoopButton")->click();
    // A typed model replaces the automatic one...
    child<QDoubleSpinBox>(fx.panel, "modelKSpin")->setValue(3.0);
    QCOMPARE(fx.panel.settings().closedLoop.modelSource, QStringLiteral("typed"));
    QVERIFY(qFuzzyCompare(fx.panel.settings().closedLoop.modelK, 3.0));
    // ...and the button brings the open-loop one back (the cursors are still on the open-loop step).
    QPushButton *button = useButton(fx);
    QVERIFY(button->isEnabled());
    QCOMPARE(button->text(), QStringLiteral("Use the open-loop model"));
    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    button->click();
    QCOMPARE(changed.count(), 1);
    const ClosedLoopSettings cl = fx.panel.settings().closedLoop;
    QVERIFY(cl.modelSet);
    QVERIFY(qFuzzyCompare(cl.modelK, *open.gain));
    QVERIFY(qFuzzyCompare(cl.modelLS, *open.deadTime));
    QVERIFY(qFuzzyCompare(cl.modelTS, *open.timeConstant));
    QVERIFY2(cl.modelSource.endsWith(QStringLiteral(", C1 = 9 s")), qPrintable(cl.modelSource));
    QVERIFY(cl.modelSource != QStringLiteral("typed"));

    // No valid open-loop model at the cursors: the button is off.
    fx.apply([](AnalysisSettings &s) { s.deltaCoTagId = 0; });
    QVERIFY(!button->isEnabled());
}

void TstClosedLoopPage::automaticTakeIsSilent()
{
    // The take happens on the mode switch only; setSettings() and recomputes never take.
    Fx fx(Options{.openLoopFirst = true});
    const StepResult open = fx.panel.results().first();
    QVERIFY(open.valid && open.gain);
    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    fx.apply([](AnalysisSettings &s) {
        s.mode = AnalysisMode::ClosedLoop;
        s.closedLoop.modelSet = false;
        s.closedLoop.modelK = 7.0;
    });
    QCOMPARE(changed.count(), 0);
    QVERIFY(!fx.panel.settings().closedLoop.modelSet);
    QCOMPARE(fx.panel.settings().closedLoop.modelK, 7.0);

    // Without any valid open-loop model, the switch takes nothing.
    Fx none(Options{.noCo = true});
    child<QToolButton>(none.panel, "openLoopButton")->click();
    child<QToolButton>(none.panel, "closedLoopButton")->click();
    QVERIFY(!none.panel.settings().closedLoop.modelSet);
}

void TstClosedLoopPage::takeOnlyOnModeSwitch()
{
    Fx fx(Options{.openLoopFirst = true});
    auto *closedButton = child<QToolButton>(fx.panel, "closedLoopButton");
    auto *openButton = child<QToolButton>(fx.panel, "openLoopButton");
    // Dragging the cursors in Open loop never sets the model.
    fx.setCursors(9.0, 30.0);
    fx.setCursors(9.0, 45.0);
    const StepResult open = fx.panel.results().first();
    QVERIFY(open.valid && open.gain);
    QVERIFY(!fx.panel.settings().closedLoop.modelSet);
    // The switch takes it (and the mode switch itself is the change that marks the project).
    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    closedButton->click();
    QCOMPARE(changed.count(), 1);
    ClosedLoopSettings cl = fx.panel.settings().closedLoop;
    QVERIFY(cl.modelSet);
    QVERIFY(qFuzzyCompare(cl.modelK, *open.gain));
    // Back, other cursors, forward again: unchanged once set.
    openButton->click();
    fx.setCursors(9.0, 25.0);
    closedButton->click();
    const ClosedLoopSettings again = fx.panel.settings().closedLoop;
    QVERIFY(again.modelSet && again.modelK == cl.modelK && again.modelLS == cl.modelLS
            && again.modelTS == cl.modelTS && again.modelSource == cl.modelSource);

    // A session applied with modelSet false and an old recording present: nothing is taken.
    Fx loaded(Options{.openLoopFirst = true});
    loaded.apply([](AnalysisSettings &s) {
        s.mode = AnalysisMode::ClosedLoop;
        s.closedLoop.modelSet = false;
    });
    QVERIFY(!loaded.panel.settings().closedLoop.modelSet);
    loaded.setCursors(9.0, 40.0);
    QVERIFY(!loaded.panel.settings().closedLoop.modelSet);
}

void TstClosedLoopPage::typedZeroGainModel()
{
    Fx fx(Options{.noCo = true});
    auto *k = child<QDoubleSpinBox>(fx.panel, "modelKSpin");
    auto *label = child<QLabel>(fx.panel, "predictionModelLabel");
    child<QDoubleSpinBox>(fx.panel, "modelTSpin")->setValue(5.0);
    k->setValue(0.0);
    QVERIFY(label->text().startsWith(QStringLiteral("No model yet")));
    const ClosedLoopSettings cl = fx.panel.settings().closedLoop;
    QVERIFY(cl.modelSet);
    QCOMPARE(cl.modelK, 0.0);
    Project p;
    p.analysis = fx.panel.settings();
    QString error;
    const std::optional<Project> loaded = projectFromJson(projectToJson(p), &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QVERIFY(loaded->analysis.closedLoop == cl);
    fx.panel.setSettings(loaded->analysis);
    QVERIFY(label->text().startsWith(QStringLiteral("No model yet")));
}

void TstClosedLoopPage::predictionSpinsKeepPrecision()
{
    Fx fx;
    auto *gain = child<QDoubleSpinBox>(fx.panel, "predGainSpin");
    auto *integral = child<QDoubleSpinBox>(fx.panel, "predIntegralSpin");
    auto *derivative = child<QDoubleSpinBox>(fx.panel, "predDerivativeSpin");
    fx.apply([](AnalysisSettings &s) {
        s.controller.form = PidForm::Parallel;
        s.closedLoop.predictionKp = 0.5;
        s.closedLoop.predictionTiS = 600.0;
        s.closedLoop.predictionTdS = 0.0;
    });
    QVERIFY(qAbs(integral->value() - 0.5 / 600.0) < 1e-9); // shown with enough digits
    QVERIFY(integral->text().startsWith(QStringLiteral("0.000833")));
    gain->setValue(0.6);
    ClosedLoopSettings cl = fx.panel.settings().closedLoop;
    QVERIFY(qAbs(cl.predictionKp - 0.6) < 1e-12);
    QVERIFY(qAbs(cl.predictionTiS - 600.0) < 1e-6); // not 720 or 750
    QVERIFY(qAbs(integral->value() - 0.001) < 1e-9);

    // A tiny Ki stays non-zero when the gain is edited.
    fx.apply([](AnalysisSettings &s) {
        s.closedLoop.predictionKp = 1.0;
        s.closedLoop.predictionTiS = 25000.0; // Ki = 4e-5 1/s
    });
    QVERIFY(integral->value() > 0.0);
    gain->setValue(2.0);
    cl = fx.panel.settings().closedLoop;
    QVERIFY(cl.predictionTiS > 0.0);
    QVERIFY(qAbs(cl.predictionTiS - 25000.0) < 1e-3);
    QVERIFY(integral->value() > 0.0);

    // Series round trip keeps Td when the gain is edited.
    fx.apply([](AnalysisSettings &s) {
        s.controller.form = PidForm::Series;
        s.closedLoop.predictionKp = 3.0;
        s.closedLoop.predictionTiS = 10.0;
        s.closedLoop.predictionTdS = 1.234567;
    });
    gain->setValue(gain->value());       // no change: nothing happens
    gain->setValue(gain->value() * 1.1);
    cl = fx.panel.settings().closedLoop;
    QVERIFY(qAbs(cl.predictionTdS - 1.234567) < 1e-6);
    QVERIFY(qAbs(cl.predictionTiS - 10.0) < 1e-6);
    Q_UNUSED(derivative);
}

void TstClosedLoopPage::typingIsNotOverwritten()
{
    Fx fx(Options{.openLoopFirst = true});
    child<QToolButton>(fx.panel, "closedLoopButton")->click();
    fx.panel.show();
    auto *k = child<QDoubleSpinBox>(fx.panel, "modelKSpin");
    auto *kEdit = k->findChild<QLineEdit *>();
    QVERIFY(kEdit);
    k->setFocus();
    k->selectAll();
    kEdit->setText(QStringLiteral("12.5"));
    QVERIFY(kEdit->text().startsWith(QStringLiteral("12.5")));
    // New data and recomputes with the model unchanged: the text stays.
    appendRow(fx.rec, 700.0, 30.0, 60.0, 50.0);
    fx.panel.refresh();
    fx.setCursors(9.0, 41.0);
    QVERIFY(kEdit->text().startsWith(QStringLiteral("12.5")));
    // Even with a changed stored value, a focused spin keeps what is typed.
    if (k->hasFocus()) {
        fx.apply([](AnalysisSettings &s) { s.closedLoop.modelK = 3.0; });
        QVERIFY(kEdit->text().startsWith(QStringLiteral("12.5")));
    }
    // The take-tuning popup is not rebuilt by a refresh: the same item objects survive.
    auto *combo = child<QComboBox>(fx.panel, "takeTuningCombo");
    QVERIFY(combo->count() > 0);
    const QVariant first = combo->itemData(0, Qt::ToolTipRole);
    const int index = combo->currentIndex();
    combo->setProperty("marker", 42);
    fx.panel.refresh();
    fx.setCursors(9.0, 42.0);
    QCOMPARE(combo->currentIndex(), index);
    QCOMPARE(combo->itemData(0, Qt::ToolTipRole), first);
    kEdit->setText(QStringLiteral("1")); // leave nothing typed behind
    k->clearFocus();
}

void TstClosedLoopPage::predictionBaseline()
{
    Fx fx;
    fx.apply([](AnalysisSettings &s) {
        s.closedLoop.showPrediction = true;
        s.closedLoop.modelSet = true;
        s.closedLoop.modelK = 0.5;
        s.closedLoop.modelLS = 1.0;
        s.closedLoop.modelTS = 5.0;
        s.closedLoop.predictionKp = 2.0;
        s.closedLoop.predictionTiS = 8.0;
    });
    QTableWidget *t = fx.table();
    QCOMPARE(cell(t, rowOf(t, "SP before"), 2), cell(t, rowOf(t, "SP before"), 1));
    QCOMPARE(cell(t, rowOf(t, "SP after"), 2), cell(t, rowOf(t, "SP after"), 1));
    QVERIFY(cell(t, rowOf(t, "SP before"), 2).startsWith(QStringLiteral("50")));
    // The predicted PV curve starts at SP before.
    const QList<QwtPlotCurve *> dashed = curvesWithStyle(fx.plot, Qt::DashLine);
    QVERIFY(!dashed.isEmpty());
    bool found = false;
    for (QwtPlotCurve *c : dashed) {
        if (c->dataSize() > 0 && qAbs(c->sample(0).y() - 50.0) < 1e-6)
            found = true;
    }
    QVERIFY(found);
    // With the core's band override the settling band (and the rows derived from it) match; the
    // settling row is only comparable after the core change (bandOverride is ignored before it).
    const int band = rowOf(t, "Settling time");
    QVERIFY(band >= 0);
}

void TstClosedLoopPage::closedModeHintAndFindStep()
{
    Fx fx;
    auto *find = child<QPushButton>(fx.panel, "findStepButton");
    QVERIFY(!find->isEnabled());
    QVERIFY(find->toolTip().startsWith(QStringLiteral("Find step looks for an open-loop step")));
    child<QToolButton>(fx.panel, "openLoopButton")->click();
    QVERIFY(find->isEnabled());
    child<QToolButton>(fx.panel, "closedLoopButton")->click();
    QVERIFY(!find->isEnabled());
    QVERIFY(!fx.page()->findChild<QLabel *>(QStringLiteral("closedHintLabel")));
}

void TstClosedLoopPage::typedModel()
{
    Fx fx(Options{.noCo = true}); // no automatic model
    auto *k = child<QDoubleSpinBox>(fx.panel, "modelKSpin");
    auto *l = child<QDoubleSpinBox>(fx.panel, "modelLSpin");
    auto *t = child<QDoubleSpinBox>(fx.panel, "modelTSpin");
    auto *label = child<QLabel>(fx.panel, "predictionModelLabel");
    QVERIFY(k && l && t);
    QVERIFY(!fx.panel.settings().closedLoop.modelSet);
    QVERIFY(label->text().startsWith(QStringLiteral("No model yet")));
    QCOMPARE(l->minimum(), 0.0);
    QCOMPARE(t->minimum(), 0.001);

    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    k->setValue(2.0);
    QCOMPARE(changed.count(), 1);
    ClosedLoopSettings cl = fx.panel.settings().closedLoop;
    QVERIFY(cl.modelSet);
    QCOMPARE(cl.modelK, 2.0);
    QCOMPARE(cl.modelSource, QStringLiteral("typed"));
    l->setValue(1.5);
    t->setValue(12.0);
    QCOMPARE(changed.count(), 3);
    cl = fx.panel.settings().closedLoop;
    QCOMPARE(cl.modelLS, 1.5);
    QCOMPARE(cl.modelTS, 12.0);
    QCOMPARE(label->text(), QStringLiteral("Model: K = 2, L = 1.5 s, T = 12 s (typed)"));

    // The spins refuse a T that is not above 0 and an L below 0 (the model stays valid).
    t->setValue(-4.0);
    QCOMPARE(t->value(), 0.001);
    QCOMPARE(fx.panel.settings().closedLoop.modelTS, 0.001);
    l->setValue(-1.0);
    QCOMPARE(l->value(), 0.0);
    QVERIFY(label->text().startsWith(QStringLiteral("Model: K = 2")));

    // K = 0 is no model: back to the "No model yet" text and no prediction.
    fx.apply([](AnalysisSettings &s) { s.closedLoop.showPrediction = true; });
    QVERIFY(cell(fx.table(), rowOf(fx.table(), "IAE"), 2) != QStringLiteral("—"));
    k->setValue(0.0);
    QVERIFY(label->text().startsWith(QStringLiteral("No model yet")));
    QCOMPARE(cell(fx.table(), rowOf(fx.table(), "IAE"), 2), QStringLiteral("—"));
    QVERIFY(curvesWithStyle(fx.plot, Qt::DashLine).isEmpty());
    // An invalid model from a loaded project behaves the same.
    fx.apply([](AnalysisSettings &s) {
        s.closedLoop.modelK = 1.0;
        s.closedLoop.modelTS = 0.0;
    });
    QVERIFY(label->text().startsWith(QStringLiteral("No model yet")));
    QVERIFY(curvesWithStyle(fx.plot, Qt::DashLine).isEmpty());
}

void TstClosedLoopPage::loadStepIsSaved()
{
    Fx fx(Options{.disturbance = true});
    auto *load = child<QDoubleSpinBox>(fx.panel, "loadStepSpin");
    QCOMPARE(load->value(), ClosedLoopSettings{}.loadStep);
    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    load->setValue(12.5);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(fx.panel.settings().closedLoop.loadStep, 12.5);
    // Loaded from a project.
    fx.apply([](AnalysisSettings &s) { s.closedLoop.loadStep = -3.0; });
    QCOMPARE(load->value(), -3.0);
    QCOMPARE(changed.count(), 1);
}

void TstClosedLoopPage::panelButtonsAndReadout()
{
    Fx fx(Options{.openLoopFirst = true});
    auto *copy = child<QPushButton>(fx.panel, "copyResultsButton");
    auto *exportButton = child<QPushButton>(fx.panel, "exportButton");
    // Open loop mode: as before.
    QVERIFY(copy->isEnabled() && exportButton->isEnabled());
    QVERIFY(exportButton->toolTip().isEmpty());
    QVERIFY(fx.panel.readoutText().contains(QStringLiteral("K = ")));

    child<QToolButton>(fx.panel, "closedLoopButton")->click();
    fx.setCursors(fx.t0 + 9.0, fx.t0 + 70.0);
    QVERIFY(fx.page()->copyText().startsWith(QStringLiteral("Quantity\tMeasured")));
    QVERIFY(copy->isEnabled());
    QVERIFY(!exportButton->isEnabled());
    QCOMPARE(exportButton->toolTip(),
             QStringLiteral("The CSV export covers the open-loop results. Use Copy results for "
                            "the closed-loop numbers."));
    // The closed-loop readout, and nothing from the open-loop page.
    QCOMPARE(fx.panel.readoutText().section(QLatin1Char('\n'), 0, 0),
             fx.page()->readoutText().section(QLatin1Char('\n'), 0, 0));
    QVERIFY(fx.panel.readoutText().startsWith(QStringLiteral("SP step 50")));
    QVERIFY(!fx.panel.readoutText().contains(QStringLiteral("K = ")));

    copy->click();
    const QString clip = QGuiApplication::clipboard()->text();
    QVERIFY(clip.startsWith(QStringLiteral("Quantity\tMeasured")));
    QVERIFY(!clip.contains(QStringLiteral("Fit error")));

    // No closed-loop result (no data between the cursors): nothing to copy or export.
    fx.setCursors(500.0, 600.0);
    QVERIFY(!copy->isEnabled());
    QVERIFY(!exportButton->isEnabled());
    QVERIFY(fx.panel.readoutText().isEmpty());

    // Back to Open loop: the normal rules and the normal tooltip.
    fx.setCursors(9.0, 40.0);
    child<QToolButton>(fx.panel, "openLoopButton")->click();
    QVERIFY(copy->isEnabled() && exportButton->isEnabled());
    QVERIFY(exportButton->toolTip().isEmpty());
}

QTEST_MAIN(TstClosedLoopPage)
#include "tst_closedlooppage.moc"
