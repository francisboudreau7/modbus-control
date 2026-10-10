#include "SessionIO.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace mb {

namespace {

constexpr qsizetype kMaxRows = 100'000'000; // sanity limit against absurd "rows" values

QString formatName() { return QStringLiteral("modbus-control-session"); }

QByteArray encodeDoubles(const double *values, qsizetype count)
{
    QByteArray raw(count * qsizetype(sizeof(double)), Qt::Uninitialized);
    uchar *dst = reinterpret_cast<uchar *>(raw.data());
    for (qsizetype i = 0; i < count; ++i) {
        quint64 bits;
        std::memcpy(&bits, &values[i], sizeof bits);
        qToLittleEndian<quint64>(bits, dst + i * 8);
    }
    return qCompress(raw).toBase64();
}

/// Decodes a <data> string into exactly `rows` doubles. Returns false on any problem.
bool decodeDoubles(const QJsonValue &v, qsizetype rows, QList<double> &out, QString &err)
{
    if (!v.isString()) {
        err = QStringLiteral("Data is not a string");
        return false;
    }
    const auto b64 = QByteArray::fromBase64Encoding(v.toString().toLatin1(),
                                                     QByteArray::AbortOnBase64DecodingErrors);
    if (!b64) {
        err = QStringLiteral("Invalid base64 data");
        return false;
    }
    out.clear();
    if (rows == 0)
        return true;
    const QByteArray raw = qUncompress(*b64);
    if (raw.isEmpty()) {
        err = QStringLiteral("Invalid compressed data");
        return false;
    }
    if (raw.size() != rows * qsizetype(sizeof(double))) {
        err = QStringLiteral("Data size does not match the row count");
        return false;
    }
    out.resize(rows);
    const uchar *src = reinterpret_cast<const uchar *>(raw.constData());
    for (qsizetype i = 0; i < rows; ++i) {
        const quint64 bits = qFromLittleEndian<quint64>(src + i * 8);
        double d;
        std::memcpy(&d, &bits, sizeof d);
        out[i] = d;
    }
    return true;
}

QJsonValue cursorJson(const std::optional<double> &c)
{
    return c ? QJsonValue(*c) : QJsonValue(QJsonValue::Null);
}

bool readCursor(const QJsonObject &o, const QString &key, std::optional<double> &out, QString &err)
{
    const QJsonValue v = o.value(key);
    if (v.isUndefined() || v.isNull()) {
        out.reset();
        return true;
    }
    if (!v.isDouble() || !std::isfinite(v.toDouble())) {
        err = QStringLiteral("Cursor \"%1\" is neither a number nor null").arg(key);
        return false;
    }
    out = v.toDouble();
    return true;
}

bool readCursorGroup(const QJsonObject &root, const QString &group, std::optional<double> &c1,
                     std::optional<double> &c2, QString &err)
{
    const QJsonValue g = root.value(QStringLiteral("cursors")).toObject().value(group);
    if (g.isUndefined() || g.isNull()) {
        c1.reset();
        c2.reset();
        return true;
    }
    if (!g.isObject()) {
        err = QStringLiteral("Cursor group \"%1\" is not an object").arg(group);
        return false;
    }
    const QJsonObject o = g.toObject();
    return readCursor(o, QStringLiteral("c1"), c1, err) && readCursor(o, QStringLiteral("c2"), c2, err);
}

} // namespace

