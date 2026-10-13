#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace mb::rich {

// Building blocks for message box texts (Qt rich text), so every prompt has the same layout: a
// bold headline, the facts as label/value rows or a grid, bullet lists instead of lists inside a
// sentence, and technical details in a small note. Message boxes showing these texts must use
// Qt::RichText. Arguments named `plain` are escaped; arguments named `rich` are inserted as they
// are (build them from escaped() and the functions here).

/// HTML-escaped plain text.
QString escaped(const QString &plain);
/// "<b>…</b>" around escaped text, for emphasis inside a row value or a bullet.
QString bold(const QString &plain);

/// "<p><b>…</b></p>": the first line of a prompt, saying what it is about.
QString headline(const QString &plain);
/// "<p>…</p>".
QString paragraph(const QString &plain);
QString paragraphRich(const QString &rich);
/// "<p><small>…</small></p>": technical details (registers, raw values) under the main content.
QString note(const QString &plain);

/// One label/value row of rows(); the value is shown in bold when `emphasize` is set.
struct Row {
    QString label; ///< plain, e.g. "New value:"
    QString value; ///< plain, e.g. "52.3 °C"
    bool emphasize = false;
};
/// A two-column table without borders: labels left, values left-aligned next to them.
QString rows(const QList<Row> &rows);

/// A table with a bold header row; every cell is plain text. Cells of `emphasizeColumn` (if >= 0)
/// are shown in bold.
QString grid(const QStringList &header, const QList<QStringList> &cells, int emphasizeColumn = -1);

/// "<ul><li>…</li>…</ul>" from rich items.
QString bulletsRich(const QStringList &rich);
/// Same from plain items.
QString bullets(const QStringList &plain);

} // namespace mb::rich
