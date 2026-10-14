#include "AnalysisPanel.h"
#include "PlotCursors.h"
#include "Recorder.h"
#include "SampleBatch.h"
#include "StepAnalysis.h"
#include "Tag.h"
#include "Tuning.h"
#include "TrendPlot.h"

#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_item.h>
#include <qwt_plot_marker.h>
#include <qwt_symbol.h>
#include <qwt_text.h>

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSignalSpy>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtTest>

#include <cmath>
#include <functional>
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
        makeTag(kOther, QStringLiteral("Other"), QString(), QStringLiteral("#00aa00")),
    };
}

/// FOPDT: CO steps 20 -> 30 at coStep; PV = 50 + 10 * (1 - exp(-(t - 11.5) / 4)) after 11.5
/// (so with coStep = 10: K = 1, L = 1.5, T = 4). "Other" follows the PV (2 * pv - 40).
void appendRows(Recorder &r, int fromIndex, int toIndex, double coStep = 10.0)
{
    for (int i = fromIndex; i <= toIndex; ++i) {
        const double t = i * 0.1;
        const double co = t >= coStep ? 30.0 : 20.0;
        const double pv = t < 11.5 ? 50.0 : 50.0 + 10.0 * (1.0 - std::exp(-(t - 11.5) / 4.0));
        SampleBatch b;
        b.t = t;
        b.samples = {good(kCo, co), good(kPv, pv), good(kOther, 2.0 * pv - 40.0)};
        r.append(b);
    }
}

template<class T>
T *child(QWidget &w, const char *name)
{
    return w.findChild<T *>(QLatin1String(name));
}

QTableWidget *table(QWidget &w, const char *name)
{
    return child<QTableWidget>(w, name);
}

QString g4(double v)
{
    return QString::number(v, 'g', 4);
}

/// Recorder + plot + panel wired together; the panel is configured for the FOPDT data (PV chosen,
/// CO tag as dCO, cursors at 9 s and 40 s) unless `configure` is false.
struct Fx {
    Recorder rec;
    TrendPlot plot;
    AnalysisPanel panel;