QByteArray sessionToJson(const Project &project, const Recorder &recording,
                         const SessionCursors &cursors)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), formatName());
    root.insert(QStringLiteral("version"), kSessionFormatVersion);
    root.insert(QStringLiteral("project"), QJsonDocument::fromJson(projectToJson(project)).object());

    const qsizetype rows = recording.size();
    QJsonObject rec;
    const QDateTime start = recording.startTime();
    rec.insert(QStringLiteral("startTime"),
               start.isValid() ? start.toOffsetFromUtc(start.offsetFromUtc()).toString(Qt::ISODateWithMs)
                               : QString());
    rec.insert(QStringLiteral("rows"), double(rows));

    QList<double> buf(rows);
    for (qsizetype i = 0; i < rows; ++i)
        buf[i] = recording.timeAt(i);
    rec.insert(QStringLiteral("times"), QString::fromLatin1(encodeDoubles(buf.constData(), rows)));

    QJsonArray columns;
    for (const TagId id : recording.tagIds()) {
        for (qsizetype i = 0; i < rows; ++i)
            buf[i] = recording.valueAt(i, id);
        QJsonObject col;
        col.insert(QStringLiteral("tagId"), double(id));
        col.insert(QStringLiteral("values"), QString::fromLatin1(encodeDoubles(buf.constData(), rows)));
        columns.append(col);
    }
    rec.insert(QStringLiteral("columns"), columns);
    root.insert(QStringLiteral("recording"), rec);

    QJsonObject trend;
    trend.insert(QStringLiteral("c1"), cursorJson(cursors.trendC1));
    trend.insert(QStringLiteral("c2"), cursorJson(cursors.trendC2));
    QJsonObject analysis;
    analysis.insert(QStringLiteral("c1"), cursorJson(cursors.analysisC1));
    analysis.insert(QStringLiteral("c2"), cursorJson(cursors.analysisC2));
    QJsonObject cur;
    cur.insert(QStringLiteral("trend"), trend);
    cur.insert(QStringLiteral("analysis"), analysis);
    root.insert(QStringLiteral("cursors"), cur);

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool sessionFromJson(const QByteArray &json, Project &project, Recorder &recording,
                     SessionCursors &cursors, QString *error)
{
    QString err;
    const auto fail = [&](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QStringLiteral("Invalid JSON: %1").arg(pe.errorString()));
    const QJsonObject root = doc.object();

    if (root.value(QStringLiteral("format")).toString() != formatName())
        return fail(QStringLiteral("Not a Modbus Control session file"));
    const QJsonValue ver = root.value(QStringLiteral("version"));
    if (!ver.isDouble())
        return fail(QStringLiteral("Missing session version"));
    if (ver.toInt() > kSessionFormatVersion)
        return fail(QStringLiteral("Unsupported session version %1").arg(ver.toInt()));

    // Project
    if (!root.value(QStringLiteral("project")).isObject())
        return fail(QStringLiteral("Missing project"));
    const auto proj = projectFromJson(
        QJsonDocument(root.value(QStringLiteral("project")).toObject()).toJson(QJsonDocument::Compact),
        &err);
    if (!proj)
        return fail(err);

    // Recording
    if (!root.value(QStringLiteral("recording")).isObject())
        return fail(QStringLiteral("Missing recording"));
    const QJsonObject rec = root.value(QStringLiteral("recording")).toObject();

    QDateTime start;
    const QJsonValue st = rec.value(QStringLiteral("startTime"));
    if (!st.isUndefined() && !st.isNull()) {
        if (!st.isString())
            return fail(QStringLiteral("Invalid start time"));
        if (!st.toString().isEmpty()) {
            start = QDateTime::fromString(st.toString(), Qt::ISODateWithMs);
            if (!start.isValid())
                return fail(QStringLiteral("Invalid start time"));
        }
    }

    const QJsonValue rv = rec.value(QStringLiteral("rows"));
    if (!rv.isDouble())
        return fail(QStringLiteral("Missing row count"));
    const double rd = rv.toDouble();
    if (!(rd >= 0) || rd > double(kMaxRows) || rd != std::floor(rd))
        return fail(QStringLiteral("Invalid row count"));
    const qsizetype rows = qsizetype(rd);

    QList<double> times;
    if (!decodeDoubles(rec.value(QStringLiteral("times")), rows, times, err))
        return fail(QStringLiteral("Times: %1").arg(err));
    for (qsizetype i = 0; i < rows; ++i) {
        if (!std::isfinite(times[i]))
            return fail(QStringLiteral("Non-finite time value"));
        if (i > 0 && times[i] < times[i - 1])
            return fail(QStringLiteral("Times are not in increasing order"));
    }

    QList<TagId> ids;
    QList<QList<double>> cols;
    const QJsonValue cv = rec.value(QStringLiteral("columns"));
    if (!cv.isUndefined() && !cv.isNull()) {
        if (!cv.isArray())
            return fail(QStringLiteral("Columns is not an array"));
        for (const QJsonValue &c : cv.toArray()) {
            const QJsonObject co = c.toObject();
            const QJsonValue idv = co.value(QStringLiteral("tagId"));
            if (!c.isObject() || !idv.isDouble() || idv.toDouble() < 0
                || idv.toDouble() > double(std::numeric_limits<TagId>::max())
                || idv.toDouble() != std::floor(idv.toDouble()))
                return fail(QStringLiteral("Invalid column tag id"));
            const TagId id = TagId(idv.toDouble());
            if (ids.contains(id))
                return fail(QStringLiteral("Duplicate column for tag %1").arg(id));
            QList<double> vals;
            if (!decodeDoubles(co.value(QStringLiteral("values")), rows, vals, err))
                return fail(QStringLiteral("Tag %1: %2").arg(id).arg(err));
            ids.append(id);
            cols.append(std::move(vals));
        }
    }

    // Cursors
    SessionCursors cur;
    if (!readCursorGroup(root, QStringLiteral("trend"), cur.trendC1, cur.trendC2, err)
        || !readCursorGroup(root, QStringLiteral("analysis"), cur.analysisC1, cur.analysisC2, err))
        return fail(err);

    // Build the recorder in a temporary, then swap everything in.
    constexpr qsizetype kContinueRows = 72000; // 2 h at 100 ms after the loaded rows
    Recorder tmp(std::max(recording.capacity(), rows + kContinueRows));
    tmp.setStartTime(start);
    const qsizetype ncols = ids.size();
    for (qsizetype r = 0; r < rows; ++r) {
        SampleBatch batch;
        batch.t = times[r];
        batch.samples.reserve(ncols);
        for (qsizetype c = 0; c < ncols; ++c) {
            const double v = cols[c][r];
            TagSample s;
            s.tagId = ids[c];
            s.value = v;
            s.quality = std::isnan(v) ? Quality::Timeout : Quality::Good;
            batch.samples.append(s);
        }
        tmp.append(batch);
    }

    project = *proj;
    recording = std::move(tmp);
    cursors = cur;
    return true;
}

bool saveSession(const QString &path, const Project &project, const Recorder &recording,
                 const SessionCursors &cursors, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QByteArray data = sessionToJson(project, recording, cursors);
    if (f.write(data) != data.size() || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

bool loadSession(const QString &path, Project &project, Recorder &recording,
                 SessionCursors &cursors, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return sessionFromJson(f.readAll(), project, recording, cursors, error);
}

} // namespace mb
