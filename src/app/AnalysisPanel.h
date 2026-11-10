#pragma once

#include "StepAnalysis.h"
#include "Tag.h"
#include "Tuning.h"

#include <QColor>
#include <QList>
#include <QPointer>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QwtPlotItem;
class QwtPlotMarker;
class QListWidgetItem;
class QStackedWidget;
class QToolButton;
class TstAnalysisPanel;

namespace mb {

class ClosedLoopPage;
class OpenLoopPage;
class Recorder;
class TrendPlot;

namespace analysis {
struct Context;
}

/// Contents of the "Analysis" dock: a step-response analysis of the main Trend plot, laid out as
/// guided steps for students (see docs/USER_GUIDE.md). v1.3.3: every number field is a
/// NumberSpinBox (accepts 72,3 and 72.3). Replaces the former Analysis tab: there is
/// no plot of its own; the overlays are drawn on the TrendPlot given to setPlot(), and the cursors
/// are that plot's C1/C2.
///
/// Children (object names, for tests), top to bottom, in a QScrollArea:
/// - Buttons "Copy results" ("copyResultsButton") and "Export results (CSV)..." ("exportButton",
///   emits exportResultsRequested()). Open loop mode: both enabled while the PV (open-loop) result
///   is valid. Closed loop mode (v1.4.0): "Copy results" is enabled while the closed-loop page's
///   copyText() is not empty and copies that text (not the open-loop tables) followed by
///   setpointTable; "Export results (CSV)..." is disabled with the tooltip "The CSV export covers
///   the open-loop results. Use Copy results for the closed-loop numbers." (no tooltip in Open
///   loop mode). The CSV export covers the open-loop results only; the main window's Export >
///   Analysis results follows results() and is not changed by the mode.
/// - Group "1. Signals" ("signalsGroup"):
///   * "pvCombo": "Choose..." (data 0) + every tag ("name [unit]", data TagId). Choosing a PV sets
///     AnalysisSettings::pvTagId and adds it to setpointTagIds (the "Show setpoints on" list).
///   * "coCombo": "Typed ΔCO" (data 0) + every tag except the PV; "deltaCoSpin" (enabled for
///     "Typed ΔCO"; decimals 4, range -1e9..1e9).
///   * v1.4.0: "spCombo" "Setpoint (SP)", on its own row after the ΔCO row: "Typed" (data 0) +
///     every tag except the PV ("name [unit]", data TagId) = AnalysisSettings::spTagId. Same rules
///     as coCombo: kept while its tag exists, bit tags never offered, an unknown / bit / PV tag
///     (setSettings, setTags, choosing it as PV) means "Typed". Used by the closed-loop area.
///   * Ranges row "Ranges for %/% gains": "pvMinEdit", "pvMaxEdit" (QLineEdit, empty = not set,
///     numbers only), "coMinSpin", "coMaxSpin" (default 0 and 100).
/// - Group "2. Cursors" ("cursorsGroup"): "cursorLabel" "C1 = 10.000 s, C2 = 30.000 s" ("—" for a
///   missing cursor); "findStepButton" "Find step" (enabled when a CO tag and a PV are chosen and
///   a recorder is set; its tooltip says what is missing when disabled); "hintLabel", one line (mode-aware: in Closed loop mode it asks for C1 at the setpoint change and
///   C2 after settling, shows the closed-loop error, and Find step is disabled):
///   no PV: "Choose the process value (PV) in step 1."; a cursor missing: "Place C1 just before the
///   step and C2 after the PV has settled, or click Find step."; otherwise the result error, if
///   any, else empty. Find step calls findStep(recorder, CO, PV, from, to) with [from, to] = the
///   plot's visible time axis range clipped to the recording (the whole recording when that has no
///   samples), then sets the plot's C1/C2; nullopt -> hintLabel "No step found in the controller
///   output in the visible range." A step found with settled == false also shows the NotSettled
///   check, even when checkStep() does not report it: a private flag set by Find step is cleared by
///   the next cursor change that Find step did not make, and by a PV / CO / settings change.
/// - v1.4.0: group "Controller" ("controllerGroup", shared by both modes, between group 2 and the
///   mode switch): one row of three combo boxes = AnalysisSettings::controller. "formCombo"
///   (PidForm: Ideal (ISA), Series (interacting), Parallel; toString()), "gainUnitCombo" (Kp /
///   PB (%)), "integralUnitCombo" (Time / Repeats/min; disabled for Parallel, which always shows
///   Ki). Each has a one-sentence tooltip. A user change recomputes both pages and emits
///   settingsChanged() once; the app still computes in the ideal form and only converts what it
///   shows (PidForm.h).
/// - v1.4.0: mode switch "modeSwitch": two checkable, exclusive QToolButtons "openLoopButton"
///   "Open loop" and "closedLoopButton" "Closed loop" = AnalysisSettings::mode (default Open
///   loop). A click by the user emits settingsChanged() once (setSettings() does not); switching
///   keeps every setting and result (no recompute, no resultsChanged()) and only changes the shown
///   page and the overlays on the plot.
/// - QStackedWidget "modeStack", as tall as the shown page (the hidden page's height is ignored):
///   page 0 "openLoopPage" (OpenLoopPage.h) holds groups 3 and 4 below; page 1 "closedLoopPage"
///   (ClosedLoopPage.h) holds the closed-loop groups, described next.
/// - Closed loop page (v1.4.0, groups 3-5, all settings in AnalysisSettings::closedLoop):
///   * "3. Test" ("closedTestGroup"): "testCombo" (Setpoint step / Disturbance), "spBeforeSpin" /
///     "spAfterSpin" (typed setpoints, hidden when an SP tag is chosen; Disturbance shows only
///     "spAfterSpin", labelled "Setpoint"), "bandCombo" (settling band 2 % / 5 %); there is no page hint label: the
///     panel's "hintLabel" (group 2) is mode-aware.
///   * "4. Performance" ("performanceGroup"): "performanceTable" (Quantity, Measured and, with the
///     prediction shown, Predicted; every value cell has a didactic tooltip) and
///     "closedChecksList". Overlays on the plot while Closed loop mode is shown and both cursors
///     are set: settling band, overshoot / maximum deviation marker, predicted PV and output curves.
///   * "5. Prediction" ("predictionGroup"): "showPredictionCheck", "predictionModelLabel" ("Model: K =
///     ..., L = ... s, T = ... s (<source>)" or "No model yet: ..."), "modelKSpin" / "modelLSpin" /
///     "modelTSpin" (typing sets the model with source "typed"), "useOpenLoopModelButton" ("Use the
///     open-loop model", enabled while the open-loop result is valid), "predictionFormLabel",
///     "predGainSpin" / "predIntegralSpin" / "predDerivativeSpin" (in the Controller group's form,
///     tooltips with the conversion), "takeTuningCombo" + "takeTuningButton", "loadStepSpin"
///     (Disturbance only; ClosedLoopSettings::loadStep).
///   * The prediction model is saved with the project (ClosedLoopSettings::modelSet / modelK /
///     modelLS / modelTS / modelSource). It is taken from the open-loop result only when the
///     user clicks the Closed loop button (Open loop -> Closed loop) while none is set and the PV
///     result has K != 0, L >= 0, T > 0 (the mode switch emits settingsChanged(); no other
///     signal); never on a recompute, cursor move, setSettings() or a session/project load. Once
///     set, only the button or typing changes it (they emit settingsChanged()), so it survives
///     moving the cursors to the closed-loop test.
///     Choosing another PV does not clear it.
///   * readoutText() in Closed loop mode: the closed-loop page's readout (and nothing from the
///     open-loop page) whenever it is not empty, even when the open-loop result is invalid, then
///     the other setpoint tags' lines; Open loop mode unchanged.
/// - Group "3. Model" ("modelGroup", on the open-loop page):
///   * "methodCombo": the five ModelMethod values in the order 0 % / 63.2 %, Smith, Sundaresan,
///     Tangent, Custom, "fromSpin"/"toSpin" (0..99.99, enabled for Custom only; an edit that breaks
///     a < b is reverted). A fresh panel (and
///     File > New) shows the AnalysisSettings defaults (v1.3.5): Smith, 28.3 % / 63.2 % in the
///     spins, setpoint percents 0, 28.3 and 63.2.
///   * v1.4.0, method Tangent: the spins show 0 % / 100 % (disabled) and a check box
///     "tangentT632Check" "T from the 63.2 % point" (AnalysisSettings::tangentT632) is shown, only
///     with Tangent. The tooltips of L and T give the construction: "L = time the tangent reaches
///     y0 - step time", "T = ΔPV / slope" (or "T = t63.2 - step time - L"), with the numbers. The
///     overlay is the tangent drawn from its crossing of y0 to its crossing of y100 (dashed, model
///     colour, title "<name> tangent", not in the legend) with a small marker at the tangent point,
///     only while the PV is plotted and the result has a tangent. StepCheck::TangentNoisy shows
///     its sentence in the checks list.
///   * v1.4.0, "Compare with:" ("compareRow") under resultsTable: check boxes
///     "compare0632Check", "compareSmithCheck", "compareSundaresanCheck", "compareTangentCheck"
///     = AnalysisSettings::compareMethods (kept in that fixed order, no duplicates, never Custom;
///     setSettings() normalises a loaded list). The selected method's own box is hidden (its state
///     is kept) and it is never compared with itself. For each compared method analyzeStep() runs
///     with that method (all other settings unchanged) into a row of "compareTable" (columns Method,
///     L (s), T (s), Fit error; the selected method first, marked "(used for tuning)"; tooltips of
///     the L, T and Fit cells explain that method's numbers); the table is hidden when nothing is
///     compared. While compared (showModel or not), each method's model curve is drawn on the
///     plot (the PV's when plotted) like the main model curve but dotted, dash-dotted, dash-dot-dot
///     (the Tangent one with a long-dash pattern), in the model colour, title
///     "<name> model (<method>)", not in the legend. "Copy results" includes compareTable while it
///     is shown. The selected method alone drives the tuning.
///   * "checksList" also shows StepCheck::DeadTimeFromFirstMove (checkStep() with the settings)
///     when the lower model point is 0 % and the rule is Ziegler-Nichols or Cohen-Coon; it
///     follows the method and rule combos.
///   * "resultsTable" (QTableWidget, 2 columns "Quantity", "Value", no row header): rows y0, y100,
///     ΔPV, ΔCO, K, K (%/%) (only when both ranges are set), L, T, L/T, Fit error. Values with 4
///     significant digits and units: y0/y100/ΔPV in the PV unit, ΔCO in the CO unit (typed: none),
///     K in "<PV unit>/<CO unit>", L and T in s, Fit error "0.12 <unit> (0.6 %) — Good"
///     (FitQuality word). "—" for a missing value.
///   * "showModelCheck" "Show model curve" (AnalysisSettings::showModel, default off since
///     v1.3.3), followed by "modelColorButton" (a small button showing the colour swatch, tooltip
///     "Colour of the model curve"; QColorDialog through a replaceable seam; sets
///     AnalysisSettings::modelColor, emits settingsChanged()).
///   * "checksList" (QListWidget, no selection; hidden when empty): one item per checkStep() entry,
///     warning icon + sentence:
///     NoDeltaCo "K can't be computed: choose the controller output (CO) or type ΔCO in step 1."
///       (typed ΔCO, i.e. no CO tag chosen); with a CO tag chosen: "The controller output (CO) does
///       not change between the cursors: K can't be computed. Move the cursors around the step.";
///     NoModel "L and T can't be computed: the PV does not reach both model points (a % and b %)
///       between the cursors.";
///     NotSettled "The PV is still moving at C2: move C2 later.";
///     SmallStep "The step is small compared with the noise: the results are uncertain.";
///     NegativeDeadTime "L is negative: C1 is after the start of the response. Move C1 before the
///       step.";
///     DeadTimeDominant "L/T > 1: dead time dominates. Ziegler-Nichols and Cohen-Coon will be
///       aggressive; prefer IMC / lambda or SIMC.";
///     FitPoor "The model does not fit the response well (fit error > 8 %).";
///     v1.4.0, computed by the page (not a StepCheck), shown last: "CO moved by itself": a CO tag is
///     chosen and, from the step time + L (+ 1 s when there is no L) to C2, the CO tag changes by
///     more than 10 % of |ΔCO| between two consecutive samples, or between the first and the last
///     sample of that window: "The controller output changes after the step: the loop looks
///     closed. Put the controller in manual for an open-loop test, or use the Closed loop area."
/// - Group "4. Tuning" ("tuningGroup", on the open-loop page):
///   * "ruleCombo" (TuningRule, toString); "lambdaSpin" ("λ", s, 0..1e6, special value "Auto (= L)"
///     at 0; enabled for IMC / lambda and SIMC); "minutesCheck" "Ti, Td in minutes".
///   * "tuningTable": rows P, PI, PID; columns Kp, Kp (%/%) (only when both ranges are set), Ti,
///     Td; values from tune(K, L, T, rule, lambdaS); "—" for a missing value, "not defined" across
///     the row for an unavailable row. Empty (no rows) while K, L or T is missing or L <= 0.
///     v1.4.0: the values and headers follow the Controller group, through toForm() of the ideal
///     row: gain "Kp" (Ideal, Parallel) / "Kc'" (Series) or "PB (%)"; integral "Ti (s|min)" /
///     "Ti' (s|min)" / "Repeats/min" / "Ki (1/s|1/min)"; derivative "Td (s|min)" / "Td' (s|min)" /
///     "Kd (s|min)". The Kp (%/%) column stays only for Ideal with Kp (PB is already the percent
///     form). A row whose form is unavailable (Series with Ti < 4 Td, PB without the ranges) shows
///     the reason across the row, like "not defined". The tooltips keep the rule's formula first,
///     then the conversion with the numbers (pidConversionLines())
///   * "tuningNote": "Ideal (ISA) form: u = Kp (e + 1/Ti ∫e dt + Td de/dt). Ti and Td in s." (or
///     "in min"); for Series and Parallel it adds "Converted from the ideal form."; when K < 0 it adds "K is negative: the controller must be direct acting (Kp is
///     shown negative)."
/// - Setpoints page (setpointsPage(), object name "setpointsPage"; not in the panel's layout: the
///   main window shows it in its own "Setpoints" dock): "Show setpoints on" list ("setpointTagList",
///   checkable items, one per plotted tag + the PV, "name [unit]", data TagId) =
///   AnalysisSettings::setpointTagIds; the reference controls ("referenceCombo" "Value at cursor" /
///   "Mean before cursor", "windowSpin" W, "smoothingSpin" S with "Off" at 0); the percent list
///   ("percentList", "percentSpin", "addPercentButton", "removePercentButton"; sorted, no
///   duplicates, at most 20); "detailedLabelsCheck", "guideLinesCheck"; "setpointTable" (Tag,
///   %, Target, Measured, Time (s), t − C1 (s)) with one row per (checked tag, percent).
///   All tables get installCopyShortcut() (Ctrl+C) and extended selection. "Copy results" copies
///   resultsTable, tuningTable and setpointTable as tableToTsv(), separated by empty lines (in
///   Closed loop mode the closed-loop page's table replaces the open-loop ones).
///
/// Didactic tooltips: every value cell of resultsTable, tuningTable and setpointTable has a
/// rich-text tooltip (built by explainValue-style private helpers in one place) with (1) the name
/// and a one-sentence meaning, (2) the general formula, (3) the same formula with this result's
/// numbers and the value, e.g. for K: "<b>K, process gain</b>: change of the PV per unit of
/// controller output, once settled.<br>K = ΔPV / ΔCO<br>K = 12.5 / 10 = 1.25 °C/%". Content:
/// - y0 / y100: level at C1 / C2; "value at cursor" or "mean of the PV over W s before C1/C2".
/// - ΔPV = y100 - y0. ΔCO = CO(C2) - CO(C1) (tag) or "typed in step 1".
/// - K = ΔPV / ΔCO; K (%/%) = K × CO span / PV span with the spans.
/// - L and T: the method's formula with its crossing times measured from the step time (e.g. Smith:
///   T = 1.5 (t63.2 - t28.3) = 1.5 (14.36 - 11.02) = 5.01 s; L = t63.2 - T = ...). Generic
///   two-point (Custom): T = (tb - ta) / (ln(1 - a) - ln(1 - b)), L = ta + T ln(1 - a).
/// - L/T: "controllability ratio: below 0.3 easy, above 1 dead-time dominated".
/// - Fit error: RMS of (PV - model) between the cursors, and as % of |ΔPV|, with the thresholds.
/// - Setpoint times: "time at which the PV reaches y0 + p (y100 - y0) = <target>".
/// - Tuning cells: the rule's formula for that cell (Tuning.h) with K, L, T and λ filled in.
/// Numbers in tooltips use 4 significant digits.
///
/// Plot overlays (on setPlot's TrendPlot, only while both cursors are set):
/// - Only tags that are plotted get overlays and setpoint rows (an unplotted tag, the PV included,
///   shows none; the PV is still analysed for the results, tuning and CSV export, unplotted
///   non-PV setpoint tags are not analysed at all).
/// - For every tag in setpointTagIds that is plotted, every found setpoint: a marker as before
///   (white disc, dark ring, dot of the tag colour; label "63.2 %" or the detailed three lines;
///   z above the curves), and with guideLines the dashed guide lines (tag colour) to both axes.
///   v1.3.3: the 0 % setpoint is always placed at C1's time with Y = y0 (its "Time (s)" is t1
///   and "t − C1 (s)" is 0), even when the PV crosses y0 later (periodic signals, "Mean before
///   cursor" reference); other percents are unchanged.
/// - Setpoint markers and guide lines belong to the setpoints page: shown in both modes.
/// - showModel (v1.3.3: off by default), Open loop mode only (v1.4.0: switching to Closed loop
///   removes it from the plot, switching back restores it): the PV's model curve, only when the PV is plotted (dashed,
///   colour AnalysisSettings::modelColor (default #202020), width = the PV curve's pen width + 1;
///   v1.3.3, it was the tag colour), ~400 points, from t1 to t2, title
///   "<name> model", not in the legend). Overlays use each tag's Y axis.
/// - Overlays are plot items, so the image export includes them; they are removed on
///   setPlot(nullptr) and when they no longer apply.
///
/// Recomputes (analyzeStep for the PV and for each other plotted tag in setpointTagIds, with the PV's ΔCO
/// settings) when the cursors, settings or tags change, and in refresh() under the same "new data
/// can still change the result" rule as before (last row older than max(C1, C2) + S/2). Emits
/// resultsChanged() after every recompute.
/// v1.3.4: bit tags (coils, discrete inputs) are not offered as PV, CO or setpoint tags; a project
/// that names one keeps loading and treats it as not chosen.
class AnalysisPanel : public QWidget
{
    Q_OBJECT
public:
    explicit AnalysisPanel(QWidget *parent = nullptr);
    ~AnalysisPanel() override;