    explicit Fx(bool configure = true, const QList<Tag> &tags = standardTags(), double coStep = 10.0)
    {
        appendRows(rec, 0, 600, coStep);
        plot.setRecorder(&rec);
        plot.setTags(tags);
        plot.refresh();
        panel.setRecorder(&rec);
        panel.setPlot(&plot);
        panel.setTags(tags);
        if (configure) {
            AnalysisSettings s;
            s.pvTagId = kPv;
            s.setpointTagIds = {kPv};
            s.deltaCoTagId = kCo;
            panel.setSettings(s);
            plot.cursors()->setC1(9.0);
            plot.cursors()->setC2(40.0);
        }
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
};

int symbolMarkerCount(TrendPlot &plot)
{
    int n = 0;
    const QwtPlotItemList items = plot.plot()->itemList(QwtPlotItem::Rtti_PlotMarker);
    for (QwtPlotItem *item : items) {
        if (static_cast<QwtPlotMarker *>(item)->symbol())
            ++n;
    }
    return n;
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

QwtPlotCurve *findCurve(TrendPlot &plot, const QString &title)
{
    const QwtPlotItemList items = plot.plot()->itemList(QwtPlotItem::Rtti_PlotCurve);
    for (QwtPlotItem *item : items) {
        if (item->title().text() == title)
            return static_cast<QwtPlotCurve *>(item);
    }
    return nullptr;
}

QwtPlotItemList guideItems(TrendPlot &plot)
{
    return plot.plot()->itemList(QwtPlotItem::Rtti_PlotUserItem + 1);
}

QStringList checkTexts(QWidget &panel)
{
    QStringList out;
    auto *list = child<QListWidget>(panel, "checksList");
    for (int i = 0; i < list->count(); ++i)
        out << list->item(i)->text();
    return out;
}

bool anyContains(const QStringList &list, const QString &part)
{
    for (const QString &s : list) {
        if (s.contains(part))
            return true;
    }
    return false;
}

QString cell(QTableWidget *t, int row, int col)
{
    return t->item(row, col) ? t->item(row, col)->text() : QString();
}

int rowOf(QTableWidget *t, const QString &label)
{
    for (int r = 0; r < t->rowCount(); ++r) {
        if (cell(t, r, 0) == label)
            return r;
    }
    return -1;
}

void selectData(QComboBox *combo, TagId id)
{
    const int idx = combo->findData(id);
    QVERIFY2(idx >= 0, "tag not in combo");
    combo->setCurrentIndex(idx);
}

} // namespace

class TstAnalysisPanel : public QObject
{
    Q_OBJECT
private slots:
    void constructs();
    void pvCoSelection();
    void staleTagsMeanNone();
    void methodCombo();
    void resultsTable();
    void percentRowsWithRanges();
    void checksList();
    void tuningRules();
    void tuningMinutesLambda();
    void tuningNegativeGainAndEmpty();
    void tooltipsResults();
    void tooltipsTuningAndSetpoints();
    void findStep();
    void findStepNoStep();
    void findStepNotSettledCheck();
    void noDeltaCoWording();
    void hintLine();
    void overlays();
    void overlaysRemovedWithPlot();
    void plotDestroyedFirst();
    void detailedLabels();
    void guideLinesAndAxis();
    void readoutText();
    void settingsChangedOnUserEditsOnly();
    void settingsRoundTrip();
    void addRemovePercent();
    void loadedPercentsNormalised();
    void modelPercentRevert();
    void rangesEdit();
    void setpointTable();
    void copyResults();
    void collapsedGroup();
    void refreshRecompute();
    void evictionRecomputes();
    void manyRecomputes();
};

void TstAnalysisPanel::constructs()
{
    AnalysisPanel panel;
    QVERIFY(panel.results().isEmpty());
    QVERIFY(panel.settings().showModel);
    QVERIFY(panel.readoutText().isEmpty());
    QVERIFY(!child<QPushButton>(panel, "copyResultsButton")->isEnabled());
    QVERIFY(!child<QPushButton>(panel, "exportButton")->isEnabled());
    QVERIFY(!child<QPushButton>(panel, "findStepButton")->isEnabled());
}

void TstAnalysisPanel::pvCoSelection()
{
    Fx fx(false);
    auto *pv = child<QComboBox>(fx.panel, "pvCombo");
    auto *co = child<QComboBox>(fx.panel, "coCombo");
    auto *tags = child<QListWidget>(fx.panel, "setpointTagList");
    auto *spin = child<QDoubleSpinBox>(fx.panel, "deltaCoSpin");
    QCOMPARE(pv->count(), 4);
    QCOMPARE(pv->itemText(0), QStringLiteral("Choose..."));
    QCOMPARE(pv->itemText(2), QStringLiteral("PV [degC]"));
    QCOMPARE(pv->itemText(3), QStringLiteral("Other"));
    QCOMPARE(co->count(), 4);
    QCOMPARE(co->itemText(0), QStringLiteral(u"Typed ΔCO"));
    QVERIFY(spin->isEnabled());

    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    selectData(pv, kPv);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(fx.panel.settings().pvTagId, kPv);
    QVERIFY(fx.panel.settings().setpointTagIds.contains(kPv));
    QCOMPARE(co->count(), 3); // the PV is not offered as CO
    QCOMPARE(co->findData(kPv), -1);
    // The list shows every plotted tag, the PV checked.
    QCOMPARE(tags->count(), 3);
    for (int i = 0; i < tags->count(); ++i) {
        const bool isPv = tags->item(i)->data(Qt::UserRole).toUInt() == kPv;
        QVERIFY((tags->item(i)->checkState() == Qt::Checked) == isPv);
    }

    selectData(co, kCo);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(fx.panel.settings().deltaCoTagId, kCo);
    QVERIFY(!spin->isEnabled());

    // Choosing the CO tag as PV clears the CO.
    selectData(pv, kCo);
    QCOMPARE(fx.panel.settings().pvTagId, kCo);
    QCOMPARE(fx.panel.settings().deltaCoTagId, TagId(0));
    QCOMPARE(co->currentIndex(), 0);
    QVERIFY(spin->isEnabled());
    QVERIFY(fx.panel.settings().setpointTagIds.contains(kPv));
    QVERIFY(fx.panel.settings().setpointTagIds.contains(kCo));

    // The list mirrors setpointTagIds in both directions.
    pv->setCurrentIndex(0);
    QCOMPARE(fx.panel.settings().pvTagId, TagId(0));
    for (int i = 0; i < tags->count(); ++i) {
        if (tags->item(i)->data(Qt::UserRole).toUInt() == kOther)
            tags->item(i)->setCheckState(Qt::Checked);
    }
    QVERIFY(fx.panel.settings().setpointTagIds.contains(kOther));
    for (int i = 0; i < tags->count(); ++i) {
        if (tags->item(i)->data(Qt::UserRole).toUInt() == kOther)
            tags->item(i)->setCheckState(Qt::Unchecked);
    }
    QVERIFY(!fx.panel.settings().setpointTagIds.contains(kOther));
}

void TstAnalysisPanel::staleTagsMeanNone()
{
    Fx fx(false);
    AnalysisSettings s;
    s.pvTagId = 99; // not in the project
    s.deltaCoTagId = 98;
    s.setpointTagIds = {kPv, 97};
    fx.panel.setSettings(s);
    QCOMPARE(fx.panel.settings().pvTagId, TagId(0));
    QCOMPARE(fx.panel.settings().deltaCoTagId, TagId(0));
    QVERIFY(fx.panel.settings().setpointTagIds.contains(97)); // unknown ids are kept

    fx.apply([](AnalysisSettings &a) {
        a.pvTagId = kPv;
        a.deltaCoTagId = kCo;
    });
    QList<Tag> tags = standardTags();
    tags.removeAt(0); // drop CO
    fx.panel.setTags(tags);
    QCOMPARE(fx.panel.settings().deltaCoTagId, TagId(0));
    QCOMPARE(fx.panel.settings().pvTagId, kPv);
    QCOMPARE(child<QComboBox>(fx.panel, "coCombo")->currentIndex(), 0);
    tags.removeAt(0); // drop PV
    fx.panel.setTags(tags);
    QCOMPARE(fx.panel.settings().pvTagId, TagId(0));
    QCOMPARE(child<QComboBox>(fx.panel, "pvCombo")->currentIndex(), 0);
}

void TstAnalysisPanel::methodCombo()
{
    Fx fx;
    auto *method = child<QComboBox>(fx.panel, "methodCombo");
    auto *from = child<QDoubleSpinBox>(fx.panel, "fromSpin");
    auto *to = child<QDoubleSpinBox>(fx.panel, "toSpin");
    QCOMPARE(method->count(), 4);
    QCOMPARE(method->itemText(1), toString(ModelMethod::Smith));
    QVERIFY(!from->isEnabled());
    QVERIFY(!to->isEnabled());

    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    method->setCurrentIndex(1);
    QCOMPARE(changed.count(), 1);
    QVERIFY(fx.panel.settings().method == ModelMethod::Smith);
    QCOMPARE(from->value(), 28.3);
    QCOMPARE(to->value(), 63.2);
    QVERIFY(!from->isEnabled());
    const StepResult smith = fx.panel.results().at(0);
    QVERIFY(smith.timeConstant && smith.deadTime);
    QVERIFY(std::abs(*smith.timeConstant - 4.0) < 0.3);
    QVERIFY(std::abs(*smith.deadTime - 1.5) < 0.3);

    method->setCurrentIndex(2);
    QVERIFY(fx.panel.settings().method == ModelMethod::Sundaresan);
    QCOMPARE(from->value(), 35.3);
    QCOMPARE(to->value(), 85.3);
    QVERIFY(std::abs(*fx.panel.results().at(0).timeConstant - 4.0) < 0.4);

    method->setCurrentIndex(3);
    QVERIFY(from->isEnabled());
    QVERIFY(to->isEnabled());
    QVERIFY(fx.panel.settings().method == ModelMethod::Custom);
    from->setValue(10.0);
    QCOMPARE(fx.panel.settings().modelPercentA, 10.0);
    to->setValue(80.0);
    QCOMPARE(fx.panel.settings().modelPercentB, 80.0);
    const StepResult custom = fx.panel.results().at(0);
    QVERIFY(custom.timeConstant);
    QVERIFY(std::abs(*custom.timeConstant - 4.0) < 0.4);
}

void TstAnalysisPanel::resultsTable()
{
    Fx fx;
    QTableWidget *t = table(fx.panel, "resultsTable");
    QVERIFY(t);
    QCOMPARE(t->columnCount(), 2);
    QCOMPARE(t->horizontalHeaderItem(0)->text(), QStringLiteral("Quantity"));
    QCOMPARE(t->horizontalHeaderItem(1)->text(), QStringLiteral("Value"));
    QVERIFY(!t->verticalHeader()->isVisible());
    const QStringList labels = {QStringLiteral("y0"),   QStringLiteral("y100"),
                                QStringLiteral(u"ΔPV"), QStringLiteral(u"ΔCO"),
                                QStringLiteral("K"),    QStringLiteral("L"),
                                QStringLiteral("T"),    QStringLiteral("L/T"),
                                QStringLiteral("Fit error")};
    QCOMPARE(t->rowCount(), int(labels.size()));
    for (int i = 0; i < labels.size(); ++i)
        QCOMPARE(cell(t, i, 0), labels.at(i));

    const StepResult r = fx.panel.results().at(0);
    QVERIFY(r.valid && r.gain && r.deadTime && r.timeConstant && r.deltaCo);
    QCOMPARE(cell(t, 0, 1), g4(r.y0) + QStringLiteral(" degC"));
    QCOMPARE(cell(t, 1, 1), g4(r.y100) + QStringLiteral(" degC"));
    QCOMPARE(cell(t, 2, 1), g4(r.deltaPv) + QStringLiteral(" degC"));
    QCOMPARE(cell(t, 3, 1), g4(*r.deltaCo) + QStringLiteral(" %"));
    QCOMPARE(cell(t, 4, 1), g4(*r.gain) + QStringLiteral(" degC/%"));
    QCOMPARE(cell(t, 5, 1), g4(*r.deadTime) + QStringLiteral(" s"));
    QCOMPARE(cell(t, 6, 1), g4(*r.timeConstant) + QStringLiteral(" s"));
    QCOMPARE(cell(t, 7, 1), g4(*r.deadTime / *r.timeConstant));
    const QString fit = cell(t, 8, 1);
    QVERIFY2(QRegularExpression(QStringLiteral(u"^[0-9.e+-]+ degC \\([0-9.e+-]+ %\\) — Good$"))
                 .match(fit).hasMatch(),
             qPrintable(fit));

    // Typed dCO: no unit; no dCO at all: K and dCO missing.
    fx.apply([](AnalysisSettings &a) {
        a.deltaCoTagId = 0;
        a.manualDeltaCo = 10.0;
    });
    QCOMPARE(cell(t, 3, 1), QStringLiteral("10"));
    QCOMPARE(cell(t, 4, 1), g4(*fx.panel.results().at(0).gain));
    fx.apply([](AnalysisSettings &a) { a.manualDeltaCo = 0.0; });
    QCOMPARE(cell(t, 3, 1), QStringLiteral(u"—"));
    QCOMPARE(cell(t, 4, 1), QStringLiteral(u"—"));
    QVERIFY(cell(t, 5, 1) != QStringLiteral(u"—"));

    // No valid result: dashes.
    fx.plot.cursors()->clear();
    QCOMPARE(cell(t, 0, 1), QStringLiteral(u"—"));
    QCOMPARE(cell(t, 8, 1), QStringLiteral(u"—"));
    QVERIFY(fx.panel.results().isEmpty());
}

void TstAnalysisPanel::percentRowsWithRanges()
{
    Fx fx;
    QTableWidget *t = table(fx.panel, "resultsTable");
    QTableWidget *tt = table(fx.panel, "tuningTable");
    QCOMPARE(rowOf(t, QStringLiteral("K (%/%)")), -1);
    QCOMPARE(tt->columnCount(), 3);

    auto *pvMin = child<QLineEdit>(fx.panel, "pvMinEdit");
    auto *pvMax = child<QLineEdit>(fx.panel, "pvMaxEdit");
    QVERIFY(pvMin && pvMax);
    pvMin->setText(QStringLiteral("0"));
    QCOMPARE(rowOf(t, QStringLiteral("K (%/%)")), -1); // only one value
    pvMax->setText(QStringLiteral("200"));
    const int row = rowOf(t, QStringLiteral("K (%/%)"));
    QVERIFY(row >= 0);
    const StepResult r = fx.panel.results().at(0);
    QCOMPARE(cell(t, row, 1), g4(*r.gain * 100.0 / 200.0) + QStringLiteral(" %/%"));
    QCOMPARE(tt->columnCount(), 4);
    QCOMPARE(tt->horizontalHeaderItem(1)->text(), QStringLiteral("Kp (%/%)"));
    const double kp = *tune(*r.gain, *r.deadTime, *r.timeConstant, TuningRule::ZieglerNichols).at(0).kp;
    QCOMPARE(cell(tt, 0, 1), g4(kp * 200.0 / 100.0));
}

void TstAnalysisPanel::checksList()
{
    // Everything fine: no checks, list hidden.
    {
        Fx fx;
        fx.panel.show();
        auto *list = child<QListWidget>(fx.panel, "checksList");
        QVERIFY2(list->count() == 0, qPrintable(checkTexts(fx.panel).join('|')));
        QVERIFY(!list->isVisible());
    }
    // No dCO: K can't be computed.
    {
        Fx fx;
        fx.apply([](AnalysisSettings &a) {
            a.deltaCoTagId = 0;
            a.manualDeltaCo = 0.0;
        });
        QVERIFY(anyContains(checkTexts(fx.panel),
                            QStringLiteral(u"K can't be computed: choose the controller output (CO) "
                                           u"or type ΔCO in step 1.")));
        QVERIFY(!child<QListWidget>(fx.panel, "checksList")->isHidden());
    }
    // C2 too early: the PV is still moving.
    {
        Fx fx;
        fx.setCursors(9.0, 15.0);
        QVERIFY(anyContains(checkTexts(fx.panel),
                            QStringLiteral("The PV is still moving at C2: move C2 later.")));
    }
    // PV reacts before the CO step: negative dead time.
    {
        Fx fx(true, standardTags(), 20.0);
        fx.setCursors(5.0, 40.0);
        const StepResult r = fx.panel.results().at(0);
        QVERIFY(r.deadTime && *r.deadTime < 0.0);
        QVERIFY(anyContains(checkTexts(fx.panel),
                            QStringLiteral("L is negative: C1 is after the start of the response. "
                                           "Move C1 before the step.")));
    }
    // The model points are not reached (C2 just after the start of the response).
    {
        Fx fx;
        fx.apply([](AnalysisSettings &a) { a.method = ModelMethod::Sundaresan; });
        fx.setCursors(9.0, 12.0);
        const StepResult r = fx.panel.results().at(0);
        if (!r.deadTime) {
            QVERIFY(anyContains(checkTexts(fx.panel),
                                QStringLiteral("L and T can't be computed: the PV does not reach both "
                                               "model points (35.3 % and 85.3 %) between the cursors.")));
        }
    }
    // Invalid result: no checks, the error is the hint.
    {
        Fx fx;
        fx.setCursors(9.0, 9.0);
        QCOMPARE(child<QListWidget>(fx.panel, "checksList")->count(), 0);
    }
}

void TstAnalysisPanel::tuningRules()
{
    Fx fx;
    auto *rule = child<QComboBox>(fx.panel, "ruleCombo");
    QTableWidget *t = table(fx.panel, "tuningTable");
    QCOMPARE(rule->count(), 4);
    const StepResult r = fx.panel.results().at(0);
    const double K = *r.gain;
    const double L = *r.deadTime;
    const double T = *r.timeConstant;

    const TuningRule rules[] = {TuningRule::ZieglerNichols, TuningRule::CohenCoon,
                                TuningRule::ImcLambda, TuningRule::Simc};
    for (int i = 0; i < 4; ++i) {
        rule->setCurrentIndex(i);
        QVERIFY(fx.panel.settings().tuningRule == rules[i]);
        QCOMPARE(rule->itemText(i), toString(rules[i]));
        const QList<TuningRow> expected = tune(K, L, T, rules[i], 0.0);
        QCOMPARE(t->rowCount(), 3);
        QCOMPARE(t->verticalHeaderItem(0)->text(), QStringLiteral("P"));
        QCOMPARE(t->verticalHeaderItem(1)->text(), QStringLiteral("PI"));
        QCOMPARE(t->verticalHeaderItem(2)->text(), QStringLiteral("PID"));
        for (int row = 0; row < 3; ++row) {
            const TuningRow &e = expected.at(row);
            if (!e.available) {
                QCOMPARE(cell(t, row, 0), QStringLiteral("not defined"));
                QCOMPARE(t->columnSpan(row, 0), t->columnCount());
                continue;
            }
            QCOMPARE(cell(t, row, 0), g4(*e.kp));
            QCOMPARE(cell(t, row, 1), e.ti ? g4(*e.ti) : QStringLiteral(u"—"));
            QCOMPARE(cell(t, row, 2), e.td ? g4(*e.td) : QStringLiteral(u"—"));
        }
    }
    // The "not defined" rows of IMC / lambda (P) and SIMC (PID).
    rule->setCurrentIndex(2);
    QCOMPARE(cell(t, 0, 0), QStringLiteral("not defined"));
    QVERIFY(!t->item(0, 0)->toolTip().isEmpty());
    rule->setCurrentIndex(3);
    QCOMPARE(cell(t, 2, 0), QStringLiteral("not defined"));
    QVERIFY(cell(t, 1, 0) != QStringLiteral("not defined"));
}

void TstAnalysisPanel::tuningMinutesLambda()
{
    Fx fx;
    auto *rule = child<QComboBox>(fx.panel, "ruleCombo");
    auto *lambda = child<QDoubleSpinBox>(fx.panel, "lambdaSpin");
    auto *minutes = child<QCheckBox>(fx.panel, "minutesCheck");
    auto *note = child<QLabel>(fx.panel, "tuningNote");
    QTableWidget *t = table(fx.panel, "tuningTable");
    const StepResult r = fx.panel.results().at(0);
    const double K = *r.gain;
    const double L = *r.deadTime;
    const double T = *r.timeConstant;

    // lambda is only used by IMC / lambda and SIMC; 0 shows "Auto (= L)".
    QVERIFY(!lambda->isEnabled());
    QCOMPARE(lambda->specialValueText(), QStringLiteral("Auto (= L)"));
    rule->setCurrentIndex(2);
    QVERIFY(lambda->isEnabled());
    rule->setCurrentIndex(3);
    QVERIFY(lambda->isEnabled());
    rule->setCurrentIndex(0);
    QVERIFY(!lambda->isEnabled());

    // Seconds / minutes.
    QCOMPARE(t->horizontalHeaderItem(1)->text(), QStringLiteral("Ti (s)"));
    QVERIFY(note->text().startsWith(
        QStringLiteral(u"Ideal (ISA) form: u = Kp (e + 1/Ti ∫e dt + Td de/dt). Ti and Td in s.")));
    const double ti = *tune(K, L, T, TuningRule::ZieglerNichols).at(1).ti;
    QCOMPARE(cell(t, 1, 1), g4(ti));
    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    minutes->setChecked(true);
    QCOMPARE(changed.count(), 1);
    QVERIFY(fx.panel.settings().timeInMinutes);
    QCOMPARE(cell(t, 1, 1), g4(ti / 60.0));
    QCOMPARE(t->horizontalHeaderItem(1)->text(), QStringLiteral("Ti (min)"));
    QVERIFY(note->text().endsWith(QStringLiteral("Ti and Td in min.")));
    QVERIFY(t->item(1, 1)->toolTip().contains(QStringLiteral("min")));

    // lambda Auto (= L) and an explicit lambda.
    rule->setCurrentIndex(2);
    QCOMPARE(cell(t, 1, 0), g4(*tune(K, L, T, TuningRule::ImcLambda, 0.0).at(1).kp));
    QVERIFY(t->item(1, 0)->toolTip().contains(QStringLiteral("(Auto)")));
    lambda->setValue(2.0);
    QCOMPARE(fx.panel.settings().lambdaS, 2.0);
    QCOMPARE(cell(t, 1, 0), g4(*tune(K, L, T, TuningRule::ImcLambda, 2.0).at(1).kp));
    QCOMPARE(cell(t, 1, 0), g4(T / (K * (2.0 + L))));
    lambda->setValue(0.0);
    QCOMPARE(cell(t, 1, 0), g4(T / (K * (L + L))));
}

void TstAnalysisPanel::tuningNegativeGainAndEmpty()
{
    // Negative K: negative Kp and the direct-acting note.
    {
        Fx fx;
        fx.apply([](AnalysisSettings &a) {
            a.deltaCoTagId = 0;
            a.manualDeltaCo = -10.0;
        });
        const StepResult r = fx.panel.results().at(0);
        QVERIFY(r.gain && *r.gain < 0.0);
        QTableWidget *t = table(fx.panel, "tuningTable");
        QCOMPARE(t->rowCount(), 3);
        QVERIFY(cell(t, 0, 0).startsWith(u'-'));
        auto *note = child<QLabel>(fx.panel, "tuningNote");
        QVERIFY(note->text().contains(
            QStringLiteral("K is negative: the controller must be direct acting (Kp is shown negative).")));
    }
    // L <= 0 or no K / L / T: no rows.
    {
        Fx fx(true, standardTags(), 20.0);
        fx.setCursors(5.0, 40.0);
        QVERIFY(*fx.panel.results().at(0).deadTime < 0.0);
        QCOMPARE(table(fx.panel, "tuningTable")->rowCount(), 0);
    }
    {
        Fx fx;
        fx.apply([](AnalysisSettings &a) {
            a.deltaCoTagId = 0;
            a.manualDeltaCo = 0.0;
        });
        QCOMPARE(table(fx.panel, "tuningTable")->rowCount(), 0);
        QVERIFY(!child<QLabel>(fx.panel, "tuningNote")->text().contains(QStringLiteral("negative")));
    }
}

void TstAnalysisPanel::tooltipsResults()
{
    Fx fx;
    QTableWidget *t = table(fx.panel, "resultsTable");
    const StepResult r = fx.panel.results().at(0);
    const QString dPv = g4(r.deltaPv);
    const QString dCo = g4(*r.deltaCo);
    const QString k = g4(*r.gain);

    const QString kTip = t->item(rowOf(t, QStringLiteral("K")), 1)->toolTip();
    QVERIFY2(kTip.contains(QStringLiteral(u"K, process gain")), qPrintable(kTip));
    QVERIFY(kTip.contains(QStringLiteral(u"K = ΔPV / ΔCO")));
    QVERIFY2(kTip.contains(QStringLiteral("K = %1 / %2 = %3 degC/%").arg(dPv, dCo, k)), qPrintable(kTip));
    // The label cell has the same tooltip.
    QCOMPARE(t->item(rowOf(t, QStringLiteral("K")), 0)->toolTip(), kTip);

    const QString dPvTip = t->item(2, 1)->toolTip();
    QVERIFY(dPvTip.contains(QStringLiteral(u"ΔPV = y100 - y0")));
    QVERIFY(dPvTip.contains(QStringLiteral(u"ΔPV = %1 - %2 = %3 degC").arg(g4(r.y100), g4(r.y0), dPv)));

    const QString y0Tip = t->item(0, 1)->toolTip();
    QVERIFY(y0Tip.contains(QStringLiteral("y0 = PV(C1)")));
    QVERIFY(y0Tip.contains(QStringLiteral("9.000 s")));
    const QString dCoTip = t->item(3, 1)->toolTip();
    QVERIFY(dCoTip.contains(QStringLiteral(u"ΔCO = CO(C2) - CO(C1)")));
    QVERIFY(dCoTip.contains(QStringLiteral(u"ΔCO = 30 - 20 = 10 %")));

    // L and T with the 0 % / 63.2 % method.
    const double L = *r.deadTime;
    const double T = *r.timeConstant;
    const QString tTip = t->item(rowOf(t, QStringLiteral("T")), 1)->toolTip();
    QVERIFY2(tTip.contains(QStringLiteral("T = t63.2 - t0")), qPrintable(tTip));
    QVERIFY(tTip.contains(QStringLiteral("= %1 s").arg(g4(T))));
    QVERIFY(tTip.contains(QStringLiteral("measured from the step time")));
    const QString lTip = t->item(rowOf(t, QStringLiteral("L")), 1)->toolTip();
    QVERIFY(lTip.contains(QStringLiteral("L = t0")));
    QVERIFY(lTip.contains(QStringLiteral("L = %1 s").arg(g4(L))));

    // Smith: the textbook formula with the crossing times.
    child<QComboBox>(fx.panel, "methodCombo")->setCurrentIndex(1);
    const StepResult s = fx.panel.results().at(0);
    const double Ts = *s.timeConstant;
    const double Ls = *s.deadTime;
    const double ta = Ls - Ts * std::log(1.0 - 0.283);
    const double tb = ta + Ts * (std::log(1.0 - 0.283) - std::log(1.0 - 0.632));
    const QString sT = t->item(rowOf(t, QStringLiteral("T")), 1)->toolTip();
    QVERIFY2(sT.contains(QStringLiteral("T = 1.5 (t63.2 - t28.3)")), qPrintable(sT));
    // Rounded textbook constants: "≈", like Sundaresan.
    QVERIFY2(sT.contains(QStringLiteral(u"T = 1.5 (%1 - %2) ≈ %3 s").arg(g4(tb), g4(ta), g4(Ts))),
             qPrintable(sT));
    const QString sL = t->item(rowOf(t, QStringLiteral("L")), 1)->toolTip();
    QVERIFY2(sL.contains(QStringLiteral("L = t63.2 - T")), qPrintable(sL));
    QVERIFY2(sL.contains(QStringLiteral(u"L = %1 - %2 ≈ %3 s").arg(g4(tb), g4(Ts), g4(Ls))),
             qPrintable(sL));

    // Custom: the generic two-point formula.
    child<QComboBox>(fx.panel, "methodCombo")->setCurrentIndex(3);
    const QString cT = t->item(rowOf(t, QStringLiteral("T")), 1)->toolTip();
    QVERIFY(cT.contains(QStringLiteral("(ln(1 - a) - ln(1 - b))")));
    QVERIFY(t->item(rowOf(t, QStringLiteral("L")), 1)->toolTip().contains(QStringLiteral("T ln(1 - a)")));

    // L/T and the fit.
    const QString ltTip = t->item(rowOf(t, QStringLiteral("L/T")), 1)->toolTip();
    QVERIFY(ltTip.contains(QStringLiteral("below 0.3 easy, above 1 dead-time dominated")));
    const QString fitTip = t->item(rowOf(t, QStringLiteral("Fit error")), 1)->toolTip();
    QVERIFY(fitTip.contains(QStringLiteral("Under 3 % is Good")));
    QVERIFY(fitTip.contains(g4(*fx.panel.results().at(0).fitRms)));

    // User text is escaped.
    QList<Tag> tags = standardTags();
    tags[1].unit = QStringLiteral("<b>&");
    fx.plot.setTags(tags);
    fx.panel.setTags(tags);
    const QString esc = t->item(0, 1)->toolTip();
    QVERIFY2(esc.contains(QStringLiteral("&lt;b&gt;&amp;")), qPrintable(esc));
    QVERIFY(!esc.contains(QStringLiteral("<b>&")));
}

void TstAnalysisPanel::tooltipsTuningAndSetpoints()
{
    Fx fx;
    QTableWidget *t = table(fx.panel, "tuningTable");
    const StepResult r = fx.panel.results().at(0);
    const double K = *r.gain;
    const double L = *r.deadTime;
    const double T = *r.timeConstant;
    const TuningRow pid = tune(K, L, T, TuningRule::ZieglerNichols).at(2);

    const QString kpTip = t->item(2, 0)->toolTip();
    QVERIFY2(kpTip.contains(QStringLiteral(u"Kp = 1.2 × T / (K × L)")), qPrintable(kpTip));
    QVERIFY2(kpTip.contains(QStringLiteral(u"Kp = 1.2 × %1 / (%2 × %3) = %4 %/degC")
                                .arg(g4(T), g4(K), g4(L), g4(*pid.kp))),
             qPrintable(kpTip));
    const QString tiTip = t->item(2, 1)->toolTip();
    QVERIFY(tiTip.contains(QStringLiteral(u"Ti = 2 × L")));
    QVERIFY(tiTip.contains(QStringLiteral(u"Ti = 2 × %1 = %2 s").arg(g4(L), g4(*pid.ti))));
    QVERIFY(t->item(2, 2)->toolTip().contains(QStringLiteral(u"Td = 0.5 × L")));

    child<QComboBox>(fx.panel, "ruleCombo")->setCurrentIndex(1); // Cohen-Coon uses r = L / T
    const QString cc = t->item(1, 1)->toolTip();
    QVERIFY(cc.contains(QStringLiteral("r = L / T")));
    QVERIFY(cc.contains(QStringLiteral("r = L / T = %1 / %2 = %3").arg(g4(L), g4(T), g4(L / T))));

    // Kp (%/%)
    child<QComboBox>(fx.panel, "ruleCombo")->setCurrentIndex(0);
    fx.apply([](AnalysisSettings &a) {
        a.pvRangeMin = 0.0;
        a.pvRangeMax = 200.0;
    });
    QCOMPARE(t->columnCount(), 4);
    const QString pct = t->item(0, 1)->toolTip();
    QVERIFY(pct.contains(QStringLiteral(u"Kp (%/%) = Kp × PV span / CO span")));
    const double kp = *tune(K, L, T, TuningRule::ZieglerNichols).at(0).kp;
    QCOMPARE(cell(t, 0, 1), g4(kp * 200.0 / 100.0));

    // Setpoint table cells.
    QTableWidget *sp = table(fx.panel, "setpointTable");
    QVERIFY(sp->rowCount() >= 2);
    const QString target = sp->item(1, 2)->toolTip();
    QVERIFY(target.contains(QStringLiteral("target = y0 + p (y100 - y0)")));
    QVERIFY(target.contains(g4(r.setpoints.at(1).target)));
    const QString time = sp->item(1, 4)->toolTip();
    QVERIFY(time.contains(QStringLiteral("time at which the PV reaches y0 + p (y100 - y0)")));
    QVERIFY(time.contains(g4(r.setpoints.at(1).target)));
}

void TstAnalysisPanel::findStep()
{
    Fx fx(false);
    auto *button = child<QPushButton>(fx.panel, "findStepButton");
    auto *hint = child<QLabel>(fx.panel, "hintLabel");
    QVERIFY(!button->isEnabled());
    QVERIFY(button->toolTip().contains(QStringLiteral("process value")));
    fx.apply([](AnalysisSettings &a) { a.pvTagId = kPv; });
    QVERIFY(!button->isEnabled());
    QVERIFY(button->toolTip().contains(QStringLiteral("controller output")));
    fx.apply([](AnalysisSettings &a) { a.deltaCoTagId = kCo; });
    QVERIFY(button->isEnabled());
    QVERIFY(!fx.plot.cursors()->hasC1());
    QVERIFY(hint->text().startsWith(QStringLiteral("Place C1 just before the step")));

    QSignalSpy results(&fx.panel, &AnalysisPanel::resultsChanged);
    button->click();
    QVERIFY(fx.plot.cursors()->hasC1() && fx.plot.cursors()->hasC2());
    QCOMPARE(results.count(), 1); // recomputed once
    const double c1 = fx.plot.cursors()->c1();
    const double c2 = fx.plot.cursors()->c2();
    QVERIFY2(c1 > 1.0 && c1 < 10.0, qPrintable(QString::number(c1)));
    QVERIFY2(c2 > 15.0, qPrintable(QString::number(c2)));
    QCOMPARE(fx.panel.results().size(), 1);
    QVERIFY(fx.panel.results().at(0).valid);
    QVERIFY(hint->text().isEmpty());
    QVERIFY(child<QLabel>(fx.panel, "cursorLabel")->text().startsWith(QStringLiteral("C1 = ")));
}

void TstAnalysisPanel::findStepNoStep()
{
    Recorder rec;
    for (int i = 0; i <= 300; ++i) {
        SampleBatch b;
        b.t = i * 0.1;
        b.samples = {good(kCo, 20.0), good(kPv, 50.0 + 0.001 * i), good(kOther, 1.0)};
        rec.append(b);
    }
    TrendPlot plot;
    plot.setRecorder(&rec);
    plot.setTags(standardTags());
    AnalysisPanel panel;
    panel.setRecorder(&rec);
    panel.setPlot(&plot);
    panel.setTags(standardTags());
    AnalysisSettings s;
    s.pvTagId = kPv;
    s.deltaCoTagId = kCo;
    panel.setSettings(s);
    auto *button = child<QPushButton>(panel, "findStepButton");
    QVERIFY(button->isEnabled());
    button->click();
    QVERIFY(!plot.cursors()->hasC1());
    QCOMPARE(child<QLabel>(panel, "hintLabel")->text(),
             QStringLiteral("No step found in the controller output in the visible range."));
}

void TstAnalysisPanel::findStepNotSettledCheck()
{
    // CO step at 10 s; the PV follows a first-order response with a +-0.5 alternating ripple (5 %
    // of the 10 degC step, above the 2 % settling band), so findStep() never sees it settle, while
    // the least-squares slope at C2 stays tiny: checkStep() alone does not report NotSettled.
    Recorder rec;
    for (int i = 0; i <= 600; ++i) {
        const double t = i * 0.1;
        const double pv = (t < 11.5 ? 50.0 : 50.0 + 10.0 * (1.0 - std::exp(-(t - 11.5) / 4.0)))
                          + ((i % 2) ? 0.5 : -0.5);
        SampleBatch b;
        b.t = t;
        b.samples = {good(kCo, t >= 10.0 ? 30.0 : 20.0), good(kPv, pv), good(kOther, 1.0)};
        rec.append(b);
    }
    const auto window = mb::findStep(rec, kCo, kPv, 0.0, 60.0);
    QVERIFY(window.has_value());
    QVERIFY(!window->settled);

    TrendPlot plot;
    plot.setRecorder(&rec);
    plot.setTags(standardTags());
    AnalysisPanel panel;
    panel.setRecorder(&rec);
    panel.setPlot(&plot);
    panel.setTags(standardTags());
    AnalysisSettings s;
    s.pvTagId = kPv;
    s.setpointTagIds = {kPv};
    s.deltaCoTagId = kCo;
    panel.setSettings(s);
    const QString notSettled = QStringLiteral("The PV is still moving at C2: move C2 later.");

    child<QPushButton>(panel, "findStepButton")->click();
    QVERIFY(plot.cursors()->hasC1() && plot.cursors()->hasC2());
    QVERIFY(panel.results().first().valid);
    QVERIFY2(!checkStep(rec, panel.results().first()).contains(StepCheck::NotSettled),
             "the slope test must not fire for this test to mean anything");
    QVERIFY2(anyContains(checkTexts(panel), notSettled), qPrintable(checkTexts(panel).join('|')));
    QVERIFY(!child<QListWidget>(panel, "checksList")->isHidden());

    // A cursor moved by the user (not by Find step) drops the remembered verdict.
    plot.cursors()->setC2(plot.cursors()->c2() - 1.0);
    QVERIFY(!anyContains(checkTexts(panel), notSettled));

    // Find step again brings it back; changing the CO tag clears it.
    child<QPushButton>(panel, "findStepButton")->click();
    QVERIFY(anyContains(checkTexts(panel), notSettled));
    AnalysisSettings typed = panel.settings();
    typed.deltaCoTagId = 0;
    typed.manualDeltaCo = 10.0;
    panel.setSettings(typed);
    QVERIFY(!anyContains(checkTexts(panel), notSettled));
}

void TstAnalysisPanel::noDeltaCoWording()
{
    const QString tagText = QStringLiteral("The controller output (CO) does not change between the "
                                           "cursors: K can't be computed. Move the cursors around "
                                           "the step.");
    const QString typedText = QStringLiteral(u"K can't be computed: choose the controller output (CO) "
                                             u"or type ΔCO in step 1.");
    // CO tag chosen, but it is flat between the cursors (its step is at 20 s) while the PV moves.
    Fx fx(true, standardTags(), 20.0);
    fx.setCursors(9.0, 18.0);
    QVERIFY(fx.panel.results().at(0).valid);
    QVERIFY(!fx.panel.results().at(0).deltaCo.has_value());
    QStringList texts = checkTexts(fx.panel);
    QVERIFY2(anyContains(texts, tagText), qPrintable(texts.join('|')));
    QVERIFY(!anyContains(texts, typedText));

    // Typed ΔCO of 0: the "choose the CO or type" text.
    fx.apply([](AnalysisSettings &a) {
        a.deltaCoTagId = 0;
        a.manualDeltaCo = 0.0;
    });
    texts = checkTexts(fx.panel);
    QVERIFY2(anyContains(texts, typedText), qPrintable(texts.join('|')));
    QVERIFY(!anyContains(texts, tagText));
}

void TstAnalysisPanel::hintLine()
{
    Fx fx(false);
    auto *hint = child<QLabel>(fx.panel, "hintLabel");
    auto *label = child<QLabel>(fx.panel, "cursorLabel");
    QCOMPARE(hint->text(), QStringLiteral("Choose the process value (PV) in step 1."));
    QCOMPARE(label->text(), QStringLiteral(u"C1 = —, C2 = —"));
    fx.apply([](AnalysisSettings &a) { a.pvTagId = kPv; });
    QCOMPARE(hint->text(),
             QStringLiteral("Place C1 just before the step and C2 after the PV has settled, or click "
                            "Find step."));
    fx.plot.cursors()->setC1(10.0);
    QVERIFY(hint->text().startsWith(QStringLiteral("Place C1")));
    QCOMPARE(label->text(), QStringLiteral(u"C1 = 10.000 s, C2 = —"));
    fx.plot.cursors()->setC2(30.0);
    QCOMPARE(label->text(), QStringLiteral("C1 = 10.000 s, C2 = 30.000 s"));
    QVERIFY(hint->text().isEmpty());
    fx.plot.cursors()->setC2(10.0);
    QVERIFY(!fx.panel.results().at(0).valid);
    QCOMPARE(hint->text(), fx.panel.results().at(0).error);
    QVERIFY(!hint->text().isEmpty());
}

void TstAnalysisPanel::overlays()
{
    Fx fx;
    QCOMPARE(symbolMarkerCount(fx.plot), 2);
    QVERIFY(markerLabels(fx.plot).contains(QStringLiteral("63.2 %")));
    // Model curve for the PV only.
    QwtPlotCurve *model = findCurve(fx.plot, QStringLiteral("PV model"));
    QVERIFY(model);
    QVERIFY(!findCurve(fx.plot, QStringLiteral("Other model")));
    QVERIFY(!model->testItemAttribute(QwtPlotItem::Legend));
    QVERIFY(model->dataSize() >= 400);
    QCOMPARE(model->pen().style(), Qt::DashLine);
    QCOMPARE(model->pen().color(), QColor(QStringLiteral("#0000ff")));

    // Markers on a second tag only while it is checked.
    auto *tags = child<QListWidget>(fx.panel, "setpointTagList");
    QListWidgetItem *other = nullptr;
    for (int i = 0; i < tags->count(); ++i) {
        if (tags->item(i)->data(Qt::UserRole).toUInt() == kOther)
            other = tags->item(i);
    }
    QVERIFY(other);
    other->setCheckState(Qt::Checked);
    QCOMPARE(fx.panel.results().size(), 2);
    QCOMPARE(fx.panel.results().at(0).tagId, kPv);
    QCOMPARE(fx.panel.results().at(1).tagId, kOther);
    QCOMPARE(symbolMarkerCount(fx.plot), 4);
    QVERIFY(!findCurve(fx.plot, QStringLiteral("Other model")));
    QCOMPARE(table(fx.panel, "setpointTable")->rowCount(), 4);
    other->setCheckState(Qt::Unchecked);
    QCOMPARE(fx.panel.results().size(), 1);
    QCOMPARE(symbolMarkerCount(fx.plot), 2);
    QCOMPARE(table(fx.panel, "setpointTable")->rowCount(), 2);

    // A tag that is not plotted gets no markers (nor a list entry) even if listed.
    QList<Tag> tags2 = standardTags();
    tags2[2].plotted = false;
    fx.plot.setTags(tags2);
    fx.panel.setTags(tags2);
    fx.apply([](AnalysisSettings &a) { a.setpointTagIds = {kPv, kOther}; });
    QCOMPARE(tags->count(), 2); // CO and PV
    QCOMPARE(symbolMarkerCount(fx.plot), 2);

    // The PV is listed (it is chosen) but, not being plotted, it has no markers, no guide lines,
    // no model curve and no setpoint rows. It is still analysed (results, tuning).
    tags2[1].plotted = false;
    fx.plot.setTags(tags2);
    fx.panel.setTags(tags2);
    QCOMPARE(symbolMarkerCount(fx.plot), 0);
    QVERIFY(!findCurve(fx.plot, QStringLiteral("PV model")));
    QVERIFY(guideItems(fx.plot).isEmpty());
    QCOMPARE(table(fx.panel, "setpointTable")->rowCount(), 0);
    QCOMPARE(tags->count(), 2); // CO (plotted) and PV (chosen)
    QCOMPARE(fx.panel.results().size(), 1); // the PV only: "Other" is neither plotted nor analysed
    QVERIFY(fx.panel.results().at(0).valid && fx.panel.results().at(0).timeConstant);
    QVERIFY(table(fx.panel, "tuningTable")->rowCount() > 0);

    // Plotted again: everything returns.
    tags2[1].plotted = true;
    fx.plot.setTags(tags2);
    fx.panel.setTags(tags2);
    QCOMPARE(symbolMarkerCount(fx.plot), 2);
    QVERIFY(findCurve(fx.plot, QStringLiteral("PV model")));

    // Unchecking the PV removes its markers.
    fx.apply([](AnalysisSettings &a) { a.setpointTagIds = {}; });
    QCOMPARE(symbolMarkerCount(fx.plot), 0);

    // showModel off; no cursors.
    fx.apply([](AnalysisSettings &a) {
        a.setpointTagIds = {kPv};
        a.showModel = false;
    });
    QVERIFY(!findCurve(fx.plot, QStringLiteral("PV model")));
    QCOMPARE(symbolMarkerCount(fx.plot), 2);
    fx.plot.cursors()->clear();
    QCOMPARE(symbolMarkerCount(fx.plot), 0);
    QVERIFY(guideItems(fx.plot).isEmpty());
}

void TstAnalysisPanel::overlaysRemovedWithPlot()
{
    Fx fx;
    QVERIFY(symbolMarkerCount(fx.plot) > 0);
    QVERIFY(findCurve(fx.plot, QStringLiteral("PV model")));
    QCOMPARE(guideItems(fx.plot).size(), 1);
    fx.panel.setPlot(nullptr);
    QCOMPARE(symbolMarkerCount(fx.plot), 0);
    QVERIFY(!findCurve(fx.plot, QStringLiteral("PV model")));
    QVERIFY(guideItems(fx.plot).isEmpty());
    QVERIFY(fx.panel.results().isEmpty());
    QVERIFY(fx.panel.readoutText().isEmpty());

    // Not connected to the old plot's cursors any more.
    QSignalSpy results(&fx.panel, &AnalysisPanel::resultsChanged);
    fx.plot.cursors()->setC1(5.0);
    QCOMPARE(results.count(), 0);

    // Back on the plot: overlays return.
    fx.panel.setPlot(&fx.plot);
    QVERIFY(symbolMarkerCount(fx.plot) > 0);
    QVERIFY(findCurve(fx.plot, QStringLiteral("PV model")));
}

void TstAnalysisPanel::plotDestroyedFirst()
{
    Recorder rec;
    appendRows(rec, 0, 600);
    auto *plot = new TrendPlot;
    plot->setRecorder(&rec);
    plot->setTags(standardTags());
    AnalysisPanel panel;
    panel.setRecorder(&rec);
    panel.setPlot(plot);
    panel.setTags(standardTags());
    AnalysisSettings s;
    s.pvTagId = kPv;
    s.deltaCoTagId = kCo;
    s.setpointTagIds = {kPv};
    panel.setSettings(s);
    plot->cursors()->setC1(9.0);
    plot->cursors()->setC2(40.0);
    QVERIFY(!panel.results().isEmpty());
    delete plot; // takes its overlay items with it
    QVERIFY(panel.results().isEmpty());
    panel.setSettings(s); // must not touch freed items
    QVERIFY(panel.readoutText().isEmpty());
}

void TstAnalysisPanel::detailedLabels()
{
    Fx fx;
    QStringList labels = markerLabels(fx.plot);
    QCOMPARE(labels.size(), 2);
    const QwtPlotItemList items = fx.plot.plot()->itemList(QwtPlotItem::Rtti_PlotMarker);
    for (QwtPlotItem *item : items) {
        auto *m = static_cast<QwtPlotMarker *>(item);
        if (!m->symbol())
            continue; // the C1/C2 cursor lines
        const QwtText l = m->label();
        QVERIFY(l.backgroundBrush().style() != Qt::NoBrush);
        QVERIFY(l.backgroundBrush().color().alpha() >= 200);
        QCOMPARE(l.borderPen().color(), QColor(QStringLiteral("#0000ff")));
        QCOMPARE(m->symbol()->brush().color(), QColor(Qt::white));
        QVERIFY(m->symbol()->pen().color().lightness() < 80);
        QVERIFY(m->z() > 20.0);
        QVERIFY(l.color().lightness() < 80);
    }

    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    auto *check = child<QCheckBox>(fx.panel, "detailedLabelsCheck");
    check->setChecked(true);
    QCOMPARE(changed.count(), 1);
    labels = markerLabels(fx.plot);
    QString detailed;
    for (const QString &l : labels)
        if (l.startsWith(QStringLiteral("63.2 %")))
            detailed = l;
    const QStringList lines = detailed.split(u'\n');
    QCOMPARE(lines.size(), 3);
    QVERIFY2(lines.at(1).startsWith(QStringLiteral("t = 15.")), qPrintable(lines.at(1)));
    QVERIFY2(lines.at(2).startsWith(QStringLiteral("y = 56.3")), qPrintable(lines.at(2)));
    QVERIFY(lines.at(2).endsWith(QStringLiteral(" degC")));

    AnalysisSettings s = fx.panel.settings();
    s.detailedLabels = false;
    fx.panel.setSettings(s);
    QVERIFY(!check->isChecked());
    QCOMPARE(changed.count(), 1);
    QVERIFY(markerLabels(fx.plot).contains(QStringLiteral("63.2 %")));
}

void TstAnalysisPanel::guideLinesAndAxis()
{
    QList<Tag> tags = standardTags();
    tags[1].axis = YAxis::Right;
    Fx fx(true, tags);
    QCOMPARE(guideItems(fx.plot).size(), 1);
    QVERIFY(guideItems(fx.plot).at(0)->title().text().contains(QStringLiteral("(2 points)")));
    QVERIFY(!guideItems(fx.plot).at(0)->testItemAttribute(QwtPlotItem::Legend));
    QVERIFY(guideItems(fx.plot).at(0)->yAxis() == QwtPlot::yRight);
    int markers = 0;
    const QwtPlotItemList items = fx.plot.plot()->itemList(QwtPlotItem::Rtti_PlotMarker);
    for (QwtPlotItem *item : items) {
        if (static_cast<QwtPlotMarker *>(item)->symbol()) {
            ++markers;
            QVERIFY(item->yAxis() == QwtPlot::yRight);
        }
    }
    QCOMPARE(markers, 2);
    QVERIFY(findCurve(fx.plot, QStringLiteral("PV model"))->yAxis() == QwtPlot::yRight);
    fx.plot.resize(900, 500);
    fx.plot.show();
    QVERIFY(!fx.plot.grab().isNull());

    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    child<QCheckBox>(fx.panel, "guideLinesCheck")->setChecked(false);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(guideItems(fx.plot).size(), 0);
    QVERIFY(!fx.plot.grab().isNull());
    child<QCheckBox>(fx.panel, "guideLinesCheck")->setChecked(true);
    QCOMPARE(guideItems(fx.plot).size(), 1);
}

void TstAnalysisPanel::readoutText()
{
    Fx fx;
    const QString text = fx.panel.readoutText();
    const QStringList lines = text.split(u'\n');
    // No cursor line: the cursor panel's readout comes before this text.
    QCOMPARE(lines.size(), 1);
    QVERIFY(!text.contains(QStringLiteral("C1 = ")));
    const StepResult r = fx.panel.results().at(0);
    const QString expected =
        QStringLiteral("PV [degC] (0 % / 63.2 %): K = %1 degC/%, L = %2 s, T = %3 s, fit %4 % (Good)")
            .arg(g4(*r.gain), g4(*r.deadTime), g4(*r.timeConstant),
                 QString::number(*r.fitRmsPercent, 'g', 3));
    QCOMPARE(lines.at(0), expected);

    // Another setpoint tag adds a line with its setpoint times.
    fx.apply([](AnalysisSettings &a) { a.setpointTagIds = {kPv, kOther}; });
    const QStringList more = fx.panel.readoutText().split(u'\n');
    QCOMPARE(more.size(), 2);
    QVERIFY(more.at(1).startsWith(QStringLiteral("Other: 0 % at ")));
    QVERIFY(more.at(1).contains(QStringLiteral(", 63.2 % at ")));

    fx.apply([](AnalysisSettings &a) { a.method = ModelMethod::Smith; });
    QVERIFY(fx.panel.readoutText().contains(QStringLiteral("(28.3 % / 63.2 %)")));

    fx.plot.cursors()->clear();
    QVERIFY(fx.panel.readoutText().isEmpty());
    fx.setCursors(9.0, 9.0); // invalid
    QVERIFY(fx.panel.readoutText().isEmpty());
}

void TstAnalysisPanel::settingsChangedOnUserEditsOnly()
{
    Fx fx;
    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    // Programmatic changes: none.
    AnalysisSettings s = fx.panel.settings();
    s.tuningRule = TuningRule::Simc;
    s.lambdaS = 3.0;
    s.timeInMinutes = true;
    s.method = ModelMethod::Smith;
    s.pvRangeMin = 1.0;
    s.pvRangeMax = 5.0;
    s.coRangeMin = 10.0;
    s.coRangeMax = 20.0;
    s.smoothingS = 0.2;
    fx.panel.setSettings(s);
    fx.panel.setTags(standardTags());
    fx.plot.cursors()->setC1(8.0);
    QCOMPARE(changed.count(), 0);
    QVERIFY(fx.panel.settings() == s);

    // One signal per user edit.
    child<QComboBox>(fx.panel, "ruleCombo")->setCurrentIndex(0);
    QCOMPARE(changed.count(), 1);
    child<QDoubleSpinBox>(fx.panel, "lambdaSpin")->setValue(5.0);
    QCOMPARE(changed.count(), 2);
    child<QCheckBox>(fx.panel, "minutesCheck")->setChecked(false);
    QCOMPARE(changed.count(), 3);
    child<QComboBox>(fx.panel, "methodCombo")->setCurrentIndex(0);
    QCOMPARE(changed.count(), 4);
    child<QLineEdit>(fx.panel, "pvMinEdit")->setText(QStringLiteral("2"));
    QCOMPARE(changed.count(), 5);
    child<QLineEdit>(fx.panel, "pvMinEdit")->setText(QStringLiteral("2"));
    QCOMPARE(changed.count(), 5); // unchanged
    child<QDoubleSpinBox>(fx.panel, "coMaxSpin")->setValue(50.0);
    QCOMPARE(changed.count(), 6);
    child<QCheckBox>(fx.panel, "showModelCheck")->setChecked(false);
    QCOMPARE(changed.count(), 7);
    child<QComboBox>(fx.panel, "referenceCombo")->setCurrentIndex(1);
    QCOMPARE(changed.count(), 8);
    child<QDoubleSpinBox>(fx.panel, "deltaCoSpin")->setValue(3.0);
    QCOMPARE(changed.count(), 9);
    QCOMPARE(fx.panel.settings().coRangeMax, 50.0);
    QCOMPARE(fx.panel.settings().pvRangeMin, std::optional<double>(2.0));
    QVERIFY(!fx.panel.settings().showModel);
}

void TstAnalysisPanel::settingsRoundTrip()
{
    Fx fx(false);
    AnalysisSettings s;
    s.percents = {10.0, 50.0, 90.0};
    s.reference = ReferenceMode::WindowMean;
    s.referenceWindowS = 2.5;
    s.smoothingS = 0.6;
    s.method = ModelMethod::Custom;
    s.modelPercentA = 10.0;
    s.modelPercentB = 70.0;
    s.pvTagId = kPv;
    s.setpointTagIds = {kPv, kOther};
    s.deltaCoTagId = kCo;
    s.manualDeltaCo = 12.5;
    s.detailedLabels = true;
    s.showModel = false;
    s.guideLines = false;
    s.pvRangeMin = -5.0;
    s.pvRangeMax = 105.0;
    s.coRangeMin = 1.0;
    s.coRangeMax = 99.0;
    s.tuningRule = TuningRule::ImcLambda;
    s.lambdaS = 1.5;
    s.timeInMinutes = true;
    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    fx.panel.setSettings(s);
    QCOMPARE(changed.count(), 0);
    QVERIFY(fx.panel.settings() == s);
    QCOMPARE(child<QListWidget>(fx.panel, "percentList")->count(), 3);
    QCOMPARE(child<QComboBox>(fx.panel, "referenceCombo")->currentIndex(), 1);
    QVERIFY(child<QDoubleSpinBox>(fx.panel, "windowSpin")->isEnabled());
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "smoothingSpin")->value(), 0.6);
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "fromSpin")->value(), 10.0);
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "toSpin")->value(), 70.0);
    QVERIFY(child<QDoubleSpinBox>(fx.panel, "fromSpin")->isEnabled());
    QCOMPARE(child<QComboBox>(fx.panel, "pvCombo")->currentData().toUInt(), kPv);
    QCOMPARE(child<QComboBox>(fx.panel, "coCombo")->currentData().toUInt(), kCo);
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "coMinSpin")->value(), 1.0);
    QCOMPARE(child<QLineEdit>(fx.panel, "pvMaxEdit")->text(), QStringLiteral("105"));
    QCOMPARE(child<QComboBox>(fx.panel, "ruleCombo")->currentIndex(), 2);
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "lambdaSpin")->value(), 1.5);
    QVERIFY(child<QCheckBox>(fx.panel, "minutesCheck")->isChecked());
    QVERIFY(!child<QCheckBox>(fx.panel, "showModelCheck")->isChecked());
    QVERIFY(child<QCheckBox>(fx.panel, "detailedLabelsCheck")->isChecked());

    // A real edit emits exactly once and keeps the other fields (also those a spin box shows rounded).
    AnalysisSettings t = s;
    t.referenceWindowS = 0.25;
    t.smoothingS = 0.125;
    fx.panel.setSettings(t);
    child<QComboBox>(fx.panel, "referenceCombo")->setCurrentIndex(0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(fx.panel.settings().referenceWindowS, 0.25);
    QCOMPARE(fx.panel.settings().smoothingS, 0.125);
}

