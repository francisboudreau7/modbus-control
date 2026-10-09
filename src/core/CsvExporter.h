#pragma once

#include "Recorder.h"

#include <QList>
#include <QString>

class QIODevice;

namespace mb {

struct CsvColumn {
    TagId tagId = 0;
    QString header; ///< e.g. "Sine [V]"
};

struct CsvOptions {
    QChar delimiter = u',';  ///< ',' or ';'
    bool useRange = false;   ///< export only rows with t in [t1, t2]
    double t1 = 0.0;
    double t2 = 0.0;
};

/// Writes a wide-format CSV, UTF-8, CRLF line endings:
///   timestamp,elapsed_s,<header 1>,<header 2>,...
///   2026-10-09T14:03:12.250-04:00,12.250,1.5,,42
/// - timestamp: Recorder::startTime() + t, local time, ISO-8601 with milliseconds and offset
/// - elapsed_s: t with exactly 3 decimals
/// - values: shortest round-trip representation, '.' decimal separator regardless of
///   locale; NaN -> empty cell
/// - a header containing the delimiter, '"', CR or LF is quoted RFC-4180 style
/// Returns false (with error) if the device is not writable or a write fails.
bool exportCsv(QIODevice &out, const Recorder &recorder, const QList<CsvColumn> &columns,
               const CsvOptions &options, QString *error = nullptr);

} // namespace mb
