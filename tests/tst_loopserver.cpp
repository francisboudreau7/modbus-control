#include "LoopController.h"
#include "LoopServer.h"

#include <modbus.h>

#include <QElapsedTimer>
#include <QtTest>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

using namespace mbloop;

namespace {

/// libmodbus client on 127.0.0.1:port.
class Client
{
public:
    explicit Client(quint16 port, int unit = 1)
    {
        m_ctx = modbus_new_tcp("127.0.0.1", port);
        if (!m_ctx)
            return;
        modbus_set_slave(m_ctx, unit);
        modbus_set_response_timeout(m_ctx, 2, 0);
        if (modbus_connect(m_ctx) == -1) {
            modbus_free(m_ctx);
            m_ctx = nullptr;
        }
    }
    ~Client()
    {
        if (m_ctx) {
            modbus_close(m_ctx);
            modbus_free(m_ctx);
        }
    }
    bool connected() const { return m_ctx != nullptr; }
    modbus_t *ctx() const { return m_ctx; }

    static float toFloat(const uint16_t *r)
    {
        const std::uint32_t bits = (std::uint32_t(r[0]) << 16) | r[1];
        float f = 0;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }
    static void fromFloat(float f, uint16_t *r)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof(bits));
        r[0] = static_cast<uint16_t>(bits >> 16);
        r[1] = static_cast<uint16_t>(bits & 0xFFFF);
    }

    bool readPv(float *pv)
    {
        uint16_t r[2];
        if (modbus_read_input_registers(m_ctx, 0, 2, r) != 2)
            return false;
        *pv = toFloat(r);
        return true;
    }
    bool readSpOut(float *sp, float *out)
    {
        uint16_t r[4];
        if (modbus_read_registers(m_ctx, 0, 4, r) != 4)
            return false;
        *sp = toFloat(r);
        *out = toFloat(r + 2);
        return true;
    }
    int writeFloat(int address, float value)
    {
        uint16_t r[2];
        fromFloat(value, r);
        return modbus_write_registers(m_ctx, address, 2, r);
    }

private:
    modbus_t *m_ctx = nullptr;
};

constexpr int kIllegalAddress = EMBXILADD;
constexpr int kIllegalValue = EMBXILVAL;

} // namespace

class TstLoopServer : public QObject
{
    Q_OBJECT
private slots:
    void startAndPort();
    void startTwiceAndPortInUse();
    void readsPublishedValues();
    void writeOutAndSp();
    void writeBoth();
    void invalidRequests();
    void invalidValues();
    void unframedFunctionCodes();
    void resetAndScenarioDropPendingWrites();
    void anyUnitId();
    void clientCountFollowsConnections();
    void stopWithClientIsQuick();

    void controllerAdvancesByWallTimeAndSpeed();
    void controllerSpeedRounding();
    void controllerPauseStopsTime();
    void controllerModbusWritesReachModel();
    void controllerFaceplateSetPublishesAtOnce();
    void controllerCurrentScenarioAndReset();
};

void TstLoopServer::startAndPort()
{
    LoopServer server;
    QCOMPARE(server.port(), quint16(0));
    QString error;
    QVERIFY2(server.start(QStringLiteral("127.0.0.1"), 0, &error), qPrintable(error));
    QVERIFY(server.port() != 0);
    server.stop();
    QCOMPARE(server.port(), quint16(0));
    // restartable
    QVERIFY2(server.start(QStringLiteral("127.0.0.1"), 0, &error), qPrintable(error));
    Client c(server.port());
    QVERIFY(c.connected());
}

