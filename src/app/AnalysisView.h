#pragma once

#include "StepAnalysis.h"
#include "Tag.h"

#include <QList>
#include <QWidget>

#include <optional>

namespace mb {

class Recorder;
class TrendPlot;

/// Contents of the "Analysis" tab (step-response analysis, see PLAN.md "Analysis mode").
///
/// Layout: a horizontal QSplitter.
/// - Left: a TrendPlot of its own showing only the tags with Tag::analyzed (passed to
///   TrendPlot::setTags with plotted = true), with its own C1/C2 cursors.
/// - Right (in a QScrollArea), top to bottom:
///   * buttons "Copy cursors from Trend" (emits copyCursorsRequested()) and
///     "Export results (CSV)..." (emits exportResultsRequested(); enabled when at least one
///     valid result exists);
///   * "References" group: mode combo "Value at cursor" / "Mean before cursor", a W spin box
///     ("Window", s, 0.1..3600, enabled only for the mean), and an S spin box ("Smoothing",
///     s, 0..3600, special value text "Off" at 0);
///   * "Setpoints" group: list of percentages ("63.2 %") with a spin box (0..100, 2 decimals)
///     and Add / Remove buttons; the list stays sorted ascending without duplicates, at most
///     20 entries;
///   * "Model" group: spin boxes "From %" (a) and "To %" (b) (0..99.99; an edit that breaks
///     a < b is reverted), and ΔCO: a combo "Typed value" + every project tag ("name
///     [unit]", item data = TagId) and a value spin box enabled for "Typed value";
///   * "Setpoints" table: Tag (colour swatch, "name [unit]"), %, Target, Measured, Time (s),
///     t − C1 (s); one row per (analyzed tag, percent); "—" when not found;
///   * "Model" table: Tag, y0, y100, ΔPV, ΔCO, K, L (s), T (s); an invalid result shows its
///     error text spanning the value cells; a missing value shows "—".
///   Numbers use 6 significant digits.
/// - Plot markers: for every found setpoint a QwtPlotMarker at (time, target) with an
///   XCross symbol (size 11) in the tag colour and the label "<percent> %" (e.g. "63.2 %")
///   above right. Markers are plot items, so the image export includes them.
///
/// Recomputes (analyzeStep for every analyzed tag, in tag order) when the cursors, settings
/// or tags change, and in refresh() when new rows arrived while the previous last row was
/// older than max(C1, C2) + S/2 (later data cannot change the result). Emits
/// resultsChanged() after every recompute. Without both cursors: no markers, tables empty,
/// results() empty.
class AnalysisView : public QWidget
{
    Q_OBJECT
public:
    explicit AnalysisView(QWidget *parent = nullptr);
    ~AnalysisView() override;

    /// Not owned; may be null. Also passed to the internal TrendPlot.
    void setRecorder(const Recorder *recorder);
    /// All project tags (the ΔCO combo lists them all; the plot and results use the
    /// analyzed ones). Keeps the ΔCO selection when its tag still exists, else "Typed value".
    void setTags(const QList<Tag> &tags);

    /// Updates the controls without emitting settingsChanged(); recomputes.
    void setSettings(const AnalysisSettings &settings);
    AnalysisSettings settings() const;

    /// The Analysis plot; its cursors() are the analysis cursors.
    TrendPlot *trendPlot() const;
    /// Latest results, one per analyzed tag while both cursors are set (in tag order).
    QList<StepResult> results() const;
    /// Summary for the image export (empty without both cursors), e.g.
    /// "C1 = 10.000 s, C2 = 30.000 s\nLag [degC]: 0 % at 12.350 s, 63.2 % at 14.360 s;
    /// K = 2, L = 2.35 s, T = 2.01 s" (one line per analyzed tag).
    QString readoutText() const;

public slots:
    /// Pulls new data into the plot and recomputes when needed (called at ~10 Hz).
    void refresh();
    /// Forgets the plot data (after the Recorder was cleared); results are recomputed.
    void resetData();
    /// Sets the cursors of the Analysis plot (nullopt = that cursor is removed). Used for
    /// "Copy cursors from Trend".
    void setCursors(std::optional<double> t1, std::optional<double> t2);

signals:
    /// The user changed a setting (not emitted by setSettings()).
    void settingsChanged();
    void copyCursorsRequested();
    void exportResultsRequested();
    void resultsChanged();

private:
    // Implementation-defined; the task may change the private section freely.
    TrendPlot *m_plot = nullptr;
    const Recorder *m_recorder = nullptr;
    QList<Tag> m_tags;
    AnalysisSettings m_settings;
    QList<StepResult> m_results;
};

} // namespace mb
