#include "MetaTypes.h"
#include "ModbusConnection.h"
#include "PollWorker.h"
#include "SessionIO.h"
#include "Recorder.h"
#include "StepAnalysis.h"
#include "TagCodec.h"

#include <QElapsedTimer>
#include <QFile>
#include <QMetaObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace mb;

namespace {

/// Starts modbus_test_server on an OS-chosen (or given) port and waits for the LISTENING line.
class ServerProcess
{
public:
    ~ServerProcess() { stop(); }

    bool start(const QStringList &extraArgs = {}, quint16 port = 0)
    {
        stop();
        m_log.clear();
        m_buffer.clear();
        m_port = 0;
        QStringList args{QStringLiteral("--host"), QStringLiteral("127.0.0.1"), QStringLiteral("--port"),
                         QString::number(port)};
        args += extraArgs;
        m_proc.setProcessChannelMode(QProcess::MergedChannels);
        m_proc.start(QStringLiteral(MC_TEST_SERVER_PATH), args);
        if (!m_proc.waitForStarted(10000)) {
            m_log += QStringLiteral("failed to start: %1\n").arg(m_proc.errorString());
            return false;
        }
        static const QRegularExpression re(QStringLiteral("^LISTENING (\\d+)\\s*$"),
                                           QRegularExpression::MultilineOption);
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 10000) {
            m_proc.waitForReadyRead(200);
            const QString chunk = QString::fromLocal8Bit(m_proc.readAll());
            m_log += chunk;
            m_buffer += chunk;
            const auto m = re.match(m_buffer);
            if (m.hasMatch()) {
                m_port = static_cast<quint16>(m.captured(1).toUInt());
                return true;
            }
            if (m_proc.state() == QProcess::NotRunning)
                break;
        }
        return false;
    }

    void stop()
    {
        if (m_proc.state() != QProcess::NotRunning) {
            m_proc.kill();
            m_proc.waitForFinished(5000);
        }
        drain();
    }

    quint16 port() const { return m_port; }
    QString log()
    {
        drain();
        return m_log;
    }

private:
    void drain()
    {
        if (m_proc.isOpen())
            m_log += QString::fromLocal8Bit(m_proc.readAll());
    }

    QProcess m_proc;
    QString m_log;
    QString m_buffer;
    quint16 m_port = 0;
};

ConnectionSettings settingsFor(quint16 port, int timeoutMs = 1000, int unit = 1, int period = 200)
{
    ConnectionSettings s;
    s.host = QStringLiteral("127.0.0.1");
    s.port = port;
    s.unitId = unit;
    s.timeoutMs = timeoutMs;
    s.pollPeriodMs = period;
    return s;
}

Tag makeTag(TagId id, Area area, quint16 address, ValueType type = ValueType::UInt16)
{
    Tag t;
    t.id = id;
    t.name = QStringLiteral("tag%1").arg(id);
    t.area = area;
    t.address = address;
    t.type = type;
    t.writable = area == Area::HoldingRegister;
    return t;
}

struct ReadRecord
{
    quint64 id = 0;
    ModbusResult result;
    QList<quint16> values;
};

/// PollWorker on its own thread, with all emitted signals recorded on the main thread.
class WorkerHarness : public QObject
{
    Q_OBJECT
public:
    WorkerHarness()
    {
        m_worker = new PollWorker;
        m_worker->moveToThread(&m_thread);
        connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        connect(m_worker, &PollWorker::stateChanged, this,
                [this](ConnectionState s, const QString &) { states.append(s); });
        connect(m_worker, &PollWorker::samplesReady, this,
                [this](const SampleBatch &b) { batches.append(b); });
        connect(m_worker, &PollWorker::writeFinished, this,
                [this](quint64 id, const ModbusResult &r) { writes.insert(id, r); });
        connect(m_worker, &PollWorker::readFinished, this,
                [this](quint64 id, const ModbusResult &r, const QList<quint16> &v) {
                    reads.append({id, r, v});
                });
        m_thread.start();
    }

    ~WorkerHarness() override
    {
        // Close the connection on the worker thread before it ends.
        QMetaObject::invokeMethod(m_worker, &PollWorker::disconnectFrom, Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait(5000);
    }

    void connectTo(const ConnectionSettings &s)
    {
        QMetaObject::invokeMethod(m_worker, [w = m_worker, s] { w->connectTo(s); }, Qt::QueuedConnection);
    }
    void disconnectFrom()
    {
        QMetaObject::invokeMethod(m_worker, &PollWorker::disconnectFrom, Qt::QueuedConnection);
    }
    void setTags(const QList<Tag> &tags)
    {
        QMetaObject::invokeMethod(m_worker, [w = m_worker, tags] { w->setTags(tags); },
                                  Qt::QueuedConnection);
    }
    void write(quint64 id, quint16 address, const QList<quint16> &values)
    {
        QMetaObject::invokeMethod(m_worker, [w = m_worker, id, address, values] {
            w->writeRegisters(id, address, values);
        }, Qt::QueuedConnection);
    }

    void read(quint64 id, Area area, quint16 address, int count)
    {
        QMetaObject::invokeMethod(m_worker, [w = m_worker, id, area, address, count] {
            w->readRegisters(id, area, address, count);
        }, Qt::QueuedConnection);
    }
    /// Queues a read and a disconnect in one worker-thread call, so the read is still pending.
    void readThenDisconnect(quint64 id, Area area, quint16 address, int count)
    {
        QMetaObject::invokeMethod(m_worker, [w = m_worker, id, area, address, count] {
            w->readRegisters(id, area, address, count);
            w->disconnectFrom();
        }, Qt::QueuedConnection);
    }
    /// How many readFinished were recorded for `id`.
    int readCount(quint64 id) const
    {
        int n = 0;
        for (const ReadRecord &r : reads)
            n += r.id == id;
        return n;
    }
    const ReadRecord *readOf(quint64 id) const
    {
        for (const ReadRecord &r : reads)
            if (r.id == id)
                return &r;
        return nullptr;
    }

    bool hasState(ConnectionState s) const { return states.contains(s); }
    ConnectionState lastState() const { return states.isEmpty() ? ConnectionState::Disconnected : states.last(); }

    /// Sample of `tag` within a batch.
    static const TagSample *sampleOf(const SampleBatch &b, TagId tag)
    {
        for (const TagSample &s : b.samples)
            if (s.tagId == tag)
                return &s;
        return nullptr;
    }

    QList<ConnectionState> states;
    QList<SampleBatch> batches;
    QHash<quint64, ModbusResult> writes;
    QList<ReadRecord> reads;

private:
    QThread m_thread;
    PollWorker *m_worker = nullptr;
};

/// Result of running modbus_test_server to completion (for CLI tests that exit on their own).
struct CliRun
{
    bool finished = false;
    int exitCode = -1;
    QString out;
    QString err;
};

CliRun runServerCli(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral(MC_TEST_SERVER_PATH), args);
    CliRun r;
    if (!p.waitForStarted(10000) || !p.waitForFinished(10000)) {
        p.kill();
        p.waitForFinished(5000);
        return r;
    }
    r.finished = true;
    r.exitCode = p.exitCode();
    r.out = QString::fromLocal8Bit(p.readAllStandardOutput());
    r.err = QString::fromLocal8Bit(p.readAllStandardError());
    return r;
}

