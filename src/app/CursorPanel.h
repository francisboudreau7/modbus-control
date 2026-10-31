#pragma once

#include "Tag.h"

#include <QList>
#include <QMap>
#include <QWidget>

#include <optional>

class QComboBox;
class QLabel;
class QTableWidget;

namespace mb {

class Recorder;

/// Cursor readout (v1.3.3: up to six cursors, PlotCursors::kMaxCursors).
/// Top: one line per existing cursor, "C3  12.300 s", the name in the cursor's colour
/// (PlotCursors::colorOf), each with a small remove button (objectName "removeCursor<n>",
/// tooltip "Remove C<n>") that emits removeCursorRequested(n). Then the compared pair:
/// "compareFromCombo" and "compareToCombo" (items "C1".."C6" of the existing cursors; default
/// C1 -> C2; when a chosen cursor disappears the pair falls back to the two lowest existing
/// cursors), and the labels "Δt = 3.500 s", "1/Δt = 0.2857 Hz" for that pair ("—" when the pair
/// is incomplete; 1/Δt "—" when Δt == 0). No cursor: the line "No cursors. Use Cursors on the
/// Trend to place them."
/// Table: one row per plotted tag: Tag ("name [unit]", tag colour swatch), one column "Y@Cn" per
/// existing cursor in ascending order (header text in the cursor's colour), then "ΔY Ca→Cb"
/// (= Y@Cb - Y@Ca of the compared pair; omitted when the pair is incomplete). Values come from
/// Recorder::sampleAt(); NaN -> "—". Numbers use 6 significant digits with a '.' mark.
class CursorPanel : public QWidget
{
    Q_OBJECT
public:
    explicit CursorPanel(QWidget *parent = nullptr);

    void setRecorder(const Recorder *recorder); ///< not owned, may be null
    void setTags(const QList<Tag> &tags);
    /// All cursors (number -> seconds), e.g. from PlotCursors.
    void setCursors(const QMap<int, double> &cursors);
    /// Two-cursor form kept for callers and tests: C1/C2 only (removes C3..C6).
    void setCursors(std::optional<double> t1, std::optional<double> t2);
    /// The compared pair (a, b); (1, 2) by default.
    std::pair<int, int> comparedPair() const;

    /// Plain-text version of the readout, used by the image export: first line the cursor times
    /// ("C1 = 12.300 s   C2 = 15.800 s   C3 = 20.000 s"), second line "Δt (C1→C2) = 3.500 s
    /// 1/Δt = 0.2857 Hz" (omitted when the pair is incomplete), then one line per tag "name
    /// [unit]: C1 1.5   C2 2   ΔY 0.5". Empty when there is no cursor.
    QString readoutText() const;

signals:
    /// A remove button was pressed (AppController -> PlotCursors::removeCursor).
    void removeCursorRequested(int n);

public slots:
    /// Recomputes values (call after setCursors or when new data arrived).
    void refresh();

private:
    // Implementation-defined; the task may change the private section freely.
    const Recorder *m_recorder = nullptr;
    QList<Tag> m_tags;
    std::optional<double> m_t1;
    std::optional<double> m_t2;
    QMap<int, double> m_cursors;
    QLabel *m_header = nullptr;
    QTableWidget *m_table = nullptr;
};

} // namespace mb
