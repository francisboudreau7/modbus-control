#pragma once

// Internal page of AnalysisPanel (not a contract): the closed-loop area, groups "3. Test",
// "4. Performance" and "5. Prediction".

#include "AnalysisCommon.h"
#include "ClosedLoopAnalysis.h"
#include "ClosedLoopSim.h"

#include <QList>
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
///   labelled "Setpoint"), "bandCombo" (settling band 2 % / 5 %), "closedHintLabel".
/// - "performanceGroup": "performanceTable" (Quantity, Measured and, with the prediction shown,
///   Predicted; every value cell has a didactic tooltip) and "closedChecksList".
/// - "predictionGroup": "showPredictionCheck", "predictionModelLabel", "predictionFormLabel",
///   "predGainSpin" / "predIntegralSpin" / "predDerivativeSpin" (the ideal settings shown in the
///   controller form of AnalysisSettings::controller), "takeTuningCombo" + "takeTuningButton",
///   "loadStepSpin" (Disturbance only; not saved in the project).
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
    void updateHint();
    void onPredictionSpinEdited();
    void onTakeTuning();
    void onLoadStepChanged();
    void refreshModel();
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
    QLabel *m_hintLabel = nullptr;
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

    ClosedLoopResult m_result;
    bool m_analysed = false; ///< a result was computed (PV, recorder and both cursors present)
    Prediction m_pred;
    std::optional<ProcessModel> m_model; ///< last valid open-loop model of m_modelPv
    TagId m_modelPv = 0;
    ModelMethod m_modelMethod = ModelMethod::Smith;
    double m_loadStep = 5.0; ///< CO units; not saved in the project

    QList<QwtPlotItem *> m_items;
    bool m_overlaysShown = false;
    bool m_updating = false; ///< programmatic control changes: no user handling
};

} // namespace mb
