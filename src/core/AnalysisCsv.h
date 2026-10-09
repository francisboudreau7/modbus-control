#pragma once

#include "StepAnalysis.h"

#include <QHash>
#include <QList>
#include <QString>

class QIODevice;

namespace mb {

/// Writes the Analysis tab's results as CSV, UTF-8, CRLF line endings, two sections
/// separated by one empty line:
///
///   tag,percent,target,measured,time_s,time_from_c1_s
///   Lag [degC],0,10,10.02,12.35,2.35
///   Lag [degC],63.2,22.64,22.6,14.36,4.36
///   <empty line>
///   tag,c1_s,c2_s,y0,y100,delta_pv,delta_co,step_time_s,gain_k,dead_time_l_s,time_constant_t_s,note
///   Lag [degC],10,30,10,30,20,10,10,2,2.35,2.01,
///
/// - One setpoint row per SetpointResult of every result, results in the given order; one
///   model row per result.
/// - tag: tagHeaders.value(tagId) (e.g. "name [unit]"), or "Tag <id>" when missing.
/// - Numbers: shortest round-trip representation, '.' decimal separator regardless of
///   locale. NaN, a missing optional, or a setpoint that was not found -> empty cell
///   (percent and target are still written for a not-found setpoint; target only if finite).
/// - time_from_c1_s = time - c1 (the result's t1).
/// - For an invalid result: the model row has tag, c1_s, c2_s and note = result.error, the
///   other cells empty; it has no setpoint rows.
/// - note is empty for valid results. Fields containing the delimiter, '"', CR or LF are
///   quoted RFC-4180 style (same rule as exportCsv()).
/// Returns false (with error) if the device is not writable or a write fails.
bool exportAnalysisCsv(QIODevice &out, const QList<StepResult> &results,
                       const QHash<TagId, QString> &tagHeaders, QChar delimiter = u',',
                       QString *error = nullptr);

} // namespace mb
