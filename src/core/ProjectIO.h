#pragma once

#include "ModbusConnection.h"
#include "StepAnalysis.h"
#include "Tag.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace mb {

inline constexpr int kProjectFormatVersion = 1;

/// Contents of a .mbproj file (JSON, UTF-8).
///
/// {
///   "format": "modbus-control-project", "version": 1,
///   "connection": { "host": "...", "port": 502, "unitId": 1, "timeoutMs": 1000, "pollPeriodMs": 500 },
///   "nextTagId": 4,
///   "tags": [ { "id": 1, "name": "...", "area": "holding"|"input", "address": 0,
///               "type": "int16"|"uint16"|"int32"|"uint32"|"float32",
///               "byteOrder": "ABCD"|"CDAB"|"BADC"|"DCBA", "gain": 1.0, "offset": 0.0,
///               "unit": "", "color": "#RRGGBB", "plotted": true, "writable": false,
///               "axis": "left"|"right", "quickA": 0, "quickB": 1000 }, ... ],
///   "analysis": { "percents": [0, 63.2], "reference": "cursor"|"mean", "referenceWindowS": 1,
///                 "smoothingS": 0, "modelPercents": [0, 63.2], "method": "0-63.2",
///                 "pvTag": 1, "setpointTags": [1], "deltaCoTag": 0, "deltaCo": 0,
///                 "detailedLabels": false, "showModel": false, "guideLines": true,
///                 "pvRange": null, "coRange": [0, 100], "tuningRule": "zn", "lambdaS": 0,
///                 "timeUnit": "s" }
/// }
///
/// Older files may contain "connection.singleRegisterWrites" (the removed FC06-only option): it
/// must still be a boolean when present, is otherwise ignored, and is never written.
/// "byteOrder" only matters for the 32-bit types (2 registers); it is written for every tag.
/// Missing fields take the defaults of ConnectionSettings / Tag (byteOrder defaults to
/// "ABCD"). The change from format version 1 that added the 32-bit types and "byteOrder"
/// is additive, so the version stays 1. nextTagId is always at least max(tag id) + 1.
///
/// Ranges (values outside them are rejected when loading): port 1..65535, unitId 0..247,
/// timeoutMs 100..60000, pollPeriodMs 100..60000, tag id >= 1, address 0..65535. A 32-bit
/// tag must also fit: address + 2 <= 65536, so address 65535 is only valid for 16-bit types.
///
/// "analyzed" (default false) and "analysis" (default AnalysisSettings{}) were added in v1.1;
/// both are optional and additive, so the version stays 1. Every "analysis" key is
/// optional. Ranges: percents = array of 0..20 numbers each in [0, 100]; reference "cursor"
/// (CursorValue) or "mean" (WindowMean); referenceWindowS in (0, 3600]; smoothingS in
/// [0, 3600]; modelPercents = array of exactly 2 numbers a < b, both in [0, 100);
/// deltaCoTag integer >= 0 (may name a tag that no longer exists); deltaCo any finite
/// number; detailedLabels, showModel, guideLines booleans. Anything else is rejected.
///
/// Added in v1.2 (optional, additive): per tag "axis" ("left" default, "right") and
/// "quickA"/"quickB" (finite numbers; a missing key or null = no value; written only when
/// set).
///
/// Added in v1.2.1 (optional, additive): per tag "writeMin"/"writeMax" (finite numbers; missing
/// or null = no limit; written only when set; writeMin > writeMax is rejected).
///
/// Added in v1.2.2 (optional, additive): top-level "hiddenColumns", an array of tag-table column
/// keys (see TagTableModel::columnKey) hidden in the tag table. Missing -> compactHiddenColumns().
/// Always written. Entries must be strings (anything else is rejected); unknown keys are kept as
/// they are (the table ignores them), duplicates are dropped.
///
/// Added in v1.2.6 (optional, additive): "analysis" keys "pvTag" (integer >= 0), "setpointTags"
/// (array of integers >= 1), "method" ("0-63.2"|"smith"|"sundaresan"|"custom"), "pvRange" (null or
/// an array of 2 finite numbers [min, max]), "coRange" (array of 2 finite numbers; default
/// [0, 100]). min == max is accepted in both (the UI can produce a zero-width range): "pvRange"
/// then reads as not set (and is written as null for unset or equal ends); "coRange" keeps the
/// values and the UI treats a zero span as not set. "tuningRule" ("zn"|"cohen-coon"|"imc"|"simc"),
/// "lambdaS" (finite, >= 0), "timeUnit" ("s"|"min"); all written. Older files: without "method"
/// -> ZeroTo632 when modelPercents is [0, 63.2] (or missing), else Custom; without "pvTag" -> the
/// id of the first tag with "analyzed": true whose id is not "deltaCoTag" (the first analysed
/// tag if all are the CO; 0 if none); without "setpointTags" -> the ids of
/// every tag with "analyzed": true. "showModel" missing -> true (the new default). The per-tag
/// "analyzed" is still read (it must be a boolean) but no longer written.
///
/// Added in v1.2.3 (optional, additive): per tag "encodingVerified" (boolean, default true; written
/// only when false) and "confirmed16Bit" (boolean, default false; written only when true). Any
/// other JSON type is rejected.
/// Column keys hidden by the tag table's compact view: "raw", "gain", "offset", "byteOrder",
/// "axis", "color", "writeMin", "writeMax", "quickA", "quickB" (in this order).
QStringList compactHiddenColumns();

struct Project {
    ConnectionSettings connection;
    QList<Tag> tags;
    TagId nextTagId = 1;
    /// 0 or 1: how register numbers are shown and entered in the UI (1 = numbers as printed
    /// in device manuals such as the ABB CM50's). Tag addresses are always stored zero-based.
    /// JSON top-level key "addressBase" (optional, default 0; other values are rejected).
    int addressBase = 0;
    /// Analysis tab settings (JSON top-level key "analysis").
    AnalysisSettings analysis;
    /// Tag-table columns hidden in this project (column keys; JSON "hiddenColumns").
    QStringList hiddenColumns = compactHiddenColumns();

    friend bool operator==(const Project &, const Project &) = default;
};

QByteArray projectToJson(const Project &project);
/// Fails (nullopt + error) on invalid JSON, wrong "format", version > kProjectFormatVersion,
/// unknown enum strings, or duplicate tag ids.
std::optional<Project> projectFromJson(const QByteArray &json, QString *error = nullptr);

bool saveProject(const QString &path, const Project &project, QString *error = nullptr);
std::optional<Project> loadProject(const QString &path, QString *error = nullptr);

} // namespace mb
