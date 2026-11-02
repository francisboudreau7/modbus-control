// Polling and writing coils and discrete inputs through PollWorker, against a small in-process
// libmodbus server (port 0) whose mapping has coils, discrete inputs and holding registers.

#include "PollWorker.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <modbus.h>

#include <QThread>
#include <QtTest>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

using namespace mb;

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
#else
using NativeSocket = int;
#endif

constexpr int kBits = 2200;

NativeSocket native(int fd)
{
    return static_cast<NativeSocket>(fd);
}

void closeSocket(int fd)
{
#ifdef _WIN32
    ::closesocket(native(fd));
#else
    ::close(fd);
#endif
}

struct Request
{
    int fc = 0;
    int address = 0;
    int count = 0;
};

/// One-thread select() loop around a libmodbus TCP context. Coils start as bit i = (i % 3 == 0),
/// discrete inputs as bit i = (i % 2 == 1), holding register 5 = 1234.
class BitServer
{
public:
    ~BitServer() { stop(); }

    bool start()
    {
        m_stop = false;
        m_requests.clear();
        m_ctx = modbus_new_tcp("127.0.0.1", 0);
        if (!m_ctx)
            return false;
        m_mapping = modbus_mapping_new(kBits, kBits, 20, 20);
        if (!m_mapping)
            return false;
        for (int i = 0; i < kBits; ++i) {
            m_mapping->tab_bits[i] = i % 3 == 0 ? 1 : 0;
            m_mapping->tab_input_bits[i] = i % 2 == 1 ? 1 : 0;
        }
        m_mapping->tab_registers[5] = 1234;
        m_listen = modbus_tcp_listen(m_ctx, 4);
        if (m_listen == -1)
            return false;
        sockaddr_in addr {};
        socklen_t len = sizeof(addr);
        if (::getsockname(native(m_listen), reinterpret_cast<sockaddr *>(&addr), &len) != 0)
            return false;
        m_port = ntohs(addr.sin_port);
        m_thread = std::thread([this] { run(); });
        return true;
    }

    void stop()
    {
        m_stop = true;
        if (m_thread.joinable())
            m_thread.join();
        if (m_listen != -1) {
            closeSocket(m_listen);
            m_listen = -1;
        }
        if (m_ctx) {
            modbus_free(m_ctx);
            m_ctx = nullptr;
        }
        if (m_mapping) {
            modbus_mapping_free(m_mapping);
            m_mapping = nullptr;
        }
    }

    quint16 port() const { return m_port; }

    /// FC15 requests are refused with exception 1 (illegal function).
    void refuseFc15(bool on) { m_refuseFc15 = on; }
    /// Bit reads (FC01/FC02) whose range holds `address` answer exception 2 (-1: none).
    void setBadBit(int address) { m_badBit = address; }

    bool coil(int address)
    {
        std::lock_guard lock(m_mutex);
        return m_mapping->tab_bits[address] != 0;
    }
    void setCoil(int address, bool on)
    {
        std::lock_guard lock(m_mutex);
        m_mapping->tab_bits[address] = on ? 1 : 0;
    }
    QList<Request> requests()
    {
        std::lock_guard lock(m_mutex);
        return m_requests;
    }
    QList<Request> requestsOf(int fc)
    {
        QList<Request> out;
        for (const Request &r : requests())
            if (r.fc == fc)
                out.append(r);
        return out;
    }

private:
    void run()
    {
        std::vector<unsigned char> query(MODBUS_TCP_MAX_ADU_LENGTH);
        std::vector<int> clients;
        while (!m_stop.load()) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(native(m_listen), &readSet);
            int maxFd = m_listen;
            for (const int fd : clients) {
                FD_SET(native(fd), &readSet);
                maxFd = std::max(maxFd, fd);
            }
            timeval timeout {};
            timeout.tv_usec = 20 * 1000;
            if (::select(maxFd + 1, &readSet, nullptr, nullptr, &timeout) <= 0)
                continue;
            if (FD_ISSET(native(m_listen), &readSet)) {
                int listenSocket = m_listen;
                const int fd = modbus_tcp_accept(m_ctx, &listenSocket);
                if (fd >= 0)
                    clients.push_back(fd);
            }
            const std::vector<int> snapshot = clients;
            for (const int fd : snapshot) {
                if (!FD_ISSET(native(fd), &readSet))
                    continue;
                modbus_set_socket(m_ctx, fd);
                const int rc = modbus_receive(m_ctx, query.data());
                if (rc == -1) {
                    closeSocket(fd);
                    clients.erase(std::remove(clients.begin(), clients.end(), fd), clients.end());
                    continue;
                }
                const int hl = modbus_get_header_length(m_ctx);
                if (rc <= hl)
                    continue;
                handle(query.data(), hl, rc);
            }
        }
        for (const int fd : clients)
            closeSocket(fd);
    }

    void handle(unsigned char *query, int hl, int length)
    {
        const int fc = query[hl];
        const int address = (query[hl + 1] << 8) | query[hl + 2];
        const int count = (query[hl + 3] << 8) | query[hl + 4];
        std::lock_guard lock(m_mutex);
        m_requests.append({fc, address, fc == 5 ? 1 : count});
        const bool bitRead = fc == 1 || fc == 2;
        const int bad = m_badBit.load();
        if (fc == 15 && m_refuseFc15) {
            modbus_reply_exception(m_ctx, query, MODBUS_EXCEPTION_ILLEGAL_FUNCTION);
        } else if (bitRead && bad >= 0 && address <= bad && bad < address + count) {
            modbus_reply_exception(m_ctx, query, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
        } else {
            modbus_reply(m_ctx, query, length, m_mapping);
        }
    }

    modbus_t *m_ctx = nullptr;
    modbus_mapping_t *m_mapping = nullptr;
    int m_listen = -1;
    quint16 m_port = 0;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_refuseFc15{false};
    std::atomic<int> m_badBit{-1};
    std::mutex m_mutex;
    QList<Request> m_requests;
};