bool writeTextFile(const QString &path, const QByteArray &content)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(content) == content.size();
}

} // namespace

class TstIntegration : public QObject
{
    Q_OBJECT

private slots:
    // A device without FC16 answers a 32-bit write with exception 1: the message says what to do.
    void fc16IllegalFunctionHint()
    {
        mb::ModbusResult r;
        r.error = mb::ModbusError::Exception;
        r.exceptionCode = 1;
        r.message = QStringLiteral("Modbus exception 1: Illegal function");
        const mb::ModbusResult hinted = mb::PollWorker::withFc16Hint(r);
        QCOMPARE(hinted.error, mb::ModbusError::Exception);
        QVERIFY(hinted.message.startsWith(r.message));
        QVERIFY(hinted.message.contains(QStringLiteral("tick FC06 only")));

        r.exceptionCode = 2; // illegal data address: no hint
        QCOMPARE(mb::PollWorker::withFc16Hint(r).message, r.message);
        mb::ModbusResult timeout;
        timeout.error = mb::ModbusError::Timeout;
        timeout.message = QStringLiteral("Timeout");
        QCOMPARE(mb::PollWorker::withFc16Hint(timeout).message, timeout.message);
    }

    void initTestCase() { mb::registerMetaTypes(); }

    void init() { m_server = std::make_unique<ServerProcess>(); }

    void cleanup()
    {
        m_harness.reset();
        if (m_server) {
            m_server->stop();
            if (QTest::currentTestFailed())
                qWarning().noquote() << "--- server output ---\n" << m_server->log();
            m_server.reset();
        }
    }

    // ---- ModbusConnection ------------------------------------------------------------------

    void conn_counterIncreases()
    {
        QVERIFY(m_server->start());
        ModbusConnection c;
        QVERIFY2(c.open(settingsFor(m_server->port())).ok(), "open failed");
        QList<quint16> a, b;
        QVERIFY(c.readRegisters(Area::InputRegister, 0, 6, a).ok());
        QCOMPARE(a.size(), 6);
        QTest::qWait(300);
        QVERIFY(c.readRegisters(Area::InputRegister, 0, 6, b).ok());
        QCOMPARE(b.size(), 6);
        const quint16 delta = static_cast<quint16>(b[2] - a[2]);
        QVERIFY2(delta > 0 && delta < 100, qPrintable(QString::number(delta)));
    }