void TstAnalysisPanel::addRemovePercent()
{
    Fx fx;
    auto *list = child<QListWidget>(fx.panel, "percentList");
    auto *spin = child<QDoubleSpinBox>(fx.panel, "percentSpin");
    auto *add = child<QPushButton>(fx.panel, "addPercentButton");
    auto *remove = child<QPushButton>(fx.panel, "removePercentButton");
    QVERIFY(list && spin && add && remove);
    QCOMPARE(list->count(), 2);

    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    spin->setValue(28.3);
    add->click();
    QCOMPARE(changed.count(), 1);
    QCOMPARE(list->count(), 3);
    QCOMPARE(list->item(1)->text(), QStringLiteral("28.3 %"));
    QCOMPARE(fx.panel.settings().percents, (QList<double>{0.0, 28.3, 63.2}));
    QCOMPARE(table(fx.panel, "setpointTable")->rowCount(), 3);

    add->click(); // duplicate
    QCOMPARE(changed.count(), 1);
    QCOMPARE(list->count(), 3);

    list->setCurrentRow(1);
    remove->click();
    QCOMPARE(changed.count(), 2);
    QCOMPARE(fx.panel.settings().percents, (QList<double>{0.0, 63.2}));
    QCOMPARE(table(fx.panel, "setpointTable")->rowCount(), 2);

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
}

