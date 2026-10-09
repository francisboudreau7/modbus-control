#pragma once

#include "ProjectIO.h"
#include "Recorder.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace mb {

inline constexpr int kSessionFormatVersion = 1;

/// Cursor positions of both tabs (seconds in the recording's time base; nullopt = not set).
struct SessionCursors {
    std::optional<double> trendC1;
    std::optional<double> trendC2;
    std::optional<double> analysisC1;
    std::optional<double> analysisC2;

    friend bool operator==(const SessionCursors &, const SessionCursors &) = default;
};

/// Contents of a .mbsession file (JSON, UTF-8): a project, its recording and the cursors.
///
/// {
///   "format": "modbus-control-session", "version": 1,
///   "project": { ...the object written by projectToJson()... },
///   "recording": {
///     "startTime": "2026-10-09T14:03:12.250-04:00",   // Recorder::startTime(), ISO-8601 with ms
///     "rows": 1234,
///     "times": "<data>",
///     "columns": [ { "tagId": 7, "values": "<data>" }, ... ]   // Recorder::tagIds() order
///   },
///   "cursors": { "trend": { "c1": 12.5, "c2": null }, "analysis": { "c1": null, "c2": null } }
/// }
///
/// <data> = base64 of qCompress() of `rows` IEEE-754 float64 values, little endian. Values
/// keep NaN (missing or non-Good samples). An empty recording has rows 0 and no columns.
/// A column may name a tag that is not in the project (it was deleted after recording); it
/// is still loaded.
///
/// Loading fails (false + error) on invalid JSON, wrong "format", version >
/// kSessionFormatVersion, an invalid project (projectFromJson's rules), bad base64 or
/// compressed data, a decoded size different from rows * 8, decreasing times, duplicate
/// column tag ids, or cursor values that are neither numbers nor null.
QByteArray sessionToJson(const Project &project, const Recorder &recording,
                         const SessionCursors &cursors);

/// On success replaces `recording` completely: clear(), capacity raised to at least `rows`
/// (Recorder::setCapacity, never lowered), start time set, then one append() per row with a
/// sample per column (quality Good for finite values, Timeout for NaN, so NaN survives).
/// On failure `project`, `recording` and `cursors` are unchanged.
bool sessionFromJson(const QByteArray &json, Project &project, Recorder &recording,
                     SessionCursors &cursors, QString *error = nullptr);

/// File helpers (QSaveFile for writing).
bool saveSession(const QString &path, const Project &project, const Recorder &recording,
                 const SessionCursors &cursors, QString *error = nullptr);
bool loadSession(const QString &path, Project &project, Recorder &recording,
                 SessionCursors &cursors, QString *error = nullptr);

} // namespace mb
