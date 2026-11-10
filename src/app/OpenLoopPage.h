#pragma once

// Internal page of AnalysisPanel (not a contract): the open-loop area, groups "3. Model" and
// "4. Tuning". AnalysisPanel.h specifies every control, text, tooltip and overlay listed here.

#include "AnalysisCommon.h"

#include <QColor>
#include <QCoreApplication>
#include <QList>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QResizeEvent;
class QTableWidget;
class QwtPlotCurve;
class TstAnalysisPanel;

namespace mb {

/// Page 0 of the panel's "modeStack", object name "openLoopPage". Children: "modelGroup"
/// ("methodCombo", "fromSpin", "toSpin", "resultsTable", "showModelCheck", "modelColorButton",
/// "checksList") and "tuningGroup" ("ruleCombo", "lambdaSpin", "minutesCheck", "tuningTable",
/// "tuningNote"). Overlay: the PV's model curve (showModel).
///
/// The panel owns the Context (settings, tags, recorder, plot, step results) and calls:
/// - syncFromSettings() after it changed the settings (no user-edit hook call);
/// - recompute() after it recomputed Context::results: rebuilds the tables and checks;
/// - rebuildOverlays() inside its own overlay rebuild (attaches the model curve when the
///   overlays are shown; the panel replots once), clearOverlays() (detach + delete),
///   forgetOverlays() (the plot was destroyed and deleted the items);
/// - setOverlaysShown() when the mode changes (Open loop shows them);
/// - copyText() / readoutText() for "Copy results" and the image-export readout.
/// The user's edits write Context::settings and call the user-edit hook given to the
/// constructor (the panel recomputes and emits settingsChanged()).
///
/// Texts use the "mb::AnalysisPanel" translation context (they moved from AnalysisPanel).
class OpenLoopPage : public QWidget
{
    Q_DECLARE_TR_FUNCTIONS(mb::AnalysisPanel)
public:
    OpenLoopPage(analysis::Context *context, std::function<void()> userEdited,
                 QWidget *parent = nullptr);
    ~OpenLoopPage() override;

    void syncFromSettings();
    void recompute();

    void setOverlaysShown(bool shown);
    bool overlaysShown() const { return m_overlaysShown; }
    void rebuildOverlays();
    void clearOverlays();
    void forgetOverlays();

    /// resultsTable and tuningTable (with the controller names) as TSV, separated by an empty
    /// line, ending with a line break.
    QString copyText() const;
    /// The PV line of AnalysisPanel::readoutText(); empty without a valid PV result.
    QString readoutText() const;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    friend class ::TstAnalysisPanel;

    enum class Quantity { Y0, Y100, DeltaPv, DeltaCo, K, KPct, L, T, LOverT, Fit };

    void buildUi();
    void syncModelSpins();
    void updateEnabled();
    void updateModelColorButton();
    void onPickModelColor();
    void onMethodChanged();
    void onModelPercentChanged();
    void rebuildResultsTable();
    void rebuildChecks();
    void rebuildTuningTable();

    // Tooltips (all text built in these functions).
    QString explainResult(Quantity quantity, const StepResult *result) const;
    QString explainModelTime(bool forL, const StepResult *result) const;
    QString explainTuning(int column, ControllerKind kind, const TuningRow &row,
                          const StepResult &result) const;

    analysis::Context *m_ctx;
    std::function<void()> m_userEdited;

    QComboBox *m_methodCombo = nullptr;
    QDoubleSpinBox *m_fromSpin = nullptr;
    QDoubleSpinBox *m_toSpin = nullptr;
    QTableWidget *m_resultsTable = nullptr;
    QCheckBox *m_showModelCheck = nullptr;
    QPushButton *m_modelColorButton = nullptr;
    /// Seam for the colour dialog: (current colour, parent) -> chosen colour, invalid when
    /// cancelled. Tests replace it.
    std::function<QColor(const QColor &, QWidget *)> m_pickColor;
    QListWidget *m_checksList = nullptr;
    QComboBox *m_ruleCombo = nullptr;
    QDoubleSpinBox *m_lambdaSpin = nullptr;
    QCheckBox *m_minutesCheck = nullptr;
    QTableWidget *m_tuningTable = nullptr;
    QLabel *m_tuningNote = nullptr;

    QList<QwtPlotCurve *> m_modelCurves;
    bool m_overlaysShown = true;
    bool m_updating = false; ///< programmatic control changes: no user handling
    double m_prevFrom = 0.0;
    double m_prevTo = 63.2;
};

} // namespace mb