void TstLoopServer::startTwiceAndPortInUse()
{
    LoopServer a;
    QString error;
    QVERIFY(a.start(QStringLiteral("127.0.0.1"), 0, &error));
    QVERIFY(!a.start(QStringLiteral("127.0.0.1"), 0, &error));
    QVERIFY(!error.isEmpty());

    LoopServer b;
    error.clear();
    QVERIFY(!b.start(QStringLiteral("127.0.0.1"), a.port(), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(b.port(), quint16(0));
}

void TstLoopServer::readsPublishedValues()
{
    LoopServer server;
    QVERIFY(server.start(QStringLiteral("127.0.0.1"), 0));
    server.publish(37.5f, 42.25f, 61.0f);
    Client c(server.port());
    QVERIFY(c.connected());
    float pv = 0, sp = 0, out = 0;
    QVERIFY(c.readPv(&pv));
    QCOMPARE(pv, 37.5f);
    QVERIFY(c.readSpOut(&sp, &out));
    QCOMPARE(sp, 42.25f);
    QCOMPARE(out, 61.0f);

    // raw word order: ABCD, high word first (37.5f = 0x42160000)
    uint16_t raw[2];
    QCOMPARE(modbus_read_input_registers(c.ctx(), 0, 2, raw), 2);
    QCOMPARE(int(raw[0]), 0x4216);
    QCOMPARE(int(raw[1]), 0x0000);

    // OUT alone and SP alone
    uint16_t r2[2];
    QCOMPARE(modbus_read_registers(c.ctx(), 2, 2, r2), 2);
    QCOMPARE(Client::toFloat(r2), 61.0f);
    QCOMPARE(modbus_read_registers(c.ctx(), 0, 2, r2), 2);
    QCOMPARE(Client::toFloat(r2), 42.25f);

    server.publish(10.0f, 20.0f, 30.0f);
    QVERIFY(c.readPv(&pv));
    QCOMPARE(pv, 10.0f);
    QVERIFY(c.readSpOut(&sp, &out));
    QCOMPARE(sp, 20.0f);
    QCOMPARE(out, 30.0f);
}

void TstLoopServer::writeOutAndSp()
{
    LoopServer server;
    QVERIFY(server.start(QStringLiteral("127.0.0.1"), 0));
    server.publish(50.0f, 50.0f, 50.0f);
    Client c(server.port());
    QVERIFY(c.connected());

    LoopServer::Writes w = server.takeWrites();
    QVERIFY(!w.spWritten && !w.outWritten);

    QCOMPARE(c.writeFloat(2, 72.5f), 2);
    w = server.takeWrites();
    QVERIFY(w.outWritten);
    QVERIFY(!w.spWritten);
    QCOMPARE(w.out, 72.5f);
    QVERIFY(!server.takeWrites().outWritten); // cleared

    float sp = 0, out = 0;
    QVERIFY(c.readSpOut(&sp, &out));
    QCOMPARE(out, 72.5f); // read back at once, before any publish
    QCOMPARE(sp, 50.0f);

    QCOMPARE(c.writeFloat(0, 33.0f), 2);
    w = server.takeWrites();
    QVERIFY(w.spWritten);
    QVERIFY(!w.outWritten);
    QCOMPARE(w.sp, 33.0f);
    QVERIFY(c.readSpOut(&sp, &out));
    QCOMPARE(sp, 33.0f);
    QCOMPARE(out, 72.5f);

    // boundaries are accepted
    QCOMPARE(c.writeFloat(2, 0.0f), 2);
    QCOMPARE(c.writeFloat(2, 100.0f), 2);
    QVERIFY(server.takeWrites().outWritten);
}

void TstLoopServer::writeBoth()
{
    LoopServer server;
    QVERIFY(server.start(QStringLiteral("127.0.0.1"), 0));
    Client c(server.port());
    QVERIFY(c.connected());
    uint16_t r[4];
    Client::fromFloat(11.0f, r);
    Client::fromFloat(22.0f, r + 2);
    QCOMPARE(modbus_write_registers(c.ctx(), 0, 4, r), 4);
    const LoopServer::Writes w = server.takeWrites();
    QVERIFY(w.spWritten && w.outWritten);
    QCOMPARE(w.sp, 11.0f);
    QCOMPARE(w.out, 22.0f);
}

void TstLoopServer::invalidRequests()
{
    LoopServer server;
    QVERIFY(server.start(QStringLiteral("127.0.0.1"), 0));
    server.publish(1.0f, 2.0f, 3.0f);
    Client c(server.port());
    QVERIFY(c.connected());

    uint16_t buf[8] = {};
    // FC04 count 1, or not starting at 0, or too many
    errno = 0;
    QCOMPARE(modbus_read_input_registers(c.ctx(), 0, 1, buf), -1);
    QCOMPARE(errno, kIllegalAddress);
    QCOMPARE(modbus_read_input_registers(c.ctx(), 1, 1, buf), -1);
    QCOMPARE(errno, kIllegalAddress);
    QCOMPARE(modbus_read_input_registers(c.ctx(), 0, 4, buf), -1);
    QCOMPARE(errno, kIllegalAddress);
    // FC03 at 1, odd count, beyond the map, address 4
    QCOMPARE(modbus_read_registers(c.ctx(), 1, 2, buf), -1);
    QCOMPARE(errno, kIllegalAddress);
    QCOMPARE(modbus_read_registers(c.ctx(), 0, 1, buf), -1);
    QCOMPARE(errno, kIllegalAddress);
    QCOMPARE(modbus_read_registers(c.ctx(), 0, 3, buf), -1);
    QCOMPARE(errno, kIllegalAddress);
    QCOMPARE(modbus_read_registers(c.ctx(), 2, 4, buf), -1);
    QCOMPARE(errno, kIllegalAddress);
    QCOMPARE(modbus_read_registers(c.ctx(), 4, 2, buf), -1);
    QCOMPARE(errno, kIllegalAddress);
    // FC06
    QCOMPARE(modbus_write_register(c.ctx(), 2, 0x4000), -1);
    QCOMPARE(errno, kIllegalAddress);
    // FC16: one register, split, wrong address
    QCOMPARE(modbus_write_registers(c.ctx(), 2, 1, buf), -1);
    QCOMPARE(errno, kIllegalAddress);
    QCOMPARE(modbus_write_registers(c.ctx(), 1, 2, buf), -1);
    QCOMPARE(errno, kIllegalAddress);
    QCOMPARE(modbus_write_registers(c.ctx(), 4, 2, buf), -1);
    QCOMPARE(errno, kIllegalAddress);
    // other function codes
    uint8_t bits[2];
    QCOMPARE(modbus_read_bits(c.ctx(), 0, 2, bits), -1);
    QCOMPARE(errno, kIllegalAddress);
    QCOMPARE(modbus_write_bit(c.ctx(), 0, 1), -1);
    QCOMPARE(errno, kIllegalAddress);
    QCOMPARE(modbus_write_and_read_registers(c.ctx(), 0, 2, buf, 0, 2, buf + 4), -1);
    QCOMPARE(errno, kIllegalAddress);

    // nothing was written, the connection is still fine
    QVERIFY(!server.takeWrites().outWritten);
    float sp = 0, out = 0;
    QVERIFY(c.readSpOut(&sp, &out));
    QCOMPARE(sp, 2.0f);
    QCOMPARE(out, 3.0f);
}

void TstLoopServer::unframedFunctionCodes()
{
    LoopServer server;
    QVERIFY(server.start(QStringLiteral("127.0.0.1"), 0));
    server.publish(1.0f, 2.0f, 3.0f);
    Client c(server.port());
    QVERIFY(c.connected());

    // libmodbus does not know how long these are: the rest of the MBAP frame must be dropped.
    const uint8_t fc43[] = {1, 0x2B, 0x0E, 0x01, 0x00}; // read device identification
    const uint8_t fc08[] = {1, 0x08, 0x00, 0x00, 0xA5, 0x37, 0x12, 0x34}; // diagnostics, long data
    const struct { const uint8_t *req; int len; uint8_t fc; } cases[] = {
        {fc43, int(sizeof(fc43)), 0x2B}, {fc08, int(sizeof(fc08)), 0x08}};
    for (int round = 0; round < 2; ++round) {
        for (const auto &t : cases) {
            uint8_t rsp[MODBUS_TCP_MAX_ADU_LENGTH] = {};
            QVERIFY(modbus_send_raw_request(c.ctx(), t.req, t.len) > 0);
            const int rc = modbus_receive_confirmation(c.ctx(), rsp);
            QVERIFY2(rc >= 9 || rc == -1, "reply expected");
            if (rc >= 9) {
                QCOMPARE(int(rsp[7]), 0x80 | t.fc);
                QCOMPARE(int(rsp[8]), 2);
            } else {
                QCOMPARE(errno, kIllegalAddress); // libmodbus reports the exception as an error
            }
            // the connection is still in step: a normal read right after works
            float sp = 0, out = 0;
            QVERIFY(c.readSpOut(&sp, &out));
            QCOMPARE(sp, 2.0f);
            QCOMPARE(out, 3.0f);
        }
    }
    QCOMPARE(server.clientCount(), 1);
}

void TstLoopServer::invalidValues()
{
    LoopServer server;
    QVERIFY(server.start(QStringLiteral("127.0.0.1"), 0));
    server.publish(1.0f, 20.0f, 30.0f);
    Client c(server.port());
    QVERIFY(c.connected());

    const float bad[] = {150.0f, -0.5f, 100.01f, std::numeric_limits<float>::quiet_NaN(),
                         std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
    for (const float v : bad) {
        errno = 0;
        QCOMPARE(c.writeFloat(2, v), -1);
        QCOMPARE(errno, kIllegalValue);
        QCOMPARE(c.writeFloat(0, v), -1);
        QCOMPARE(errno, kIllegalValue);
    }
    // one good and one bad value in one request: nothing is written
    uint16_t r[4];
    Client::fromFloat(55.0f, r);
    Client::fromFloat(150.0f, r + 2);
    QCOMPARE(modbus_write_registers(c.ctx(), 0, 4, r), -1);
    QCOMPARE(errno, kIllegalValue);

    const LoopServer::Writes w = server.takeWrites();
    QVERIFY(!w.spWritten && !w.outWritten);
    float sp = 0, out = 0;
    QVERIFY(c.readSpOut(&sp, &out));
    QCOMPARE(sp, 20.0f);
    QCOMPARE(out, 30.0f);
}

void TstLoopServer::anyUnitId()
{
    LoopServer server;
    QVERIFY(server.start(QStringLiteral("127.0.0.1"), 0));
    server.publish(12.0f, 13.0f, 14.0f);
    for (const int unit : {7, 1, 247, 255}) {
        Client c(server.port(), unit);
        QVERIFY(c.connected());
        float pv = 0;
        QVERIFY2(c.readPv(&pv), qPrintable(QStringLiteral("unit %1").arg(unit)));
        QCOMPARE(pv, 12.0f);
    }
}

void TstLoopServer::clientCountFollowsConnections()
{
    LoopServer server;
    QVERIFY(server.start(QStringLiteral("127.0.0.1"), 0));
    QCOMPARE(server.clientCount(), 0);
    {
        Client a(server.port());
        QVERIFY(a.connected());
        float pv = 0;
        QVERIFY(a.readPv(&pv));
        QCOMPARE(server.clientCount(), 1);
        {
            Client b(server.port());
            QVERIFY(b.connected());
            QVERIFY(b.readPv(&pv));
            QCOMPARE(server.clientCount(), 2);
        }
        QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 1, 3000);
    }
    QTRY_COMPARE_WITH_TIMEOUT(server.clientCount(), 0, 3000);
}

void TstLoopServer::stopWithClientIsQuick()
{
    LoopServer server;
    QVERIFY(server.start(QStringLiteral("127.0.0.1"), 0));
    Client c(server.port());
    QVERIFY(c.connected());
    float pv = 0;
    QVERIFY(c.readPv(&pv));
    QElapsedTimer t;
    t.start();
    server.stop();
    QVERIFY2(t.elapsed() < 1000, qPrintable(QString::number(t.elapsed())));
    QCOMPARE(server.clientCount(), 0);
    QVERIFY(!c.readPv(&pv)); // connection is gone
}

// ---- LoopController ----

void TstLoopServer::controllerAdvancesByWallTimeAndSpeed()
{
    LoopController ctl;
    const double t0 = ctl.model().state().timeS;
    // process time = wall time x speed factor (x1 = real time)
    ctl.tickFor(0.2);
    QVERIFY(std::abs(ctl.model().state().timeS - t0 - 0.2) < 0.02);
    ctl.setSpeedFactor(2);
    ctl.tickFor(0.2);
    QVERIFY(std::abs(ctl.model().state().timeS - t0 - 0.6) < 0.02);
    ctl.setSpeedFactor(5);
    ctl.tickFor(0.1);
    QVERIFY(std::abs(ctl.model().state().timeS - t0 - 1.1) < 0.02);
}

void TstLoopServer::controllerSpeedRounding()
{
    LoopController ctl;
    QSignalSpy spy(&ctl, &LoopController::speedFactorChanged);
    QCOMPARE(ctl.speedFactor(), 1.0);
    ctl.setSpeedFactor(1.0);
    QCOMPARE(spy.count(), 0);
    ctl.setSpeedFactor(2.4);
    QCOMPARE(ctl.speedFactor(), 2.0);
    ctl.setSpeedFactor(4.0);
    QCOMPARE(ctl.speedFactor(), 5.0);
    ctl.setSpeedFactor(7.0);
    QCOMPARE(ctl.speedFactor(), 5.0);
    ctl.setSpeedFactor(10.0); // x10 no longer exists
    QCOMPARE(ctl.speedFactor(), 5.0);
    ctl.setSpeedFactor(100.0);
    QCOMPARE(ctl.speedFactor(), 5.0);
    ctl.setSpeedFactor(-3.0);
    QCOMPARE(ctl.speedFactor(), 1.0);
    QCOMPARE(spy.count(), 3); // 1->2, 2->5, 5->1
}

void TstLoopServer::controllerPauseStopsTime()
{
    LoopController ctl;
    QSignalSpy paused(&ctl, &LoopController::pausedChanged);
    QSignalSpy changed(&ctl, &LoopController::stateChanged);
    QVERIFY(!ctl.isPaused());
    ctl.setPaused(true);
    ctl.setPaused(true);
    QCOMPARE(paused.count(), 1);
    QVERIFY(ctl.isPaused());
    const double t0 = ctl.model().state().timeS;
    ctl.tickFor(1.0);
    QCOMPARE(ctl.model().state().timeS, t0);
    QCOMPARE(changed.count(), 1); // still published and signalled
    ctl.setPaused(false);
    QCOMPARE(paused.count(), 2);
    ctl.tickFor(1.0);
    QVERIFY(std::abs(ctl.model().state().timeS - t0 - 1.0) < 0.02);
}

void TstLoopServer::controllerModbusWritesReachModel()
{
    LoopController ctl;
    QString error;
    QVERIFY2(ctl.startServer(QStringLiteral("127.0.0.1"), 0, &error), qPrintable(error));
    Client c(ctl.server().port());
    QVERIFY(c.connected());

    QCOMPARE(c.writeFloat(2, 30.0f), 2);
    QCOMPARE(c.writeFloat(0, 65.0f), 2);
    ctl.tickFor(0.05);
    QCOMPARE(ctl.model().state().outPct, 30.0);
    QCOMPARE(ctl.model().state().spPct, 65.0);

    // PV published after the tick
    float pv = -1;
    QVERIFY(c.readPv(&pv));
    QCOMPARE(pv, static_cast<float>(ctl.model().state().pvPct));
    float sp = 0, out = 0;
    QVERIFY(c.readSpOut(&sp, &out));
    QCOMPARE(sp, 65.0f);
    QCOMPARE(out, 30.0f);

    // rejected writes never reach the model
    QCOMPARE(c.writeFloat(2, 150.0f), -1);
    ctl.tickFor(0.05);
    QCOMPARE(ctl.model().state().outPct, 30.0);
}

void TstLoopServer::controllerFaceplateSetPublishesAtOnce()
{
    LoopController ctl;
    QString error;
    QVERIFY2(ctl.startServer(QStringLiteral("127.0.0.1"), 0, &error), qPrintable(error));
    Client c(ctl.server().port());
    QVERIFY(c.connected());

    ctl.setOut(70.0);
    ctl.setSp(40.0);
    float sp = 0, out = 0;
    QVERIFY(c.readSpOut(&sp, &out)); // no tick in between
    QCOMPARE(out, 70.0f);
    QCOMPARE(sp, 40.0f);

    // a Modbus write that was not yet ticked does not overwrite a later faceplate change
    QCOMPARE(c.writeFloat(2, 10.0f), 2);
    ctl.setOut(80.0);
    ctl.tickFor(0.05);
    QCOMPARE(ctl.model().state().outPct, 80.0);
    QVERIFY(c.readSpOut(&sp, &out));
    QCOMPARE(out, 80.0f);

    ctl.setPumpSpeed(60.0);
    ctl.setHvOpening(25.0);
    QCOMPARE(ctl.model().params().pumpSpeedPct, 60.0);
    QCOMPARE(ctl.model().params().hvOpeningPct, 25.0);
}

void TstLoopServer::resetAndScenarioDropPendingWrites()
{
    LoopController ctl;
    QString error;
    QVERIFY2(ctl.startServer(QStringLiteral("127.0.0.1"), 0, &error), qPrintable(error));
    Client c(ctl.server().port());
    QVERIFY(c.connected());

    QCOMPARE(c.writeFloat(2, 10.0f), 2); // not ticked yet
    ctl.resetState();
    ctl.tickFor(0.05);
    QCOMPARE(ctl.model().state().outPct, 50.0);
    float sp = 0, out = 0;
    QVERIFY(c.readSpOut(&sp, &out));
    QCOMPARE(out, 50.0f);

    Scenario s;
    s.state = Scenario::State{};
    s.state->levelPct = 40.0;
    s.state->outPct = 20.0;
    s.state->spPct = 60.0;
    s.state->stemPct = 20.0;
    QCOMPARE(c.writeFloat(2, 90.0f), 2);
    QCOMPARE(c.writeFloat(0, 5.0f), 2);
    ctl.loadScenario(s);
    ctl.tickFor(0.05);
    QCOMPARE(ctl.model().state().outPct, 20.0);
    QCOMPARE(ctl.model().state().spPct, 60.0);
    QVERIFY(c.readSpOut(&sp, &out));
    QCOMPARE(out, 20.0f);
    QCOMPARE(sp, 60.0f);
}

void TstLoopServer::controllerCurrentScenarioAndReset()
{
    LoopController ctl;
    ctl.setOut(35.0);
    ctl.setSp(45.0);
    ctl.setPumpSpeed(80.0);
    ctl.setSpeedFactor(2);
    const Scenario s = ctl.currentScenario();
    QCOMPARE(s.params.pumpSpeedPct, 80.0);
    QCOMPARE(s.speedFactor, 2.0);
    QVERIFY(s.state.has_value());
    QCOMPARE(s.state->outPct, 35.0);
    QCOMPARE(s.state->spPct, 45.0);

    QSignalSpy changed(&ctl, &LoopController::stateChanged);
    ctl.resetState();
    QCOMPARE(changed.count(), 1);
    QCOMPARE(ctl.model().params().pumpSpeedPct, 80.0); // settings kept
    QCOMPARE(ctl.model().state().timeS, 0.0);
    QCOMPARE(ctl.speedFactor(), 2.0);

    Scenario other;
    other.speedFactor = 5.0;
    other.params.hvOpeningPct = 30.0;
    QSignalSpy speedSpy(&ctl, &LoopController::speedFactorChanged);
    ctl.loadScenario(other);
    QCOMPARE(ctl.speedFactor(), 5.0);
    QCOMPARE(speedSpy.count(), 1);
    QCOMPARE(changed.count(), 2);
}

QTEST_GUILESS_MAIN(TstLoopServer)
#include "tst_loopserver.moc"