void TstAnalysisPanel::loadedPercentsNormalised()
{
    Fx fx(false);
    AnalysisSettings s;
    s.percents = {63.2, 0.0, 63.2, 28.3, 150.0};
    fx.panel.setSettings(s);
    QCOMPARE(fx.panel.settings().percents, (QList<double>{0.0, 28.3, 63.2}));
    QCOMPARE(child<QListWidget>(fx.panel, "percentList")->count(), 3);
}

void TstAnalysisPanel::modelPercentRevert()
{
    Fx fx;
    auto *method = child<QComboBox>(fx.panel, "methodCombo");
    auto *from = child<QDoubleSpinBox>(fx.panel, "fromSpin");
    auto *to = child<QDoubleSpinBox>(fx.panel, "toSpin");
    method->setCurrentIndex(3);
    QSignalSpy changed(&fx.panel, &AnalysisPanel::settingsChanged);
    from->setValue(70.0); // would make a >= b
    QCOMPARE(from->value(), 0.0);
    QCOMPARE(to->value(), 63.2);
    QCOMPARE(changed.count(), 0);
    to->setValue(0.0);
    QCOMPARE(to->value(), 63.2);
    QCOMPARE(changed.count(), 0);
    from->setValue(10.0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(fx.panel.settings().modelPercentA, 10.0);
}

void TstAnalysisPanel::rangesEdit()
{
    Fx fx;
    auto *pvMin = child<QLineEdit>(fx.panel, "pvMinEdit");
    auto *pvMax = child<QLineEdit>(fx.panel, "pvMaxEdit");
    QVERIFY(!fx.panel.settings().pvRangeMin);
    pvMin->setText(QStringLiteral("-10.5"));
    pvMax->setText(QStringLiteral("abc")); // not a number: not set
    QCOMPARE(fx.panel.settings().pvRangeMin, std::optional<double>(-10.5));
    QVERIFY(!fx.panel.settings().pvRangeMax);
    QCOMPARE(rowOf(table(fx.panel, "resultsTable"), QStringLiteral("K (%/%)")), -1);
    pvMax->setText(QStringLiteral("-10.5")); // zero span counts as not set
    QCOMPARE(rowOf(table(fx.panel, "resultsTable"), QStringLiteral("K (%/%)")), -1);
    pvMax->setText(QStringLiteral("89.5"));
    QVERIFY(rowOf(table(fx.panel, "resultsTable"), QStringLiteral("K (%/%)")) >= 0);
    pvMin->clear();
    QVERIFY(!fx.panel.settings().pvRangeMin);
    QCOMPARE(rowOf(table(fx.panel, "resultsTable"), QStringLiteral("K (%/%)")), -1);
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "coMinSpin")->value(), 0.0);
    QCOMPARE(child<QDoubleSpinBox>(fx.panel, "coMaxSpin")->value(), 100.0);
}

