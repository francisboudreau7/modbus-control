#pragma once

#include "StepAnalysis.h"
#include "Tag.h"
#include "Tuning.h"

#include <QList>
#include <QPointer>
#include <QWidget>

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
class QwtPlotCurve;
class QwtPlotItem;
class QwtPlotMarker;
class QListWidgetItem;
class QResizeEvent;
class TstAnalysisPanel;

namespace mb {

class Recorder;
class TrendPlot;

/// Contents of the "Analysis" dock: a step-response analysis of the main Trend plot, laid out as
/// guided steps for students (see docs/USER_GUIDE.md). Replaces the former Analysis tab: there is
/// no plot of its own; the overlays are drawn on the TrendPlot given to setPlot(), and the cursors
/// are that plot's C1/C2.
///
/// Children (object names, for tests), top to bottom, in a QScrollArea:
/// - Buttons "Copy results" ("copyResultsButton") and "Export results (CSV)..." ("exportButton",
///   emits exportResultsRequested()); both enabled while the PV result is valid.
/// - Group "1. Signals" ("signalsGroup"):
///   * "pvCombo": "Choose..." (data 0) + every tag ("name [unit]", data TagId). Choosing a PV sets
///     AnalysisSettings::pvTagId and adds it to setpointTagIds (the "Show setpoints on" list).
///   * "coCombo": "Typed ΔCO" (data 0) + every tag except the PV; "deltaCoSpin" (enabled for
///     "Typed ΔCO"; decimals 4, range -1e9..1e9).
///   * Ranges row "Ranges for %/% gains": "pvMinEdit", "pvMaxEdit" (QLineEdit, empty = not set,
///     numbers only), "coMinSpin", "coMaxSpin" (default 0 and 100).
/// - Group "2. Cursors" ("cursorsGroup"): "cursorLabel" "C1 = 10.000 s, C2 = 30.000 s" ("—" for a
///   missing cursor); "findStepButton" "Find step" (enabled when a CO tag and a PV are chosen and
///   a recorder is set; its tooltip says what is missing when disabled); "hintLabel", one line:
///   no PV: "Choose the process value (PV) in step 1."; a cursor missing: "Place C1 just before the
///   step and C2 after the PV has settled, or click Find step."; otherwise the result error, if
///   any, else empty. Find step calls findStep(recorder, CO, PV, from, to) with [from, to] = the
///   plot's visible time axis range clipped to the recording (the whole recording when that has no
///   samples), then sets the plot's C1/C2; nullopt -> hintLabel "No step found in the controller
///   output in the visible range." A step found with settled == false also shows the NotSettled
///   check, even when checkStep() does not report it: a private flag set by Find step is cleared by
///   the next cursor change that Find step did not make, and by a PV / CO / settings change.
/// - Group "3. Model" ("modelGroup"):
///   * "methodCombo": the four ModelMethod values (toString), "fromSpin"/"toSpin" (0..99.99,
///     enabled for Custom only; an edit that breaks a < b is reverted).
///   * "resultsTable" (QTableWidget, 2 columns "Quantity", "Value", no row header): rows y0, y100,
///     ΔPV, ΔCO, K, K (%/%) (only when both ranges are set), L, T, L/T, Fit error. Values with 4
///     significant digits and units: y0/y100/ΔPV in the PV unit, ΔCO in the CO unit (typed: none),
///     K in "<PV unit>/<CO unit>", L and T in s, Fit error "0.12 <unit> (0.6 %) — Good"
///     (FitQuality word). "—" for a missing value.
///   * "showModelCheck" "Show model curve" (AnalysisSettings::showModel, default on).
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
///     FitPoor "The model does not fit the response well (fit error > 8 %)."
/// - Group "4. Tuning" ("tuningGroup"):
///   * "ruleCombo" (TuningRule, toString); "lambdaSpin" ("λ", s, 0..1e6, special value "Auto (= L)"
///     at 0; enabled for IMC / lambda and SIMC); "minutesCheck" "Ti, Td in minutes".
///   * "tuningTable": rows P, PI, PID; columns Kp, Kp (%/%) (only when both ranges are set), Ti,
///     Td; values from tune(K, L, T, rule, lambdaS); "—" for a missing value, "not defined" across
///     the row for an unavailable row. Empty (no rows) while K, L or T is missing or L <= 0.
///   * "tuningNote": "Ideal (ISA) form: u = Kp (e + 1/Ti ∫e dt + Td de/dt). Ti and Td in s." (or
///     "in min"); when K < 0 it adds "K is negative: the controller must be direct acting (Kp is
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
///   resultsTable, tuningTable and setpointTable as tableToTsv(), separated by empty lines.
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
/// - showModel: the PV's model curve, only when the PV is plotted (dashed, tag colour, ~400 points, from t1 to t2, title
///   "<name> model", not in the legend). Overlays use each tag's Y axis.
/// - Overlays are plot items, so the image export includes them; they are removed on
///   setPlot(nullptr) and when they no longer apply.
///
/// Recomputes (analyzeStep for the PV and for each other plotted tag in setpointTagIds, with the PV's ΔCO
/// settings) when the cursors, settings or tags change, and in refresh() under the same "new data
/// can still change the result" rule as before (last row older than max(C1, C2) + S/2). Emits
/// resultsChanged() after every recompute.
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