    /// The setpoint options and table, as a separate page (scrolls by itself). It starts as a
    /// hidden child of the panel; the main window reparents it into its "Setpoints" dock. The
    /// panel keeps driving it wherever it lives, and stops using it once it is destroyed.
    QWidget *setpointsPage() const;

    /// Not owned; may be null.
    void setRecorder(const Recorder *recorder);
    /// The main plot: overlays are drawn on it and its cursors are the analysis cursors (follows
    /// PlotCursors::cursorsChanged). Not owned; may be null (removes the overlays).
    void setPlot(TrendPlot *plot);
    /// All project tags. Keeps the PV / CO selection while their tags exist, else "Choose..." /
    /// "Typed ΔCO"; setpointTagIds keeps unknown ids (they are just not shown).
    void setTags(const QList<Tag> &tags);

    /// Updates the controls without emitting settingsChanged(); recomputes.
    void setSettings(const AnalysisSettings &settings);
    AnalysisSettings settings() const;

    /// Results of the PV followed by the other setpoint tags (in tag order); empty without both
    /// cursors or without a PV. results().first() is the PV's when not empty.
    QList<StepResult> results() const;
    /// Summary for the image export (empty without both cursors or a valid PV result), e.g.
    /// "Lag [degC] (0 % / 63.2 %): K = 2 degC/%, L = 2.35 s, T = 2.01 s, fit 0.6 % (Good)" plus one
    /// line per other shown setpoint tag with its setpoint times. It has no "C1 = ..., C2 = ..."
    /// line: AppController composes CursorPanel::readoutText() followed by this text.
    /// v1.4.0: in Closed loop mode the closed-loop page's readout replaces the PV line whenever it
    /// is not empty (also when the open-loop result is invalid).
    QString readoutText() const;

public slots:
    /// Recomputes when new data can still change the result (called at ~10 Hz).
    void refresh();
    /// Forgets cached data state after the Recorder was cleared; recomputes.
    void resetData();

signals:
    /// The user changed a setting (not emitted by setSettings()).
    void settingsChanged();
    void exportResultsRequested();
    void resultsChanged();

private:
    friend class ::TstAnalysisPanel;