void TstAnalysisPanel::setpointTable()
{
    Fx fx;
    QTableWidget *sp = table(fx.panel, "setpointTable");
    QCOMPARE(sp->columnCount(), 6);
    QCOMPARE(sp->horizontalHeaderItem(0)->text(), QStringLiteral("Tag"));
    QCOMPARE(sp->horizontalHeaderItem(5)->text(), QStringLiteral(u"t − C1 (s)"));
    QCOMPARE(sp->rowCount(), 2);
    QCOMPARE(cell(sp, 0, 0), QStringLiteral("PV [degC]"));
    QCOMPARE(cell(sp, 0, 1), QStringLiteral("0"));
    QCOMPARE(cell(sp, 1, 1), QStringLiteral("63.2"));
    QVERIFY(std::abs(cell(sp, 0, 4).toDouble() - 11.5) < 0.15);
    QVERIFY(std::abs(cell(sp, 1, 4).toDouble() - 15.5) < 0.15);
    QVERIFY(std::abs(cell(sp, 1, 5).toDouble() - (cell(sp, 1, 4).toDouble() - 9.0)) < 0.01);
    QCOMPARE(sp->selectionMode(), QAbstractItemView::ExtendedSelection);
}

void TstAnalysisPanel::copyResults()
{
    Fx fx;
    auto *button = child<QPushButton>(fx.panel, "copyResultsButton");
    auto *exportButton = child<QPushButton>(fx.panel, "exportButton");
    QVERIFY(button->isEnabled());
    QVERIFY(exportButton->isEnabled());
    QSignalSpy exportSpy(&fx.panel, &AnalysisPanel::exportResultsRequested);
    exportButton->click();
    QCOMPARE(exportSpy.count(), 1);

    QGuiApplication::clipboard()->setText(QString());
    button->click();
    const QString text = QGuiApplication::clipboard()->text();
    QVERIFY(text.contains(QStringLiteral("Quantity\tValue")));
    QVERIFY(text.contains(QStringLiteral("Controller\tKp")));
    QVERIFY(text.contains(QStringLiteral("PID\t")));
    QVERIFY(text.contains(QStringLiteral("Tag\t%")));
    QVERIFY(text.contains(QStringLiteral("63.2")));
    QVERIFY(text.contains(QStringLiteral("\r\n\r\n")));
    QVERIFY(text.indexOf(QStringLiteral("Quantity")) < text.indexOf(QStringLiteral("Controller")));
    QVERIFY(text.indexOf(QStringLiteral("Controller")) < text.indexOf(QStringLiteral("Tag\t%")));

    for (const char *name : {"resultsTable", "tuningTable", "setpointTable"}) {
        QTableWidget *t = table(fx.panel, name);
        QCOMPARE(t->selectionMode(), QAbstractItemView::ExtendedSelection);
        bool found = false;
        const auto shortcuts = t->findChildren<QShortcut *>();
        for (QShortcut *sc : shortcuts)
            found = found || sc->key() == QKeySequence(QKeySequence::Copy);
        QVERIFY(found);
    }

    fx.plot.cursors()->clear();
    QVERIFY(!button->isEnabled());
    QVERIFY(!exportButton->isEnabled());
}