    void conn_writeSingleReadBack()
    {
        QVERIFY(m_server->start());
        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port())).ok());
        QVERIFY(c.writeSingle(5, 1234).ok());
        QList<quint16> v;
        QVERIFY(c.readRegisters(Area::HoldingRegister, 5, 1, v).ok());
        QCOMPARE(v.value(0), quint16(1234));
    }

    void conn_writeMultipleReadBack()
    {
        QVERIFY(m_server->start());
        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port())).ok());
        const QList<quint16> values{11, 22, 33, 44, 55};
        QVERIFY(c.writeMultiple(10, values).ok());
        QList<quint16> v;
        QVERIFY(c.readRegisters(Area::HoldingRegister, 10, 5, v).ok());
        QCOMPARE(v, values);
    }

    void conn_negativeRoundTrip()
    {
        QVERIFY(m_server->start());
        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port())).ok());
        QVERIFY(c.writeSingle(20, 0xFC18).ok()); // -1000 as int16
        QList<quint16> v;
        QVERIFY(c.readRegisters(Area::HoldingRegister, 20, 1, v).ok());
        QCOMPARE(v.value(0), quint16(0xFC18));
        QCOMPARE(static_cast<qint16>(v.value(0)), qint16(-1000));
    }

    // Default config: CM50-like initial values, HR110-111 float32 CDAB 52.3, HR120-121 int32 ABCD 100.
    void conn_defaultHoldingValues()
    {
        QVERIFY(m_server->start());
        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port())).ok());
        QList<quint16> v;
        QVERIFY(c.readRegisters(Area::HoldingRegister, 110, 2, v).ok());
        QCOMPARE(v, (QList<quint16>{0x3333, 0x4251}));
        QVERIFY(c.readRegisters(Area::HoldingRegister, 120, 2, v).ok());
        QCOMPARE(v, (QList<quint16>{0x0000, 0x0064}));
    }

    // "holding" values from a config file are written at start and stay writable.
    void conn_configHoldingValuesWritable()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("server.json"));
        QVERIFY(writeTextFile(path, R"({
  "holdingCount": 16, "signals": [],
  "holding": [
    {"address": 5, "value": 65535, "format": "uint16"},
    {"address": 8, "value": 305419896, "format": "uint32", "byteOrder": "CDAB"}
  ]
})"));
        QVERIFY(m_server->start({QStringLiteral("--config"), path}));
        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port())).ok());
        QList<quint16> v;
        QVERIFY(c.readRegisters(Area::HoldingRegister, 5, 1, v).ok());
        QCOMPARE(v.value(0), quint16(0xFFFF));
        QVERIFY(c.readRegisters(Area::HoldingRegister, 8, 2, v).ok());
        QCOMPARE(v, (QList<quint16>{0x5678, 0x1234})); // 0x12345678 CDAB
        QVERIFY(c.writeSingle(5, 1234).ok());
        QVERIFY(c.readRegisters(Area::HoldingRegister, 5, 1, v).ok());
        QCOMPARE(v.value(0), quint16(1234));
    }

    void conn_exceptionHoldingOutOfMap()
    {
        QVERIFY(m_server->start());
        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port())).ok());
        QList<quint16> v;
        const ModbusResult r = c.readRegisters(Area::HoldingRegister, 200, 1, v);
        QCOMPARE(r.error, ModbusError::Exception);
        QCOMPARE(r.exceptionCode, 2);
        // The connection stays usable after an exception.
        QVERIFY(c.readRegisters(Area::HoldingRegister, 0, 1, v).ok());
    }

    void conn_exceptionInputOutOfMap()
    {
        QVERIFY(m_server->start());
        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port())).ok());
        QList<quint16> v;
        const ModbusResult r = c.readRegisters(Area::InputRegister, 64, 1, v);
        QCOMPARE(r.error, ModbusError::Exception);
        QCOMPARE(r.exceptionCode, 2);
    }

    void conn_timeoutWithDelay()
    {
        QVERIFY(m_server->start({QStringLiteral("--delay"), QStringLiteral("1500")}));
        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port(), 500)).ok());
        QList<quint16> v;
        QElapsedTimer t;
        t.start();
        const ModbusResult r = c.readRegisters(Area::InputRegister, 0, 1, v);
        QCOMPARE(r.error, ModbusError::Timeout);
        QVERIFY2(t.elapsed() < 1400, qPrintable(QString::number(t.elapsed())));
        c.flush();
    }

    void conn_dropFails()
    {
        QVERIFY(m_server->start({QStringLiteral("--drop"), QStringLiteral("100")}));
        ModbusConnection c;
        const ModbusResult o = c.open(settingsFor(m_server->port(), 500));
        QVERIFY2(o.ok(), "TCP connect should succeed even when the server drops requests");
        QList<quint16> v;
        const ModbusResult r = c.readRegisters(Area::InputRegister, 0, 1, v);
        qInfo() << "--drop 100: read result" << int(r.error) << "(3=Timeout, 5=ConnectionLost)" << r.message;
        QVERIFY2(r.error == ModbusError::ConnectionLost || r.error == ModbusError::Timeout,
                 qPrintable(QStringLiteral("unexpected error %1: %2").arg(int(r.error)).arg(r.message)));
    }

    void conn_connectFailed()
    {
        // Find a port that nothing listens on: start a server, learn its port, stop it.
        QVERIFY(m_server->start());
        const quint16 port = m_server->port();
        m_server->stop();
        ModbusConnection c;
        const ModbusResult r = c.open(settingsFor(port, 500));
        QCOMPARE(r.error, ModbusError::ConnectFailed);
        QVERIFY(!c.isOpen());
        QList<quint16> v;
        QCOMPARE(c.readRegisters(Area::InputRegister, 0, 1, v).error, ModbusError::NotConnected);
    }

    void conn_wrongUnitId()
    {
        QVERIFY(m_server->start({QStringLiteral("--unit"), QStringLiteral("1")}));
        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port(), 500, 5)).ok());
        QList<quint16> v;
        const ModbusResult r = c.readRegisters(Area::InputRegister, 0, 1, v);
        qInfo() << "wrong unit id: read result" << int(r.error) << r.exceptionCode << r.message;
        // Observed: the libmodbus TCP server does not filter on the unit id (only RTU does),
        // so a request for a foreign unit id is answered normally.
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(v.size(), 1);
    }

    // ---- PollWorker ------------------------------------------------------------------------

    void worker_connectsAndPollsAtPeriod()
    {
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        m_harness->setTags({makeTag(1, Area::InputRegister, 2)});
        m_harness->connectTo(settingsFor(m_server->port(), 1000, 1, 200));
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->hasState(ConnectionState::Connected), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!m_harness->batches.isEmpty(), 3000);
        const int start = m_harness->batches.size();
        QTest::qWait(1000);
        const int n = m_harness->batches.size() - start;
        QVERIFY2(n >= 3 && n <= 8, qPrintable(QStringLiteral("batches in 1 s: %1").arg(n)));
        for (const SampleBatch &b : std::as_const(m_harness->batches))
            QVERIFY(b.samples.value(0).quality == Quality::Good);
    }

    void worker_counterStrictlyIncreases()
    {
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        m_harness->setTags({makeTag(1, Area::InputRegister, 2)});
        m_harness->connectTo(settingsFor(m_server->port(), 1000, 1, 200));
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->batches.size() >= 5, 5000);
        for (int i = 1; i < 5; ++i) {
            const TagSample &a = m_harness->batches.at(i - 1).samples.at(0);
            const TagSample &b = m_harness->batches.at(i).samples.at(0);
            QVERIFY(a.quality == Quality::Good);
            QVERIFY(b.quality == Quality::Good);
            QVERIFY2(b.raw != a.raw, "counter must change between 200 ms cycles");
            QVERIFY(m_harness->batches.at(i).t > m_harness->batches.at(i - 1).t);
        }
    }

    void worker_writeAffectsLag()
    {
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        m_harness->setTags({makeTag(1, Area::HoldingRegister, 100), makeTag(2, Area::InputRegister, 10),
                            makeTag(3, Area::InputRegister, 2)});
        m_harness->connectTo(settingsFor(m_server->port(), 1000, 1, 200));
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->batches.size() >= 2, 5000);
        m_harness->write(77, 100, {1000});
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->writes.contains(77), 3000);
        QVERIFY2(m_harness->writes.value(77).ok(), qPrintable(m_harness->writes.value(77).message));
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                const SampleBatch &b = m_harness->batches.last();
                const TagSample *hr = WorkerHarness::sampleOf(b, 1);
                const TagSample *lag = WorkerHarness::sampleOf(b, 2);
                return hr && lag && hr->quality == Quality::Good && hr->raw == 1000
                    && lag->quality == Quality::Good && lag->raw > 500;
            }(),
            4000);
    }

    void analysis_lagStepResponse()
    {
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        const TagId coId = 1;
        const TagId pvId = 2;
        m_harness->setTags({makeTag(coId, Area::HoldingRegister, 100, ValueType::Int16),
                            makeTag(pvId, Area::InputRegister, 10, ValueType::Int16)});
        m_harness->connectTo(settingsFor(m_server->port(), 1000, 1, 100));
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->batches.size() >= 2, 5000);
        QTest::qWait(2000); // steady 0

        const double tBeforeStep = m_harness->batches.last().t;
        m_harness->write(78, 100, {1000});
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->writes.contains(78), 3000);
        QVERIFY2(m_harness->writes.value(78).ok(), qPrintable(m_harness->writes.value(78).message));
        QTest::qWait(10000);
        const double tEnd = m_harness->batches.last().t;

        Recorder recorder;
        recorder.setStartTime(QDateTime::currentDateTime());
        for (const SampleBatch &b : std::as_const(m_harness->batches))
            recorder.append(b);

        AnalysisSettings settings;
        settings.percents = {0.0, 63.2};
        settings.reference = ReferenceMode::CursorValue;
        settings.modelPercentA = 0.0;
        settings.modelPercentB = 63.2;
        settings.deltaCoTagId = coId;
        const StepResult r = analyzeStep(recorder, pvId, tBeforeStep - 0.5, tEnd, settings);
        qInfo().noquote() << "lag step: K =" << (r.gain ? *r.gain : kNaN) << "L ="
                          << (r.deadTime ? *r.deadTime : kNaN) << "T ="
                          << (r.timeConstant ? *r.timeConstant : kNaN) << "error =" << r.error;
        QVERIFY2(r.valid, qPrintable(r.error));
        QVERIFY(r.gain && r.deadTime && r.timeConstant);
        QVERIFY2(std::abs(*r.gain - 1.0) <= 0.05, qPrintable(QString::number(*r.gain)));
        QVERIFY2(std::abs(*r.timeConstant - 2.0) <= 0.3, qPrintable(QString::number(*r.timeConstant)));
        QVERIFY2(std::abs(*r.deadTime - 1.0) <= 0.3, qPrintable(QString::number(*r.deadTime)));
        QCOMPARE(r.setpoints.size(), 2);
        QVERIFY(r.setpoints.at(1).found);
    }

    void session_roundTripKeepsAnalysis()
    {
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        const TagId coId = 1;
        const TagId pvId = 2;
        const QList<Tag> tags{makeTag(coId, Area::HoldingRegister, 100, ValueType::Int16),
                              makeTag(pvId, Area::InputRegister, 10, ValueType::Int16)};
        m_harness->setTags(tags);
        m_harness->connectTo(settingsFor(m_server->port(), 1000, 1, 100));
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->batches.size() >= 2, 5000);
        QTest::qWait(2000);

        const double tBeforeStep = m_harness->batches.last().t;
        m_harness->write(79, 100, {1000});
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->writes.contains(79), 3000);
        QVERIFY2(m_harness->writes.value(79).ok(), qPrintable(m_harness->writes.value(79).message));
        QTest::qWait(10000);
        const double tEnd = m_harness->batches.last().t;

        Recorder recorder;
        recorder.setStartTime(QDateTime::currentDateTime());
        for (const SampleBatch &b : std::as_const(m_harness->batches))
            recorder.append(b);

        AnalysisSettings settings;
        settings.percents = {0.0, 63.2};
        settings.reference = ReferenceMode::CursorValue;
        settings.modelPercentA = 0.0;
        settings.modelPercentB = 63.2;
        settings.deltaCoTagId = coId;
        const double c1 = tBeforeStep - 0.5;
        const StepResult before = analyzeStep(recorder, pvId, c1, tEnd, settings);
        QVERIFY2(before.valid, qPrintable(before.error));
        QVERIFY(before.gain && before.deadTime && before.timeConstant);

        Project project;
        project.tags = tags;
        project.nextTagId = 3;
        project.analysis = settings;
        SessionCursors cursors;
        cursors.analysisC1 = c1;
        cursors.analysisC2 = tEnd;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("lag.mbsession"));
        QString error;
        QVERIFY2(saveSession(path, project, recorder, cursors, &error), qPrintable(error));

        Project loadedProject;
        Recorder loaded;
        SessionCursors loadedCursors;
        QVERIFY2(loadSession(path, loadedProject, loaded, loadedCursors, &error), qPrintable(error));
        QCOMPARE(loaded.size(), recorder.size());
        QCOMPARE(loadedCursors, cursors);
        QVERIFY(loadedProject == project);

        const StepResult after = analyzeStep(loaded, pvId, *loadedCursors.analysisC1, *loadedCursors.analysisC2,
                                             loadedProject.analysis);
        QVERIFY2(after.valid, qPrintable(after.error));
        QVERIFY(after.gain && after.deadTime && after.timeConstant);
        QCOMPARE(*after.gain, *before.gain);
        QCOMPARE(*after.deadTime, *before.deadTime);
        QCOMPARE(*after.timeConstant, *before.timeConstant);
    }

    void worker_outOfMapBlockIsException()
    {
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        m_harness->setTags({makeTag(1, Area::InputRegister, 2), makeTag(2, Area::HoldingRegister, 500)});
        m_harness->connectTo(settingsFor(m_server->port(), 1000, 1, 200));
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->batches.size() >= 3, 5000);
        const SampleBatch &b = m_harness->batches.last();
        QVERIFY(WorkerHarness::sampleOf(b, 1)->quality == Quality::Good);
        QVERIFY(WorkerHarness::sampleOf(b, 2)->quality == Quality::Exception);
        QVERIFY(m_harness->lastState() == ConnectionState::Connected);
    }

    void worker_oneOffReadHolding()
    {
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        m_harness->setTags({makeTag(1, Area::InputRegister, 2)});
        m_harness->connectTo(settingsFor(m_server->port(), 1000, 1, 200));
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->lastState() == ConnectionState::Connected, 5000);
        m_harness->read(7, Area::HoldingRegister, 109, 3);
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->readCount(7) == 1, 3000);
        const ReadRecord *r = m_harness->readOf(7);
        QVERIFY2(r->result.ok(), qPrintable(r->result.message));
        QCOMPARE(r->values, (QList<quint16>{0, 0x3333, 0x4251}));
    }

    void worker_oneOffReadInput()
    {
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        m_harness->connectTo(settingsFor(m_server->port(), 1000, 1, 200));
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->lastState() == ConnectionState::Connected, 5000);
        m_harness->read(1, Area::InputRegister, 20, 2);
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->readCount(1) == 1, 3000);
        const ReadRecord *r = m_harness->readOf(1);
        QVERIFY2(r->result.ok(), qPrintable(r->result.message));
        QCOMPARE(r->values.size(), 2);
    }

    void worker_oneOffReadOutOfMapKeepsPolling()
    {
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        m_harness->setTags({makeTag(1, Area::InputRegister, 2)});
        m_harness->connectTo(settingsFor(m_server->port(), 1000, 1, 100));
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->batches.size() >= 2, 5000);
        const int statesBefore = m_harness->states.size();
        m_harness->read(3, Area::HoldingRegister, 199, 3);
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->readCount(3) == 1, 3000);
        const ReadRecord *r = m_harness->readOf(3);
        QCOMPARE(r->result.error, ModbusError::Exception);
        QCOMPARE(int(r->result.exceptionCode), 2);
        QVERIFY(r->values.isEmpty());

        const int n = m_harness->batches.size();
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->batches.size() >= n + 2, 3000);
        QVERIFY(m_harness->batches.last().samples.at(0).quality == Quality::Good);
        QVERIFY(m_harness->lastState() == ConnectionState::Connected);
        QCOMPARE(m_harness->states.size(), statesBefore);
    }

    void worker_oneOffReadRejectedRequests()
    {
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        m_harness->read(1, Area::HoldingRegister, 0, 1); // never connected
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->readCount(1) == 1, 2000);
        QCOMPARE(m_harness->readOf(1)->result.error, ModbusError::NotConnected);
        QVERIFY(m_harness->readOf(1)->values.isEmpty());

        m_harness->connectTo(settingsFor(m_server->port(), 1000, 1, 200));
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->lastState() == ConnectionState::Connected, 5000);
        m_harness->read(2, Area::HoldingRegister, 0, 0);
        m_harness->read(3, Area::HoldingRegister, 0, 126);
        m_harness->read(4, Area::InputRegister, 65535, 2);
        m_harness->read(5, Area::InputRegister, 65535, 1); // last address is valid input
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->readCount(5) == 1, 3000);
        for (quint64 id : {2, 3, 4}) {
            QCOMPARE(m_harness->readCount(id), 1);
            QCOMPARE(m_harness->readOf(id)->result.error, ModbusError::InvalidArgument);
        }
        QVERIFY(m_harness->readOf(5)->result.error != ModbusError::InvalidArgument);
        QVERIFY(m_harness->lastState() == ConnectionState::Connected);
    }

    void worker_oneOffReadFailsOnDisconnect()
    {
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        m_harness->connectTo(settingsFor(m_server->port(), 1000, 1, 200));
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->lastState() == ConnectionState::Connected, 5000);
        m_harness->readThenDisconnect(9, Area::HoldingRegister, 0, 2);
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->lastState() == ConnectionState::Disconnected, 3000);
        QTest::qWait(200);
        QCOMPARE(m_harness->readCount(9), 1);
        QCOMPARE(m_harness->readOf(9)->result.error, ModbusError::NotConnected);
    }

    void worker_oneOffReadsAnsweredOnceInOrder()
    {
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        m_harness->setTags({makeTag(1, Area::InputRegister, 2)});
        m_harness->connectTo(settingsFor(m_server->port(), 1000, 1, 100));
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->lastState() == ConnectionState::Connected, 5000);
        for (quint64 i = 0; i < 5; ++i)
            m_harness->read(100 + i, Area::HoldingRegister, quint16(110 + (i % 2)), 1);
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->reads.size() >= 5, 3000);
        QTest::qWait(300);
        QCOMPARE(m_harness->reads.size(), 5);
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(m_harness->reads.at(i).id, quint64(100 + i));
            QVERIFY(m_harness->reads.at(i).result.ok());
            QCOMPARE(m_harness->reads.at(i).values,
                     (QList<quint16>{i % 2 ? quint16(0x4251) : quint16(0x3333)}));
        }
    }

    void worker_reconnectsAfterServerRestart()
    {
        QVERIFY(m_server->start());
        const quint16 port = m_server->port();
        m_harness = std::make_unique<WorkerHarness>();
        m_harness->setTags({makeTag(1, Area::InputRegister, 2)});
        m_harness->connectTo(settingsFor(port, 500, 1, 200));
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->batches.size() >= 2, 5000);

        m_server->stop();
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->hasState(ConnectionState::Reconnecting), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                return !m_harness->batches.isEmpty()
                    && m_harness->batches.last().samples.at(0).quality == Quality::Disconnected;
            }(),
            3000);

        // Bring a new server up on the same port.
        const int before = m_harness->batches.size();
        QVERIFY2(m_server->start({}, port), "could not restart the server on the same port");
        QCOMPARE(m_server->port(), port);
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->lastState() == ConnectionState::Connected, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                return m_harness->batches.size() > before
                    && m_harness->batches.last().samples.at(0).quality == Quality::Good;
            }(),
            5000);
    }

    void worker_disconnectStopsPolling()
    {
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        m_harness->setTags({makeTag(1, Area::InputRegister, 2)});
        m_harness->connectTo(settingsFor(m_server->port(), 1000, 1, 100));
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->batches.size() >= 2, 5000);
        m_harness->disconnectFrom();
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->lastState() == ConnectionState::Disconnected, 3000);
        QTest::qWait(100); // let in-flight queued batch signals arrive
        const int n = m_harness->batches.size();
        QTest::qWait(600);
        QCOMPARE(m_harness->batches.size(), n);
    }

    void worker_writeWhileDisconnected()
    {
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        m_harness->write(1, 5, {42}); // never connected
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->writes.contains(1), 2000);
        QCOMPARE(m_harness->writes.value(1).error, ModbusError::NotConnected);

        m_harness->setTags({makeTag(1, Area::InputRegister, 2)});
        m_harness->connectTo(settingsFor(m_server->port(), 1000, 1, 200));
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->batches.size() >= 1, 5000);
        m_harness->disconnectFrom();
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->lastState() == ConnectionState::Disconnected, 3000);
        m_harness->write(2, 5, {42});
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->writes.contains(2), 2000);
        QCOMPARE(m_harness->writes.value(2).error, ModbusError::NotConnected);
    }

    void worker_writeUnderLoad()
    {
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        QList<Tag> tags;
        for (int i = 0; i < 20; ++i)
            tags.append(makeTag(TagId(i + 1), i % 2 ? Area::InputRegister : Area::HoldingRegister,
                                quint16(i * 3)));
        m_harness->setTags(tags);
        m_harness->connectTo(settingsFor(m_server->port(), 1000, 1, 100));
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->batches.size() >= 3, 5000);
        m_harness->write(9, 150, {321});
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->writes.contains(9), 3000);
        QVERIFY2(m_harness->writes.value(9).ok(), qPrintable(m_harness->writes.value(9).message));

        // Read it back with an independent connection.
        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port())).ok());
        QList<quint16> v;
        QVERIFY(c.readRegisters(Area::HoldingRegister, 150, 1, v).ok());
        QCOMPARE(v.value(0), quint16(321));
    }

    // ---- 32-bit values (T64 / T71) ---------------------------------------------------------

    void worker_reads32BitTags()
    {
        QVERIFY(m_server->start());
        Tag abcd = makeTag(1, Area::InputRegister, 20, ValueType::Float32);
        Tag cdab = makeTag(2, Area::InputRegister, 22, ValueType::Float32);
        cdab.byteOrder = ByteOrder::CDAB;
        Tag i32 = makeTag(3, Area::InputRegister, 24, ValueType::Int32);
        Tag wrong = makeTag(4, Area::InputRegister, 20, ValueType::Float32); // ABCD data read as CDAB
        wrong.byteOrder = ByteOrder::CDAB;

        m_harness = std::make_unique<WorkerHarness>();
        m_harness->setTags({abcd, cdab, i32, wrong});
        m_harness->connectTo(settingsFor(m_server->port(), 1000, 1, 100));

        // Good quality within 2 s.
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                for (const SampleBatch &b : std::as_const(m_harness->batches)) {
                    bool all = b.samples.size() == 4;
                    for (const TagSample &s : b.samples)
                        all = all && s.quality == Quality::Good;
                    if (all)
                        return true;
                }
                return false;
            }(),
            2000);

        // Collect ~5 s of batches.
        QTest::qWait(5000);

        constexpr double amplitude = 123.456;
        int goodBatches = 0;
        bool anyNonZero = false;
        bool wrongDiffers = false;
        double maxAbsInt32 = 0;
        for (const SampleBatch &b : std::as_const(m_harness->batches)) {
            const TagSample *a = WorkerHarness::sampleOf(b, 1);
            const TagSample *c = WorkerHarness::sampleOf(b, 2);
            const TagSample *n = WorkerHarness::sampleOf(b, 3);
            const TagSample *w = WorkerHarness::sampleOf(b, 4);
            QVERIFY(a && c && n && w);
            if (a->quality != Quality::Good || c->quality != Quality::Good
                || n->quality != Quality::Good || w->quality != Quality::Good)
                continue;
            ++goodBatches;
            QVERIFY2(std::abs(a->value) <= amplitude + 1e-3, qPrintable(QString::number(a->value)));
            QVERIFY2(std::abs(c->value) <= amplitude + 1e-3, qPrintable(QString::number(c->value)));
            QVERIFY2(std::abs(a->value - c->value) <= 0.01 * amplitude,
                     qPrintable(QStringLiteral("ABCD %1 vs CDAB %2").arg(a->value).arg(c->value)));
            if (a->value != 0.0 || c->value != 0.0)
                anyNonZero = true;
            if (!(std::abs(w->value - a->value) < 1e-3)) // also true for NaN
                wrongDiffers = true;
            maxAbsInt32 = std::max(maxAbsInt32, std::abs(n->value));
        }
        QVERIFY2(goodBatches >= 20, qPrintable(QString::number(goodBatches)));
        QVERIFY(anyNonZero);
        QVERIFY2(wrongDiffers, "reading ABCD data as CDAB must give a different value");
        QVERIFY2(maxAbsInt32 > 32767, qPrintable(QString::number(maxAbsInt32)));
    }

    void conn_write32BitRoundTrip_data()
    {
        QTest::addColumn<int>("order");
        QTest::addColumn<quint16>("address");
        QTest::newRow("ABCD") << int(ByteOrder::ABCD) << quint16(150);
        QTest::newRow("CDAB") << int(ByteOrder::CDAB) << quint16(150);
    }

    void conn_write32BitRoundTrip()
    {
        QFETCH(int, order);
        QFETCH(quint16, address);
        QVERIFY(m_server->start());
        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port())).ok());

        Tag f = makeTag(1, Area::HoldingRegister, address, ValueType::Float32);
        f.byteOrder = static_cast<ByteOrder>(order);
        const auto regs = encodeRegisters(f, 123.25);
        QVERIFY(regs.has_value());
        QCOMPARE(regs->size(), 2);
        QVERIFY2(c.writeMultiple(address, *regs).ok(), "writeMultiple failed");
        QList<quint16> v;
        QVERIFY(c.readRegisters(Area::HoldingRegister, address, 2, v).ok());
        QCOMPARE(v, *regs);
        QCOMPARE(decodeValue(f, combineRegisters(f, v.at(0), v.at(1))), 123.25);

        Tag i = makeTag(2, Area::HoldingRegister, 152, ValueType::Int32);
        i.byteOrder = static_cast<ByteOrder>(order);
        const auto iregs = encodeRegisters(i, -100000.0);
        QVERIFY(iregs.has_value());
        QVERIFY(c.writeMultiple(152, *iregs).ok());
        QVERIFY(c.readRegisters(Area::HoldingRegister, 152, 2, v).ok());
        QCOMPARE(decodeValue(i, combineRegisters(i, v.at(0), v.at(1))), -100000.0);
    }

    void worker_write32Bit_data()
    {
        QTest::addColumn<bool>("single");
        QTest::newRow("FC16") << false;
        QTest::newRow("FC06 per register") << true;
    }

    void worker_write32Bit()
    {
        QFETCH(bool, single);
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        ConnectionSettings s = settingsFor(m_server->port(), 1000, 1, 100);
        s.singleRegisterWrites = single;
        m_harness->setTags({makeTag(1, Area::InputRegister, 2)});
        m_harness->connectTo(s);
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->batches.size() >= 1, 5000);

        Tag f = makeTag(2, Area::HoldingRegister, 160, ValueType::Float32);
        const auto regs = encodeRegisters(f, 42.5);
        QVERIFY(regs.has_value());
        m_harness->write(5, 160, *regs);
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->writes.contains(5), 3000);
        QVERIFY2(m_harness->writes.value(5).ok(), qPrintable(m_harness->writes.value(5).message));

        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port())).ok());
        QList<quint16> v;
        QVERIFY(c.readRegisters(Area::HoldingRegister, 160, 2, v).ok());
        QCOMPARE(decodeValue(f, combineRegisters(f, v.at(0), v.at(1))), 42.5);
    }

    void worker_singleRegisterWriteFailsOnSecondRegister()
    {
        QVERIFY(m_server->start());
        m_harness = std::make_unique<WorkerHarness>();
        ConnectionSettings s = settingsFor(m_server->port(), 1000, 1, 100);
        s.singleRegisterWrites = true;
        m_harness->setTags({makeTag(1, Area::InputRegister, 2)});
        m_harness->connectTo(s);
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->batches.size() >= 1, 5000);

        m_harness->write(6, 199, {0x1234, 0x5678}); // register 200 is outside the server map
        QTRY_VERIFY_WITH_TIMEOUT(m_harness->writes.contains(6), 3000);
        const ModbusResult r = m_harness->writes.value(6);
        QCOMPARE(r.error, ModbusError::Exception);
        QCOMPARE(r.exceptionCode, 2);
        QVERIFY2(r.message.contains(QStringLiteral("200")), qPrintable(r.message));
        QVERIFY(m_harness->lastState() == ConnectionState::Connected);

        ModbusConnection c; // the first register was written before the failure
        QVERIFY(c.open(settingsFor(m_server->port())).ok());
        QList<quint16> v;
        QVERIFY(c.readRegisters(Area::HoldingRegister, 199, 1, v).ok());
        QCOMPARE(v.value(0), quint16(0x1234));
    }

    // ---- ModbusConnection error paths ------------------------------------------------------

    void conn_notConnectedOperations()
    {
        ModbusConnection c;
        QVERIFY(!c.isOpen());
        QList<quint16> v;
        QCOMPARE(c.readRegisters(Area::HoldingRegister, 0, 1, v).error, ModbusError::NotConnected);
        QCOMPARE(c.readRegisters(Area::InputRegister, 0, 1, v).error, ModbusError::NotConnected);
        QCOMPARE(c.writeSingle(0, 1).error, ModbusError::NotConnected);
        QCOMPARE(c.writeMultiple(0, {1, 2}).error, ModbusError::NotConnected);
        QCOMPARE(c.writeMultiple(0, {}).error, ModbusError::NotConnected); // checked before the count
        c.flush(); // no-op when closed
        c.close();
        QVERIFY(!c.isOpen());
    }

    void conn_openRejectsBadSettings()
    {
        // Validation happens before any socket is created: no server needed.
        auto expectInvalid = [](ConnectionSettings s, const char *what) {
            ModbusConnection c;
            const ModbusResult r = c.open(s);
            QVERIFY2(r.error == ModbusError::InvalidArgument, what);
            QVERIFY(!c.isOpen());
        };
        ConnectionSettings s = settingsFor(9);
        s.host = QStringLiteral("   ");
        expectInvalid(s, "blank host");
        s.host = QStringLiteral("example.invalid");
        expectInvalid(s, "host name that is not an IPv4 literal");
        s.host = QStringLiteral("256.1.1.1");
        expectInvalid(s, "IPv4 octet above 255");
        s.host = QStringLiteral("10.0.0");
        expectInvalid(s, "three-part address");
        s = settingsFor(9);
        s.port = 0;
        expectInvalid(s, "port 0");
        s = settingsFor(9);
        s.unitId = 248;
        expectInvalid(s, "unit id 248");
        s = settingsFor(9);
        s.timeoutMs = 0;
        expectInvalid(s, "timeout 0");
    }

    void conn_openAcceptsLocalhostAndUnit255()
    {
        QVERIFY(m_server->start());
        ConnectionSettings s = settingsFor(m_server->port());
        s.host = QStringLiteral("LocalHost"); // mapped to 127.0.0.1, case-insensitive
        ModbusConnection c;
        QVERIFY2(c.open(s).ok(), "localhost should be accepted");
        c.close();
        s.unitId = 255; // accepted for TCP
        QVERIFY2(c.open(s).ok(), "unit id 255 should be accepted");
    }

    void conn_invalidRangeArguments()
    {
        QVERIFY(m_server->start());
        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port())).ok());
        QList<quint16> v;
        QCOMPARE(c.readRegisters(Area::HoldingRegister, 0, 0, v).error, ModbusError::InvalidArgument);
        QCOMPARE(c.readRegisters(Area::HoldingRegister, 0, 126, v).error, ModbusError::InvalidArgument);
        QCOMPARE(c.readRegisters(Area::InputRegister, 65535, 2, v).error, ModbusError::InvalidArgument);
        QCOMPARE(c.writeMultiple(0, {}).error, ModbusError::InvalidArgument);

        QList<quint16> tooMany(124, 7);
        QCOMPARE(c.writeMultiple(0, tooMany).error, ModbusError::InvalidArgument);
        QCOMPARE(c.writeMultiple(65535, {1, 2}).error, ModbusError::InvalidArgument);

        // Limits themselves are accepted: 125 registers read, 123 written.
        QList<quint16> maxRead;
        QVERIFY(c.readRegisters(Area::HoldingRegister, 0, 125, maxRead).ok());
        QCOMPARE(maxRead.size(), 125);
        QVERIFY(c.writeMultiple(0, QList<quint16>(123, 9)).ok());
        QVERIFY(c.readRegisters(Area::HoldingRegister, 122, 1, v).ok());
        QCOMPARE(v.value(0), quint16(9));
    }

    void conn_serverStoppedFailsRead()
    {
        QVERIFY(m_server->start());
        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port(), 500)).ok());
        m_server->stop();
        QList<quint16> v;
        const ModbusResult r = c.readRegisters(Area::InputRegister, 0, 1, v);
        qInfo() << "read after server stop:" << int(r.error) << r.message;
        QVERIFY(!r.ok());
        QVERIFY2(r.error == ModbusError::ConnectionLost || r.error == ModbusError::Timeout,
                 qPrintable(r.message));
    }

    // ---- modbus_test_server command line (server/main.cpp) ---------------------------------

    void server_help()
    {
        const CliRun r = runServerCli({QStringLiteral("--help")});
        QVERIFY(r.finished);
        QCOMPARE(r.exitCode, 0);
        for (const char *opt : {"--log-writes", "--config", "--delay", "--drop", "--verbose", "--port"})
            QVERIFY2(r.out.contains(QLatin1String(opt)), opt);
    }

    void server_version()
    {
        const CliRun r = runServerCli({QStringLiteral("--version")});
        QVERIFY(r.finished);
        QCOMPARE(r.exitCode, 0);
        QVERIFY2(QRegularExpression(QStringLiteral("^modbus_test_server \\d+\\.\\d+\\.\\d+ \\(libmodbus "))
                     .match(r.out)
                     .hasMatch(),
                 qPrintable(r.out));
    }

    void server_invalidOptions_data()
    {
        QTest::addColumn<QStringList>("args");
        QTest::addColumn<QString>("expected");
        QTest::newRow("port above 65535")
            << QStringList{QStringLiteral("--port"), QStringLiteral("70000")}
            << QStringLiteral("--port must be 0..65535");
        QTest::newRow("port not a number")
            << QStringList{QStringLiteral("--port"), QStringLiteral("abc")}
            << QStringLiteral("--port must be 0..65535");
        QTest::newRow("drop above 100")
            << QStringList{QStringLiteral("--drop"), QStringLiteral("150")}
            << QStringLiteral("--drop must be 0..100");
        QTest::newRow("negative delay") << QStringList{QStringLiteral("--delay"), QStringLiteral("-1")}
                                        << QStringLiteral("delay");
        QTest::newRow("tick zero")
            << QStringList{QStringLiteral("--tick"), QStringLiteral("0")}
            << QStringLiteral("--tick must be 1..60000 ms");
        QTest::newRow("unit above 247")
            << QStringList{QStringLiteral("--unit"), QStringLiteral("300")}
            << QStringLiteral("--unit must be 0..247");
        QTest::newRow("unknown option") << QStringList{QStringLiteral("--bogus")} << QStringLiteral("bogus");
        QTest::newRow("option without value") << QStringList{QStringLiteral("--tick")} << QStringLiteral("tick");
        QTest::newRow("positional argument")
            << QStringList{QStringLiteral("extra")}
            << QStringLiteral("unexpected argument 'extra'");
    }

    void server_invalidOptions()
    {
        QFETCH(QStringList, args);
        QFETCH(QString, expected);
        const CliRun r = runServerCli(args);
        QVERIFY(r.finished);
        QCOMPARE(r.exitCode, 2);
        QVERIFY2(r.err.startsWith(QStringLiteral("modbus_test_server:")), qPrintable(r.err));
        QVERIFY2(r.err.contains(expected), qPrintable(r.err));
    }

    void server_configErrors_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<bool>("missingFile");
        QTest::addColumn<QString>("expected");
        QTest::newRow("missing file") << QByteArray() << true << QStringLiteral("Cannot open");
        QTest::newRow("invalid JSON") << QByteArray("{ \"tickMs\": ") << false
                                      << QStringLiteral("Invalid JSON");
        QTest::newRow("top-level array") << QByteArray("[1, 2]") << false
                                         << QStringLiteral("must be an object");
        QTest::newRow("value out of range") << QByteArray("{\"tickMs\": 0}") << false
                                            << QStringLiteral("\"tickMs\"");
    }

    void server_configErrors()
    {
        QFETCH(QByteArray, json);
        QFETCH(bool, missingFile);
        QFETCH(QString, expected);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(missingFile ? QStringLiteral("nope.json") : QStringLiteral("server.json"));
        if (!missingFile)
            QVERIFY(writeTextFile(path, json));
        const CliRun r = runServerCli({QStringLiteral("--config"), path});
        QVERIFY(r.finished);
        QCOMPARE(r.exitCode, 2);
        QVERIFY2(r.err.contains(QStringLiteral("invalid config: ")), qPrintable(r.err));
        QVERIFY2(r.err.contains(expected), qPrintable(r.err));
    }

    void server_configFileApplied()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("server.json"));
        QVERIFY(writeTextFile(path, R"({
  "unitId": 3, "holdingCount": 40, "inputCount": 12, "tickMs": 100,
  "signals": [
    {"input": 0, "kind": "constant", "amplitude": 77, "offset": 0},
    {"input": 4, "kind": "counter", "format": "int32", "byteOrder": "CDAB"}
  ]
})"));
        QVERIFY(m_server->start({QStringLiteral("--config"), path}));
        const QString banner = m_server->log();
        QVERIFY2(banner.contains(QStringLiteral("unit 3, holding registers 0..39 (read/write), input registers 0..11, tick 100 ms")),
                 qPrintable(banner));
        QVERIFY(banner.contains(QStringLiteral("IR0: constant")));
        QVERIFY(banner.contains(QStringLiteral("IR4-5: counter (int32 CDAB)")));

        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port(), 1000, 3)).ok());
        QList<quint16> v;
        QVERIFY(c.readRegisters(Area::HoldingRegister, 39, 1, v).ok());
        const ModbusResult beyond = c.readRegisters(Area::HoldingRegister, 40, 1, v);
        QCOMPARE(beyond.error, ModbusError::Exception);
        QCOMPARE(beyond.exceptionCode, 2);
        QVERIFY(c.readRegisters(Area::InputRegister, 4, 2, v).ok());
        QCOMPARE(c.readRegisters(Area::InputRegister, 12, 1, v).exceptionCode, 2);
    }

    void server_verboseLogsConnection()
    {
        QVERIFY(m_server->start({QStringLiteral("-v")}));
        {
            ModbusConnection c;
            QVERIFY(c.open(settingsFor(m_server->port())).ok());
            QList<quint16> v;
            QVERIFY(c.readRegisters(Area::InputRegister, 0, 2, v).ok());
            c.close();
        }
        QTRY_VERIFY_WITH_TIMEOUT(m_server->log().contains(QStringLiteral("disconnected (")), 3000);
        const QString log = m_server->log();
        QVERIFY2(log.contains(QStringLiteral("] connected")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("unit=1 fc=0x04 read addr=0 count=2")), qPrintable(log));
    }

    void server_logWritesShowsValues()
    {
        QVERIFY(m_server->start({QStringLiteral("--log-writes")}));
        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port())).ok());
        // 1.0f is 0x3F800000: ABCD = 0x3F80,0x0000.
        QVERIFY(c.writeMultiple(30, {0x3F80, 0x0000}).ok());
        QList<quint16> v;
        QVERIFY(c.readRegisters(Area::HoldingRegister, 30, 2, v).ok());
        QVERIFY(c.writeSingle(40, 0x3F80).ok());
        QVERIFY(c.writeSingle(41, 0x0000).ok());
        QTRY_VERIFY_WITH_TIMEOUT(m_server->log().contains(QStringLiteral("FC06 pair")), 3000);
        const QString log = m_server->log();
        QVERIFY2(log.contains(QStringLiteral("fc=0x10 write addr=30 count=2 values=0x3F80,0x0000")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("float ABCD=1 ")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("FC06 pair addr=40-41 values=0x3F80,0x0000")), qPrintable(log));
        QVERIFY2(!log.contains(QStringLiteral("read addr=")), "reads must not be logged with --log-writes");
        QVERIFY2(!log.contains(QStringLiteral("] connected")), "connections are logged only with -v");
    }

    void server_dropInjectionLogged()
    {
        QVERIFY(m_server->start({QStringLiteral("-v"), QStringLiteral("--drop"), QStringLiteral("100")}));
        QVERIFY(m_server->log().contains(QStringLiteral("fault injection: delay 0 ms, drop 100%")));
        ModbusConnection c;
        QVERIFY(c.open(settingsFor(m_server->port(), 500)).ok());
        QList<quint16> v;
        QVERIFY(!c.readRegisters(Area::InputRegister, 0, 1, v).ok());
        QTRY_VERIFY_WITH_TIMEOUT(m_server->log().contains(QStringLiteral("fault injection: dropped")), 3000);
    }

private:
    std::unique_ptr<ServerProcess> m_server;
    std::unique_ptr<WorkerHarness> m_harness;
};

QTEST_GUILESS_MAIN(TstIntegration)
#include "tst_integration.moc"