    void buildUi();
    void syncControlsFromSettings();
    void normalizeSettings();
    void rebuildPvCombo();
    void rebuildCoCombo();
    void rebuildSpCombo();
    void rebuildSetpointList();
    void rebuildPercentList();
    void updateEnabled();
    void updateModeSwitch();
    void onModeClicked(AnalysisMode mode);
    void recompute();
    void rebuildSetpointTable();
    void rebuildOverlays();
    void clearOverlays();
    void updateResultButtons();
    void updateCursorLabel();
    void updateHint();
    void updateFindButton();
    void rememberDataState();
    void userChangedSettings();
    void copyResults();
    void onFindStep();
    void onPvChanged();
    void onCoChanged();
    void onSpChanged();
    void onControllerChanged();
    void onRangesChanged();
    void onAddPercent();
    void onRemovePercent();
    void onSetpointItemChanged(QListWidgetItem *item);

    // Tooltips of the setpoint table (the pages build their own).
    QString explainSetpoint(int column, const StepResult &result, const SetpointResult &sp,
                            const QString &tagName) const;

    /// Settings, tags, recorder, plot and step results, shared with the pages.
    std::unique_ptr<analysis::Context> m_ctx;

    QMetaObject::Connection m_cursorsConnection;
    QMetaObject::Connection m_plotDestroyedConnection;