ConnectionSettings settingsFor(quint16 port)
{
    ConnectionSettings s;
    s.host = QStringLiteral("127.0.0.1");
    s.port = port;
    s.unitId = 1;
    s.timeoutMs = 1000;
    s.pollPeriodMs = 100;
    return s;
}

Tag makeTag(TagId id, Area area, quint16 address)
{
    Tag t;
    t.id = id;
    t.name = QStringLiteral("tag%1").arg(id);
    t.area = area;
    t.address = address;
    t.type = isBitArea(area) ? ValueType::Bool : ValueType::UInt16;
    t.writable = isWritableArea(area);
    return t;
}

struct ReadRecord
{
    quint64 id = 0;
    ModbusResult result;
    QList<quint16> values;
};

/// PollWorker on its own thread; every emitted signal is recorded on the main thread.
class WorkerHarness : public QObject
{
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
        connect(m_worker, &PollWorker::message, this, [this](const QString &t) { messages.append(t); });
        m_thread.start();
    }

    ~WorkerHarness() override
    {
        QMetaObject::invokeMethod(m_worker, &PollWorker::disconnectFrom, Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait(5000);
    }

    void connectTo(const ConnectionSettings &s)
    {
        QMetaObject::invokeMethod(m_worker, [w = m_worker, s] { w->connectTo(s); }, Qt::QueuedConnection);
    }
    void setTags(const QList<Tag> &tags)
    {
        QMetaObject::invokeMethod(m_worker, [w = m_worker, tags] { w->setTags(tags); }, Qt::QueuedConnection);
    }
    void writeCoils(quint64 id, quint16 address, const QList<quint16> &values)
    {
        QMetaObject::invokeMethod(m_worker, [w = m_worker, id, address, values] {
            w->writeCoils(id, address, values);
        }, Qt::QueuedConnection);
    }
    void read(quint64 id, Area area, quint16 address, int count)
    {
        QMetaObject::invokeMethod(m_worker, [w = m_worker, id, area, address, count] {
            w->readRegisters(id, area, address, count);
        }, Qt::QueuedConnection);
    }

    bool connected() const { return states.contains(ConnectionState::Connected); }
    const ReadRecord *readOf(quint64 id) const
    {
        for (const ReadRecord &r : reads)
            if (r.id == id)
                return &r;
        return nullptr;
    }
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
    QStringList messages;

private:
    QThread m_thread;
    PollWorker *m_worker = nullptr;
};

} // namespace

class TstPollBits : public QObject
{
    Q_OBJECT

private:
    BitServer m_server;
    std::unique_ptr<WorkerHarness> m_h;

    void startAll(const QList<Tag> &tags)
    {
        QVERIFY(m_server.start());
        m_h = std::make_unique<WorkerHarness>();
        m_h->setTags(tags);
        m_h->connectTo(settingsFor(m_server.port()));
        QTRY_VERIFY_WITH_TIMEOUT(m_h->connected(), 5000);
    }

private slots:
    void cleanup()
    {
        if (QTest::currentTestFailed() && m_h)
            qWarning().noquote() << "worker messages:" << m_h->messages.join(QStringLiteral(" | "));
        m_h.reset();
        m_server.stop();
    }