    enum class Quantity { Y0, Y100, DeltaPv, DeltaCo, K, KPct, L, T, LOverT, Fit };

    void buildUi();
    void syncControlsFromSettings();
    void normalizeSettings();
    void rebuildPvCombo();
    void rebuildCoCombo();
    void rebuildSetpointList();
    void rebuildPercentList();
    void syncModelSpins();
    void updateEnabled();
    void recompute();
    void rebuildResultsTable();
    void rebuildChecks();
    void rebuildTuningTable();
    void rebuildSetpointTable();
    void rebuildOverlays();
    void clearOverlays(bool detach);
    void updateCursorLabel();
    void updateHint();
    void updateFindButton();
    void rememberDataState();
    void userChangedSettings();
    void copyResults();
    void onFindStep();
    void onPvChanged();
    void onCoChanged();
    void onMethodChanged();
    void onModelPercentChanged();
    void onRangesChanged();
    void onAddPercent();
    void onRemovePercent();
    void onSetpointItemChanged(QListWidgetItem *item);
    void resizeEvent(QResizeEvent *event) override;

    const Tag *tagById(TagId id) const;
    bool shownTag(const Tag *tag) const; ///< setpoint rows / markers of this tag
    QString pvUnit() const;
    QString coUnit() const;
    QString kUnit() const;
    std::optional<double> pvSpan() const;
    std::optional<double> coSpan() const;
    bool percentGainsShown() const;

    // Tooltips (all text built in these functions).
    QString explainResult(Quantity quantity, const StepResult *result) const;
    QString explainModelTime(bool forL, const StepResult *result) const;
    QString explainSetpoint(int column, const StepResult &result, const SetpointResult &sp,
                            const QString &tagName) const;
    QString explainTuning(int column, ControllerKind kind, const TuningRow &row,
                          const StepResult &result) const;

    const Recorder *m_recorder = nullptr;
    TrendPlot *m_plot = nullptr;
    QList<Tag> m_tags;
    AnalysisSettings m_settings;
    QList<StepResult> m_results;

    QMetaObject::Connection m_cursorsConnection;
    QMetaObject::Connection m_plotDestroyedConnection;

    QPushButton *m_copyResultsButton = nullptr;
    QPushButton *m_exportButton = nullptr;
    QComboBox *m_pvCombo = nullptr;
    QComboBox *m_coCombo = nullptr;
    QDoubleSpinBox *m_deltaCoSpin = nullptr;
    QLineEdit *m_pvMinEdit = nullptr;
    QLineEdit *m_pvMaxEdit = nullptr;
    QDoubleSpinBox *m_coMinSpin = nullptr;
    QDoubleSpinBox *m_coMaxSpin = nullptr;
    QLabel *m_cursorLabel = nullptr;
    QPushButton *m_findStepButton = nullptr;
    QLabel *m_hintLabel = nullptr;
    QComboBox *m_methodCombo = nullptr;
    QDoubleSpinBox *m_fromSpin = nullptr;
    QDoubleSpinBox *m_toSpin = nullptr;
    QTableWidget *m_resultsTable = nullptr;
    QCheckBox *m_showModelCheck = nullptr;
    QListWidget *m_checksList = nullptr;
    QComboBox *m_ruleCombo = nullptr;
    QDoubleSpinBox *m_lambdaSpin = nullptr;
    QCheckBox *m_minutesCheck = nullptr;
    QTableWidget *m_tuningTable = nullptr;
    QLabel *m_tuningNote = nullptr;
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
    QList<QwtPlotCurve *> m_modelCurves;

    bool m_updating = false;        ///< programmatic control changes: no user handling
    bool m_settingCursors = false;  ///< Find step in progress: recompute once at the end
    bool m_findNotSettled = false;  ///< Find step found settled == false; cleared by other changes
    QString m_hintOverride;         ///< e.g. "No step found...", cleared by the next recompute
    double m_prevFrom = 0.0;
    double m_prevTo = 63.2;
    qsizetype m_lastSize = 0;
    double m_lastTime = 0.0;
    quint64 m_lastEvicted = 0;
};

} // namespace mb
