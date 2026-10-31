#pragma once

#include <QDialog>
#include <QSize>
#include <QString>

class QwtPlot;

namespace mb {

class PlotCursors;

/// What to draw (v1.3.3).
struct ExportOptions {
    enum class ReadoutPosition { Below, TopLeft, TopRight, BottomLeft, BottomRight };

    QSize sizePx;            ///< size of the plot part; Below adds the readout height to it
    int dpi = 96;
    bool includeCursors = true;
    bool includeLegend = true;   ///< the plot's legend (when it has one)
    QString title;               ///< empty = none; drawn centred above the plot, bold
    QString cursorReadout;       ///< empty = not drawn
    QString analysisReadout;     ///< empty = not drawn; drawn after the cursor readout
    ReadoutPosition position = ReadoutPosition::Below;
};

/// "Export graph" dialog: file path with Browse... (format from the extension: .png, .svg,
/// .pdf), width/height in pixels (default = current plot size), DPI (default 96),
/// "Include cursors" (enabled when a cursor exists), and (v1.3.3):
/// - "titleEdit" "Title" (empty = no title);
/// - "legendCheck" "Include legend" (checked);
/// - "cursorReadoutCheck" "Include cursor readout" (enabled when cursorReadout is non-empty) and
///   "analysisReadoutCheck" "Include Analysis results" (enabled when analysisReadout is
///   non-empty), both checked when enabled;
/// - "positionCombo" "Readout position": "Below the graph" (default), "Top left", "Top right",
///   "Bottom left", "Bottom right"; disabled when no readout is included.
/// The choices (except the path and size) are remembered in QSettings under "exportImage/".
/// Number fields accept both decimal marks (NumberSpinBox). OK renders; on failure shows a
/// message and stays open.
class ExportImageDialog : public QDialog
{
    Q_OBJECT
public:
    ExportImageDialog(QwtPlot *plot, PlotCursors *cursors, const QString &cursorReadout,
                      const QString &analysisReadout, QWidget *parent = nullptr);
    /// Older form: `readoutText` is the cursor readout, no Analysis readout.
    ExportImageDialog(QwtPlot *plot, PlotCursors *cursors, const QString &readoutText,
                      QWidget *parent = nullptr);

    /// Renders `plot` to `path` with QwtPlotRenderer (v1.3.3):
    /// - cursor markers hidden during rendering when !includeCursors; the legend left out when
    ///   !includeLegend;
    /// - title: a bold line above the plot (the image grows by its height);
    /// - readouts (cursor, then Analysis, separated by an empty line) in one framed text box:
    ///   Below = under the plot, full width, the image grows by the box height (never covers
    ///   the curves); the corner positions = inside the canvas area at that corner, as before
    ///   (TopLeft was the old behaviour).
    /// Returns false with an error on unknown extension or write failure.
    static bool render(QwtPlot *plot, PlotCursors *cursors, const QString &path,
                       const ExportOptions &options, QString *error = nullptr);

    /// Renders `plot` to `path` with QwtPlotRenderer. The cursor markers are hidden during
    /// rendering when !includeCursors (cursors may be null). A non-empty `readout` is drawn
    /// as a framed text box in the top-left corner of the canvas area. Returns false with
    /// an error on unknown extension or write failure.
    static bool render(QwtPlot *plot, PlotCursors *cursors, const QString &path,
                       const QSize &sizePx, int dpi, bool includeCursors,
                       const QString &readout, QString *error = nullptr);

    void accept() override;

private:
    // Implementation-defined; the task may change the private section freely.
    QwtPlot *m_plot = nullptr;
    PlotCursors *m_cursors = nullptr;
    QString m_readout;
    QString m_analysisReadout;
};

} // namespace mb
