#pragma once

#include "ModbusConnection.h"
#include "Tag.h"

#include <QByteArray>
#include <QList>
#include <QString>

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
///               "type": "int16"|"uint16", "gain": 1.0, "offset": 0.0, "unit": "",
///               "color": "#RRGGBB", "plotted": true, "writable": false }, ... ]
/// }
///
/// Missing fields take the defaults of ConnectionSettings / Tag. nextTagId is always
/// at least max(tag id) + 1.
///
/// Ranges (values outside them are rejected when loading): port 1..65535, unitId 0..255,
/// timeoutMs 100..60000, pollPeriodMs 100..60000, tag id >= 1, address 0..65535.
struct Project {
    ConnectionSettings connection;
    QList<Tag> tags;
    TagId nextTagId = 1;

    friend bool operator==(const Project &, const Project &) = default;
};

QByteArray projectToJson(const Project &project);
/// Fails (nullopt + error) on invalid JSON, wrong "format", version > kProjectFormatVersion,
/// unknown enum strings, or duplicate tag ids.
std::optional<Project> projectFromJson(const QByteArray &json, QString *error = nullptr);

bool saveProject(const QString &path, const Project &project, QString *error = nullptr);
std::optional<Project> loadProject(const QString &path, QString *error = nullptr);

} // namespace mb