    void pollsCoilsDiscreteInputsAndRegisters()
    {
        startAll({makeTag(1, Area::HoldingRegister, 5), makeTag(2, Area::Coil, 3), makeTag(3, Area::Coil, 4),
                  makeTag(4, Area::DiscreteInput, 1), makeTag(5, Area::DiscreteInput, 2)});
        QTRY_VERIFY_WITH_TIMEOUT(!m_h->batches.isEmpty(), 5000);
        const SampleBatch &b = m_h->batches.last();
        QCOMPARE(b.samples.size(), qsizetype(5));
        for (const TagSample &s : b.samples)
            QVERIFY2(s.quality == Quality::Good, qPrintable(QString::number(s.tagId)));
        QCOMPARE(m_h->sampleOf(b, 1)->value, 1234.0);
        QCOMPARE(m_h->sampleOf(b, 2)->raw, quint32(1)); // coil 3: 3 % 3 == 0
        QCOMPARE(m_h->sampleOf(b, 2)->value, 1.0);
        QCOMPARE(m_h->sampleOf(b, 3)->raw, quint32(0));
        QCOMPARE(m_h->sampleOf(b, 3)->value, 0.0);
        QCOMPARE(m_h->sampleOf(b, 4)->raw, quint32(1)); // discrete input 1: odd
        QCOMPARE(m_h->sampleOf(b, 5)->value, 0.0);

        // The two coils share one FC01, the two inputs one FC02, the register one FC03.
        QTRY_VERIFY(!m_server.requestsOf(1).isEmpty() && !m_server.requestsOf(2).isEmpty());
        QCOMPARE(m_server.requestsOf(1).first().address, 3);
        QCOMPARE(m_server.requestsOf(1).first().count, 2);
        QCOMPARE(m_server.requestsOf(2).first().address, 1);
        QCOMPARE(m_server.requestsOf(2).first().count, 2);
    }

    void followsServerChanges()
    {
        startAll({makeTag(1, Area::Coil, 7)});
        QTRY_VERIFY_WITH_TIMEOUT(!m_h->batches.isEmpty(), 5000);
        QCOMPARE(m_h->batches.last().samples.at(0).value, 0.0);
        m_server.setCoil(7, true);
        QTRY_COMPARE_WITH_TIMEOUT(m_h->batches.last().samples.at(0).value, 1.0, 3000);
    }

    void blocksSplitAtMaxBitCount()
    {
        QList<Tag> tags;
        TagId id = 1;
        for (int addr = 0; addr <= 2100; addr += 60)
            tags.append(makeTag(id++, Area::DiscreteInput, static_cast<quint16>(addr)));
        startAll(tags);
        QTRY_VERIFY_WITH_TIMEOUT(!m_h->batches.isEmpty(), 5000);
        for (const TagSample &s : m_h->batches.last().samples) {
            QVERIFY(s.quality == Quality::Good);
            QCOMPARE(s.raw, quint32((tags.at(s.tagId - 1).address) % 2 == 1 ? 1 : 0));
        }
        const QList<Request> reqs = m_server.requestsOf(2);
        QVERIFY(reqs.size() >= 2);
        QCOMPARE(reqs.at(0).address, 0);
        QCOMPARE(reqs.at(0).count, 1981);
        QCOMPARE(reqs.at(1).address, 2040);
        QCOMPARE(reqs.at(1).count, 61);
    }

    void writeSingleCoilUsesFc05()
    {
        startAll({makeTag(1, Area::Coil, 10)});
        QVERIFY(!m_server.coil(10));
        m_h->writeCoils(11, 10, {1});
        QTRY_VERIFY_WITH_TIMEOUT(m_h->writes.contains(11), 3000);
        QVERIFY2(m_h->writes.value(11).ok(), qPrintable(m_h->writes.value(11).message));
        QVERIFY(m_server.coil(10));
        QCOMPARE(m_server.requestsOf(5).size(), 1);
        QCOMPARE(m_server.requestsOf(5).first().address, 10);
        QVERIFY(m_server.requestsOf(15).isEmpty());
        m_h->writeCoils(12, 10, {0});
        QTRY_VERIFY_WITH_TIMEOUT(m_h->writes.contains(12), 3000);
        QVERIFY(m_h->writes.value(12).ok());
        QVERIFY(!m_server.coil(10));
    }

    void writeSeveralCoilsUsesOneFc15()
    {
        startAll({makeTag(1, Area::Coil, 20)});
        m_h->writeCoils(21, 20, {1, 0, 1, 1}); // 20 is 20 % 3 != 0: all four change or hold
        QTRY_VERIFY_WITH_TIMEOUT(m_h->writes.contains(21), 3000);
        QVERIFY2(m_h->writes.value(21).ok(), qPrintable(m_h->writes.value(21).message));
        QVERIFY(m_server.coil(20));
        QVERIFY(!m_server.coil(21));
        QVERIFY(m_server.coil(22));
        QVERIFY(m_server.coil(23));
        QCOMPARE(m_server.requestsOf(15).size(), 1);
        QCOMPARE(m_server.requestsOf(15).first().address, 20);
        QCOMPARE(m_server.requestsOf(15).first().count, 4);
        QVERIFY(m_server.requestsOf(5).isEmpty());
    }

