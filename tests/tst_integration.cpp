#include "MetaTypes.h"
#include "ModbusConnection.h"
#include "PollWorker.h"
#include "TagCodec.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
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

private:
    QThread m_thread;
    PollWorker *m_worker = nullptr;
};

} // namespace

class TstIntegration : public QObject
{
    Q_OBJECT

private slots:
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

private:
    std::unique_ptr<ServerProcess> m_server;
    std::unique_ptr<WorkerHarness> m_harness;
};

QTEST_GUILESS_MAIN(TstIntegration)
#include "tst_integration.moc"


