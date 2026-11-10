#pragma once

// Internal page of AnalysisPanel (not a contract): the closed-loop area, groups "3. Test",
// "4. Performance" and "5. Prediction".

#include "AnalysisCommon.h"
#include "ClosedLoopAnalysis.h"
#include "ClosedLoopSim.h"

#include <QList>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QResizeEvent;
class QTableWidget;
class QwtPlotItem;
class TstAnalysisPanel;
class TstClosedLoopPage;

namespace mb {

/// Page 1 of the panel's "modeStack", object name "closedLoopPage". Children:
/// - "closedTestGroup": "testCombo" (Setpoint step / Disturbance), "spBeforeSpin" / "spAfterSpin"
///   (typed setpoints, hidden when an SP tag is chosen; Disturbance shows only "spAfterSpin",
///   labelled "Setpoint"), "bandCombo" (settling band 2 % / 5 %). The hint for missing PV / cursors is the panel's
///   "hintLabel" (mode-aware).
/// - "performanceGroup": "performanceTable" (Quantity, Measured and, with the prediction shown,
///   Predicted; every value cell has a didactic tooltip) and "closedChecksList".
/// - "predictionGroup": "showPredictionCheck", "predictionModelLabel", "modelKSpin" / "modelLSpin" /
///   "modelTSpin" (K, L (s), T (s) of the model, 6 significant digits; typing sets ClosedLoopSettings::modelSet and
///   modelSource "typed"; T has a minimum of 0.001 and L of 0), "useOpenLoopModelButton" ("Use the
///   open-loop model"; enabled while the open-loop result is valid with K, L >= 0, T > 0),
///   "predictionFormLabel", "predGainSpin" / "predIntegralSpin" / "predDerivativeSpin" (the ideal
///   settings shown in the controller form of AnalysisSettings::controller, labels and tooltips
///   with the conversion as in the open-loop tuning table), "takeTuningCombo" + "takeTuningButton",
///   "loadStepSpin" (Disturbance only; ClosedLoopSettings::loadStep, saved in the project).
/// The model belongs to the project (ClosedLoopSettings::modelSet / modelK / modelLS / modelTS /
/// modelSource), not to the page: it is taken from the open-loop result only by the panel's switch
/// from Open loop to Closed loop (takeOpenLoopModelIfUnset(), while modelSet is false and the
/// result has K != 0, L >= 0, T > 0), never on a recompute, cursor move, data refresh or
/// setSettings(); once set, only the button or typing changes it (both call the hook). An invalid
/// model (T <= 0, L < 0, K == 0, not finite) behaves as "no model": the label shows "No model
/// yet..." and there is no prediction. Controls the user is typing in (focus) are not overwritten
/// by recomputes.
/// Overlays (while shown and both cursors are set): the settling band, the overshoot / maximum
/// deviation marker and, with the prediction shown, the predicted PV and output curves.
///
/// The panel owns the Context (settings including AnalysisSettings::closedLoop / controller /
/// spTagId, tags, recorder, plot, the open-loop step results) and calls:
/// - syncFromSettings() after it changed the settings (no user-edit hook call);
/// - recompute() on every panel recompute (cursors, settings, tags, data under the refresh rule);
/// - rebuildOverlays() inside its own overlay rebuild (attach only while the overlays are shown;
///   the panel replots once), clearOverlays() (detach + delete), forgetOverlays() (the plot was
///   destroyed and deleted the items);
/// - setOverlaysShown() when the mode changes (Closed loop shows them);
/// - copyText() / readoutText(): this page's part of "Copy results" and of the image-export
///   readout, added by the panel in Closed loop mode when not empty.
/// The user's edits write Context::settings and call the user-edit hook given to the constructor.
class ClosedLoopPage : public QWidget
{
    Q_OBJECT
public:
    ClosedLoopPage(analysis::Context *context, std::function<void()> userEdited,
                   QWidget *parent = nullptr);
    ~ClosedLoopPage() override;

    void syncFromSettings();
    void recompute();
    /// The closed-loop result's error text (empty when valid or not analysed); for the panel's hint.
    QString resultError() const { return (m_analysed && !m_result.valid) ? m_result.error : QString(); }
    /// The automatic model take: copies the valid open-loop PV model (K != 0, L >= 0, T > 0) into
    /// the settings while modelSet is false. Returns true when it took one. The panel calls it
    /// only when the user switches from Open loop to Closed loop; no user-edit hook (the mode
    /// switch marks the project modified).
    bool takeOpenLoopModelIfUnset();

