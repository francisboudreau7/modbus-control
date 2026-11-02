#pragma once

#include "Tag.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>

namespace mb {

/// One write sent to the device, as recorded by the write log (v1.3.3, PLAN.md "v1.3.3 Classroom
/// safety"). A multi-tag (selection) write gives one entry per tag, all with the same function
/// code and result. Every text is ready to show; nothing is reformatted by the log.
struct WriteLogEntry {
    QDateTime time;          ///< when the request was sent (local time)
    TagId tagId = 0;
    QString tagName;
    quint16 address = 0;     ///< protocol (zero-based) address of the tag's first register
    int count = 0;           ///< registers of the tag (1 or 2)
    QString registers;       ///< with the address base: "HR 5009-5010", "HR 100", "CO 3" (v1.3.4)
    /// As PollWorker sends it: 6 (one register) or 16 (FC16); v1.3.4 coils: 5 (one) or 15 (FC15).
    /// For coils the raw field is "0" / "1" and old/new values are "OFF" / "ON".
    int functionCode = 0;
    QString oldValue;        ///< scaled value with unit before the write ("50 %"), or "unknown"
    QString newValue;        ///< scaled value with unit written ("-25 %")
    QString raw;             ///< registers sent in hex, space-separated: "0xC1C8 0x0000"
    QString source;          ///< "table", "selection" or "quick"
    bool ok = false;         ///< the device accepted the write
    QString result;          ///< "OK", "Exception 2: Illegal data address", "Timeout", ...
    QString server;          ///< "192.168.1.20:502 unit 1"

    friend bool operator==(const WriteLogEntry &, const WriteLogEntry &) = default;
};

/// Header of the CSV file, in this order: "time", "tag", "tag_id", "registers", "function_code",
/// "old_value", "new_value", "raw", "source", "result", "server".
QStringList writeLogHeader();

/// One CSV line (no line ending) for `entry`, fields in writeLogHeader() order, separated by
/// `delimiter`:
/// - time: ISO-8601 local time with milliseconds and offset, like CsvExporter
///   ("2026-10-31T14:03:12.250-04:00");
/// - tag_id and function_code as integers; the other fields as given;
/// - a field containing the delimiter, '"', CR or LF is quoted RFC-4180 style ('"' doubled).
QString writeLogCsvLine(const WriteLogEntry &entry, QChar delimiter = u',');

/// "writes-YYYY-MM-DD.csv" for `date`.
QString writeLogFileName(const QDate &date);

/// Appends entries to one CSV file per day in a folder.
/// - The file of entry.time.date() is used (writeLogFileName); the folder is created on demand.
/// - A new (or empty) file starts with a UTF-8 BOM and the header line; lines end with CRLF.
/// - Each append opens, writes, flushes and closes the file, so nothing is lost when the app
///   stops, and other programs (Excel) can open the file between writes.
/// - append() returns false (with error naming the path and the reason) when the folder can't be
///   created or the file can't be written; the caller decides how to report it.
class WriteLogFile
{
public:
    explicit WriteLogFile(const QString &folder = defaultFolder());

    QString folder() const;
    void setFolder(const QString &folder);
    /// Full path of the file for `date`.
    QString pathFor(const QDate &date) const;
    bool append(const WriteLogEntry &entry, QString *error = nullptr);

    /// "<Documents>/Modbus Control/Write logs" (QStandardPaths::DocumentsLocation).
    static QString defaultFolder();

private:
    QString m_folder;
};

} // namespace mb