void TstAnalysisPanel::collapsedGroup()
{
    // The setpoint options live on their own page (the main window's Setpoints dock), not in the
    // panel's layout; the panel keeps driving them.
    Fx fx;
    QVERIFY(!child<QGroupBox>(fx.panel, "setpointsGroup"));
    QWidget *page = fx.panel.setpointsPage();
    QVERIFY(page);
    QCOMPARE(page->objectName(), QStringLiteral("setpointsPage"));
    QVERIFY(page->isAncestorOf(child<QListWidget>(fx.panel, "setpointTagList")));
    QVERIFY(page->isAncestorOf(child<QTableWidget>(fx.panel, "setpointTable")));
    fx.panel.show();
    QVERIFY(!page->isVisible()); // hidden until a dock takes it
    QWidget host;
    auto *lay = new QVBoxLayout(&host);
    lay->addWidget(page);
    page->show();
    host.show();
    QVERIFY(page->isVisible());
    QVERIFY(host.findChild<QTableWidget *>(QStringLiteral("setpointTable")));
    for (const char *name : {"signalsGroup", "cursorsGroup", "modelGroup", "tuningGroup"})
        QVERIFY(child<QGroupBox>(fx.panel, name));
    lay->removeWidget(page);
    page->setParent(&fx.panel); // the panel's own child again (deleted with it)
}