    QPushButton *m_copyResultsButton = nullptr;
    QPushButton *m_exportButton = nullptr;
    QComboBox *m_pvCombo = nullptr;
    QComboBox *m_coCombo = nullptr;
    QDoubleSpinBox *m_deltaCoSpin = nullptr;
    QComboBox *m_spCombo = nullptr;
    QLineEdit *m_pvMinEdit = nullptr;
    QLineEdit *m_pvMaxEdit = nullptr;
    QDoubleSpinBox *m_coMinSpin = nullptr;
    QDoubleSpinBox *m_coMaxSpin = nullptr;
    QLabel *m_cursorLabel = nullptr;
    QPushButton *m_findStepButton = nullptr;
    QLabel *m_hintLabel = nullptr;
    QComboBox *m_formCombo = nullptr;
    QComboBox *m_gainUnitCombo = nullptr;
    QComboBox *m_integralUnitCombo = nullptr;
    QToolButton *m_openLoopButton = nullptr;
    QToolButton *m_closedLoopButton = nullptr;
    QStackedWidget *m_modeStack = nullptr;
    OpenLoopPage *m_openLoopPage = nullptr;
    ClosedLoopPage *m_closedLoopPage = nullptr;
    QPointer<QWidget> m_setpointsPage;
    QListWidget *m_setpointTagList = nullptr;
    QComboBox *m_referenceCombo = nullptr;
    QDoubleSpinBox *m_windowSpin = nullptr;
    QDoubleSpinBox *m_smoothingSpin = nullptr;
    QListWidget *m_percentList = nullptr;
    QDoubleSpinBox *m_percentSpin = nullptr;
    QPushButton *m_addPercentButton = nullptr;
    QPushButton *m_removePercentButton = nullptr;
    QCheckBox *m_detailedLabelsCheck = nullptr;
    QCheckBox *m_guideLinesCheck = nullptr;
    QTableWidget *m_setpointTable = nullptr;

    QList<QwtPlotMarker *> m_markers;
    QList<QwtPlotItem *> m_guideItems;

    bool m_updating = false;        ///< programmatic control changes: no user handling
    bool m_settingCursors = false;  ///< Find step in progress: recompute once at the end
    QString m_hintOverride;         ///< e.g. "No step found...", cleared by the next recompute
    qsizetype m_lastSize = 0;
    double m_lastTime = 0.0;
    quint64 m_lastEvicted = 0;
};

} // namespace mb