    void setOverlaysShown(bool shown);
    bool overlaysShown() const { return m_overlaysShown; }
    void rebuildOverlays();
    void clearOverlays();
    void forgetOverlays();

    /// performanceTable as TSV (with its header row); empty without a valid result.
    QString copyText() const;
    /// "SP step 50 → 60 %: overshoot 6.04 % (60 % of the step), settling 41.8 s (2 % band), IAE
    /// 152" plus a "Predicted: ..." line when the prediction is shown; empty without a valid
    /// result.
    QString readoutText() const;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    friend class ::TstAnalysisPanel;
    friend class ::TstClosedLoopPage;

    enum class Quantity {
        SpBefore, SpAfter, Overshoot, PeakTime, RiseTime, Setpoint, MaxDeviation, MaxDeviationTime,
        SettlingTime, DecayRatio, Period, SteadyStateError, Iae, Ise, Itae, OutMin, OutMax, OutTravel
    };

    /// The predicted response (cached by its inputs).
    struct Prediction {
        bool valid = false;
        bool hasOutput = false; ///< a CO tag is chosen: the output curve and rows exist
        ClosedLoopSeries series; ///< times shifted to the recording's time axis
        ClosedLoopResult result;
        QList<double> key;
    };

    void buildUi();
    void updateTestControls();
    void updateModelUi();
    void updatePredictionLabels();
    void syncPredictionSpins();
    void rebuildTakeTuningCombo();
    void updatePrediction();
    void rebuildTable();
    void rebuildChecks();
    /// part: 0 gain, 1 integral, 2 derivative (the spin the user edited).
    void onPredictionSpinEdited(int part);
    void onTakeTuning();
    void onModelSpinEdited();
    void onUseOpenLoopModel();
    void refreshModel();
    /// The valid open-loop model of the current results (K, L, T) and its source text.
    std::optional<ProcessModel> currentOpenLoopModel(QString *source = nullptr) const;
    /// The saved model when modelSet and valid, else nullopt.
    std::optional<ProcessModel> openLoopModel() const;
    QList<TuningRow> tuningRows() const;
    QString valueText(Quantity q, const ClosedLoopResult &r) const;
    QString explain(Quantity q, const ClosedLoopResult *r, bool predicted) const;
    QString summary(const ClosedLoopResult &r) const;
    double settlingBand(const ClosedLoopResult &r) const;

    analysis::Context *m_ctx;
    std::function<void()> m_userEdited;

    QComboBox *m_testCombo = nullptr;
    QLabel *m_spBeforeLabel = nullptr;
    QDoubleSpinBox *m_spBeforeSpin = nullptr;
    QLabel *m_spAfterLabel = nullptr;
    QDoubleSpinBox *m_spAfterSpin = nullptr;
    QComboBox *m_bandCombo = nullptr;
    QTableWidget *m_table = nullptr;
    QListWidget *m_checksList = nullptr;
    QCheckBox *m_showPredictionCheck = nullptr;
    QLabel *m_modelLabel = nullptr;
    QLabel *m_formLabel = nullptr;
    QLabel *m_gainLabel = nullptr;
    QLabel *m_integralLabel = nullptr;
    QLabel *m_derivativeLabel = nullptr;
    QDoubleSpinBox *m_gainSpin = nullptr;
    QDoubleSpinBox *m_integralSpin = nullptr;
    QDoubleSpinBox *m_derivativeSpin = nullptr;
    QComboBox *m_takeCombo = nullptr;
    QPushButton *m_takeButton = nullptr;
    QLabel *m_loadStepLabel = nullptr;
    QDoubleSpinBox *m_loadStepSpin = nullptr;
    QLabel *m_modelKLabel = nullptr;
    QLabel *m_modelLLabel = nullptr;
    QLabel *m_modelTLabel = nullptr;
    QDoubleSpinBox *m_modelKSpin = nullptr;
    QDoubleSpinBox *m_modelLSpin = nullptr;
    QDoubleSpinBox *m_modelTSpin = nullptr;
    QPushButton *m_useModelButton = nullptr;

    ClosedLoopResult m_result;
    bool m_analysed = false; ///< a result was computed (PV, recorder and both cursors present)
    Prediction m_pred;
    QStringList m_takeKey; ///< texts and tooltips of the take-tuning rows (rebuild only on change)

    QList<QwtPlotItem *> m_items;
    bool m_overlaysShown = false;
    bool m_updating = false; ///< programmatic control changes: no user handling
};

} // namespace mb