void TstAnalysisPanel::refreshRecompute()
{
    // Data ends inside the cursor range: new rows change the result.
    {
        Recorder rec;
        appendRows(rec, 0, 300); // to t = 30
        TrendPlot plot;
        plot.setRecorder(&rec);
        plot.setTags(standardTags());
        AnalysisPanel panel;
        panel.setRecorder(&rec);
        panel.setPlot(&plot);
        panel.setTags(standardTags());
        AnalysisSettings s;
        s.pvTagId = kPv;
        s.deltaCoTagId = kCo;
        panel.setSettings(s);
        plot.cursors()->setC1(9.0);
        plot.cursors()->setC2(40.0);
        panel.refresh();
        QSignalSpy results(&panel, &AnalysisPanel::resultsChanged);
        appendRows(rec, 301, 320);
        panel.refresh();
        QCOMPARE(results.count(), 1);
        panel.refresh(); // nothing new
        QCOMPARE(results.count(), 1);
    }
    // Data already beyond max(C1, C2) + S/2: appended rows cannot change anything.
    {
        Fx fx;
        fx.panel.refresh();
        QSignalSpy results(&fx.panel, &AnalysisPanel::resultsChanged);
        appendRows(fx.rec, 601, 650);
        fx.panel.refresh();
        QCOMPARE(results.count(), 0);

        fx.rec.clear();
        fx.panel.resetData();
        QVERIFY(results.count() >= 1);
        QVERIFY(fx.panel.results().isEmpty() || !fx.panel.results()[0].valid);
    }
}