    void fc15ExceptionOneCarriesHint()
    {
        startAll({makeTag(1, Area::Coil, 20)});
        m_server.refuseFc15(true);
        m_h->writeCoils(31, 20, {1, 1});
        QTRY_VERIFY_WITH_TIMEOUT(m_h->writes.contains(31), 3000);
        const ModbusResult r = m_h->writes.value(31);
        QVERIFY(!r.ok());
        QVERIFY(r.error == ModbusError::Exception);
        QCOMPARE(r.exceptionCode, 1);
        QVERIFY2(r.message.endsWith(QStringLiteral(" The device does not accept FC15 (write multiple coils).")),
                 qPrintable(r.message));
        // Still connected and a single write goes through.
        m_h->writeCoils(32, 20, {1});
        QTRY_VERIFY_WITH_TIMEOUT(m_h->writes.contains(32), 3000);
        QVERIFY(m_h->writes.value(32).ok());
    }

    void writeCoilsWhileDisconnectedFails()
    {
        auto h = std::make_unique<WorkerHarness>();
        h->writeCoils(1, 0, {1});
        QTRY_VERIFY_WITH_TIMEOUT(h->writes.contains(1), 3000);
        QVERIFY(h->writes.value(1).error == ModbusError::NotConnected);
    }

    void oneOffBitReads()
    {
        startAll({});
        m_h->read(41, Area::Coil, 0, 6);
        m_h->read(42, Area::DiscreteInput, 1, 3);
        m_h->read(43, Area::DiscreteInput, 0, 2000);
        m_h->read(44, Area::Coil, 0, 2001);
        m_h->read(45, Area::Coil, 0, 0);
        m_h->read(46, Area::HoldingRegister, 0, 126);
        m_h->read(47, Area::Coil, 65500, 100);
        m_h->read(48, Area::HoldingRegister, 5, 1);
        QTRY_COMPARE_WITH_TIMEOUT(m_h->reads.size(), 8, 5000);

        QVERIFY(m_h->readOf(41)->result.ok());
        QCOMPARE(m_h->readOf(41)->values, (QList<quint16>{1, 0, 0, 1, 0, 0}));
        QCOMPARE(m_h->readOf(42)->values, (QList<quint16>{1, 0, 1}));
        QVERIFY(m_h->readOf(43)->result.ok());
        QCOMPARE(m_h->readOf(43)->values.size(), qsizetype(2000));
        for (const quint64 id : {44, 45, 46, 47}) {
            QVERIFY(m_h->readOf(id)->result.error == ModbusError::InvalidArgument);
            QVERIFY(m_h->readOf(id)->values.isEmpty());
        }
        QCOMPARE(m_h->readOf(48)->values, (QList<quint16>{1234}));
        QCOMPARE(m_server.requestsOf(1).size(), 1);
    }

    void failingBitBlockFallsBackPerTag()
    {
        m_server.setBadBit(31);
        startAll({makeTag(1, Area::Coil, 30), makeTag(2, Area::Coil, 31), makeTag(3, Area::Coil, 32)});
        QTRY_VERIFY_WITH_TIMEOUT(!m_h->batches.isEmpty(), 5000);
        const SampleBatch &b = m_h->batches.last();
        QVERIFY(m_h->sampleOf(b, 1)->quality == Quality::Good);
        QVERIFY(m_h->sampleOf(b, 2)->quality == Quality::Exception);
        QVERIFY(m_h->sampleOf(b, 3)->quality == Quality::Good);
        QCOMPARE(m_h->sampleOf(b, 1)->value, 1.0); // 30 % 3 == 0
        QCOMPARE(m_h->sampleOf(b, 3)->value, 0.0);
        QVERIFY(std::any_of(m_server.requestsOf(1).begin(), m_server.requestsOf(1).end(),
                            [](const Request &r) { return r.address == 30 && r.count == 3; }));
        QVERIFY(std::any_of(m_server.requestsOf(1).begin(), m_server.requestsOf(1).end(),
                            [](const Request &r) { return r.address == 32 && r.count == 1; }));
        QTRY_VERIFY_WITH_TIMEOUT(
            std::any_of(m_h->messages.begin(), m_h->messages.end(),
                        [](const QString &m) { return m.startsWith(QStringLiteral("Read CO 31..31 failed")); }),
            3000);
    }
};

QTEST_GUILESS_MAIN(TstPollBits)
#include "tst_pollbits.moc"
