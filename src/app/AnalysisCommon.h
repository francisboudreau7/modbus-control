#pragma once

// Internal helpers shared by AnalysisPanel and its pages (OpenLoopPage, ClosedLoopPage). Not a
// contract: only the analysis panel's files include it.

#include "StepAnalysis.h"
#include "Tag.h"
#include "Tuning.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

class QDoubleSpinBox;
class QTableWidget;
class QTableWidgetItem;
class QWidget;

namespace mb {

class PlotCursors;
class Recorder;
class TrendPlot;

namespace analysis {

inline const QString kDash = QStringLiteral(u"—");  // em dash: missing value
inline const QString kDelta = QStringLiteral(u"Δ");
inline const QString kMinus = QStringLiteral(u"−");
inline const QString kTimes = QStringLiteral(u"×");

/// State shared by the panel and its pages. Owned by AnalysisPanel; the pages keep a pointer and
/// read it (and write `settings` in their own controls' handlers, then call the panel's
/// user-edit hook).
struct Context {
    AnalysisSettings settings;
    QList<Tag> tags;
    const Recorder *recorder = nullptr; ///< not owned; may be null
    TrendPlot *plot = nullptr;          ///< not owned; may be null
    /// Step analysis of the PV followed by the other shown setpoint tags (computed by the panel,
    /// used by the open-loop page and the setpoints page).
    QList<StepResult> results;
    /// Find step found settled == false; cleared by other changes (see AnalysisPanel.h).
    bool findNotSettled = false;

    /// The tag with this id, null for 0, unknown ids and bit tags (never a PV, CO or SP tag).
    const Tag *tagById(TagId id) const;
    /// Setpoint rows / markers of this tag are shown.
    bool shownTag(const Tag *tag) const;
    QString pvUnit() const;
    QString coUnit() const;
    QString kUnit() const;
    std::optional<double> pvSpan() const;
    std::optional<double> coSpan() const;
    bool percentGainsShown() const;
    /// The plot's cursors, null without a plot.
    const PlotCursors *cursors() const;
    bool hasBothCursors() const;
};

/// 4 significant digits (tables and tooltips); "—" for NaN / infinite.
QString g4(double v);
QString g4(std::optional<double> v);
/// Negative numbers in parentheses, for use inside formulas ("a - (-3)").
QString g4p(double v);
/// 6 significant digits (setpoint table).
QString g6(double v);
QString withUnit(const QString &number, const QString &unit);
QString fmtSeconds(double t);
QString fmtPlain(double p);
QString fmtPercent(double p);
/// "name [unit]" (just the name without a unit).
QString tagLabel(const Tag &tag);
QString fitWord(double fitRmsPercent);
/// Display names of the model methods and tuning rules (the core toString() names are English).
QString methodName(ModelMethod method);
QString ruleName(TuningRule rule);
/// "name: meaning<br>line<br>line" - everything escaped, name in bold.
QString tip(const QString &name, const QString &meaning, const QStringList &lines);
/// Replaces %1..%5 in `format` (QString::arg would warn about unused numbers).
QString substitute(QString format, const QStringList &values);

QTableWidgetItem *makeItem(const QString &text, bool alignRight, const QString &toolTip = {});
QDoubleSpinBox *makeSpin(const char *name, double lo, double hi, int decimals, double value,
                         QWidget *parent);
void fitTableHeight(QTableWidget *t);
/// Read-only, extended selection, Ctrl+C.
void setupTable(QTableWidget *t);

} // namespace analysis
} // namespace mb