void TstAnalysisPanel::evictionRecomputes()
{
    Recorder rec(400); // 40 s at 0.1 s
    appendRows(rec, 0, 399);
    TrendPlot plot;
    plot.setRecorder(&rec);
    plot.setTags(standardTags());
    AnalysisPanel panel;
    panel.setRecorder(&rec);
    panel.setPlot(&plot);
    panel.setTags(standardTags());
    AnalysisSettings s;
    s.pvTagId = kPv;
    s.deltaCoTagId = kCo;
    panel.setSettings(s);
    plot.cursors()->setC1(9.0);
    plot.cursors()->setC2(30.0);
    QCOMPARE(panel.results().size(), 1);
    QVERIFY(panel.results().at(0).valid);

    appendRows(rec, 400, 499);
    QSignalSpy changed(&panel, &AnalysisPanel::resultsChanged);
    panel.refresh();
    QCOMPARE(changed.count(), 1);
    QVERIFY(!panel.results().at(0).valid);
}

void TstAnalysisPanel::manyRecomputes()
{
    Recorder rec;
    TrendPlot plot;
    auto *panel = new AnalysisPanel;
    appendRows(rec, 0, 600);
    plot.setRecorder(&rec);
    plot.setTags(standardTags());
    panel->setRecorder(&rec);
    panel->setPlot(&plot);
    panel->setTags(standardTags());
    AnalysisSettings s;
    s.pvTagId = kPv;
    s.setpointTagIds = {kPv};
    s.deltaCoTagId = kCo;
    panel->setSettings(s);
    plot.cursors()->setC2(40.0);
    for (int i = 0; i < 50; ++i)
        plot.cursors()->setC1(9.0 + 0.01 * i);
    QCOMPARE(guideItems(plot).size(), 1);
    QCOMPARE(symbolMarkerCount(plot), 2);
    delete panel; // detaches and deletes its items: no crash / double delete
    QCOMPARE(symbolMarkerCount(plot), 0);
    QVERIFY(!findCurve(plot, QStringLiteral("PV model")));
    QVERIFY(guideItems(plot).isEmpty());
}

QTEST_MAIN(TstAnalysisPanel)
#include "tst_analysispanel.moc"
