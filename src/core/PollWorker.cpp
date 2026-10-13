#include "PollWorker.h"

#include "TagCodec.h"

#include <QTimer>

#include <algorithm>
#include <iterator>
#include <utility>

namespace mb {

namespace {

constexpr int kBackoffMs[] = {500, 1000, 2000, 4000, 8000, 10000};
constexpr int kBackoffSteps = static_cast<int>(std::size(kBackoffMs));
constexpr int kMinPollMs = 100;
constexpr qint64 kMessageRepeatMs = 10000;

Quality qualityFor(ModbusError e)
{
    switch (e) {
    case ModbusError::Timeout: return Quality::Timeout;
    case ModbusError::Exception: return Quality::Exception;
    case ModbusError::ConnectionLost:
    case ModbusError::NotConnected: return Quality::Disconnected;
    default: return Quality::Error;
    }
}

ModbusResult notConnectedResult()
{
    ModbusResult r;
    r.error = ModbusError::NotConnected;
    r.message = QStringLiteral("Not connected");
    return r;
}

QString areaShort(Area a)
{
    return a == Area::InputRegister ? QStringLiteral("IR") : QStringLiteral("HR");
}

} // namespace

QString toString(ConnectionState state)
{
    switch (state) {
    case ConnectionState::Disconnected: return QStringLiteral("Disconnected");
    case ConnectionState::Connecting: return QStringLiteral("Connecting");
    case ConnectionState::Connected: return QStringLiteral("Connected");
    case ConnectionState::Reconnecting: return QStringLiteral("Reconnecting");
    }
    return {};
}

PollWorker::PollWorker(QObject *parent)
    : QObject(parent)
    , m_conn(std::make_unique<ModbusConnection>())
    , m_timer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &PollWorker::onPollTimer);
    connect(m_reconnectTimer, &QTimer::timeout, this, &PollWorker::onReconnectTimer);
}

PollWorker::~PollWorker()
{
    stopTimers();
    if (m_conn)
        m_conn->close();
}

int PollWorker::periodMs() const
{
    return std::max(m_settings.pollPeriodMs, kMinPollMs);
}

QString PollWorker::endpoint() const
{
    return QStringLiteral("%1:%2").arg(m_settings.host).arg(m_settings.port);
}

void PollWorker::setState(ConnectionState state, const QString &detail)
{
    m_state = state;
    emit stateChanged(state, detail);
}

void PollWorker::stopTimers()
{
    m_timer->stop();
    m_reconnectTimer->stop();
}

void PollWorker::closeConnection()
{
    m_conn->close();
}

void PollWorker::resetTimeBase()
{
    m_clock.restart();
    const QDateTime wall = QDateTime::currentDateTime();
    emit timeBaseReset(wall);
}

void PollWorker::setAddressBase(int base)
{
    m_addressBase = base == 1 ? 1 : 0;
}

void PollWorker::setTags(const QList<Tag> &tags)
{
    m_tags = tags;
    m_tagIndex.clear();
    for (int i = 0; i < m_tags.size(); ++i)
        m_tagIndex.insert(m_tags.at(i).id, i);
    m_blocks = planBlocks(m_tags);
    m_perTagBlocks.clear();
}

void PollWorker::connectTo(const ConnectionSettings &settings)
{
    m_settings = settings;
    stopTimers();
    closeConnection();
    m_backoffStep = 0;
    m_lastBlockMessage.clear();
    m_perTagBlocks.clear();
    failAllWrites(notConnectedResult());
    failAllReads(notConnectedResult());
    if (!m_clock.isValid())
        resetTimeBase();
    attemptConnect(true);
}

int PollWorker::nextBackoffMs()
{
    const int ms = kBackoffMs[std::min(m_backoffStep, kBackoffSteps - 1)];
    if (m_backoffStep < kBackoffSteps - 1)
        ++m_backoffStep;
    return ms;
}

void PollWorker::attemptConnect(bool firstAttempt)
{
    if (firstAttempt)
        setState(ConnectionState::Connecting, QStringLiteral("Connecting to %1").arg(endpoint()));

    const ModbusResult r = m_conn->open(m_settings);
    if (r.ok()) {
        m_backoffStep = 0;
        m_perTagBlocks.clear();
        m_reconnectTimer->stop();
        setState(ConnectionState::Connected,
                 QStringLiteral("Connected to %1 (unit %2)").arg(endpoint()).arg(m_settings.unitId));
        m_timer->start(0); // first cycle immediately
        return;
    }

    if (firstAttempt) {
        enterReconnecting(r.message);
    } else {
        // Still reconnecting: keep the state, report the failed attempt, schedule the next.
        const int ms = nextBackoffMs();
        emit message(QStringLiteral("Reconnect to %1 failed: %2 (retry in %3 s)")
                         .arg(endpoint(), r.message)
                         .arg(ms / 1000.0));
        m_reconnectTimer->start(ms);
    }
}

void PollWorker::enterReconnecting(const QString &reason)
{
    closeConnection();
    const int ms = nextBackoffMs();
    const QString detail = QStringLiteral("%1 (retry in %2 s)").arg(reason).arg(ms / 1000.0);
    // Fail queued writes before the state change: listeners drop pending requests on it.
    failAllWrites(notConnectedResult());
    failAllReads(notConnectedResult());
    setState(ConnectionState::Reconnecting, detail);
    emit message(QStringLiteral("Connection to %1 failed: %2").arg(endpoint(), detail));
    m_reconnectTimer->start(ms);
    // Gap batches: one all-Disconnected batch per poll period.
    m_timer->start(periodMs());
}

void PollWorker::onReconnectTimer()
{
    if (m_state == ConnectionState::Reconnecting)
        attemptConnect(false);
}

void PollWorker::disconnectFrom()
{
    stopTimers();
    closeConnection();
    m_writePassQueued = false;
    failAllWrites(notConnectedResult());
    failAllReads(notConnectedResult());
    setState(ConnectionState::Disconnected, QStringLiteral("Disconnected"));
}

void PollWorker::failAllWrites(const ModbusResult &result)
{
    const QList<PendingWrite> writes = std::exchange(m_writes, {});
    for (const PendingWrite &w : writes)
        emit writeFinished(w.id, result);
}

void PollWorker::writeRegisters(quint64 requestId, quint16 address, const QList<quint16> &values)
{
    if (m_state != ConnectionState::Connected || !m_conn->isOpen()) {
        emit writeFinished(requestId, notConnectedResult());
        return;
    }
    m_writes.append({requestId, address, values});
    if (!m_writePassQueued) {
        m_writePassQueued = true;
        QTimer::singleShot(0, this, &PollWorker::drainWrites);
    }
}

void PollWorker::drainWrites()
{
    m_writePassQueued = false;
    if (m_state == ConnectionState::Connected)
        servePending();
}

ModbusResult PollWorker::withFc16Hint(ModbusResult result)
{
    if (result.error == ModbusError::Exception && result.exceptionCode == 1) {
        result.message += QStringLiteral(
            " The device does not accept FC16 (write multiple registers), which 32-bit values and "
            "multi-register writes need.");
    }
    return result;
}

void PollWorker::serveWrites()
{
    while (!m_writes.isEmpty()) {
        const PendingWrite w = m_writes.takeFirst();
        ModbusResult r;
        if (m_state != ConnectionState::Connected || !m_conn->isOpen())
            r = notConnectedResult();
        else if (w.values.size() == 1)
            r = m_conn->writeSingle(w.address, w.values.first());
        else
            r = withFc16Hint(m_conn->writeMultiple(w.address, w.values));

        // Discard anything left in the socket so a late reply is not read as the next answer.
        if (!r.ok() && r.error != ModbusError::Exception)
            m_conn->flush();
        emit writeFinished(w.id, r);
        if (r.error == ModbusError::ConnectionLost) {
            enterReconnecting(r.message); // also fails the remaining writes
            return;
        }
    }
}

void PollWorker::emitBlockMessage(const QString &text)
{
    if (text == m_lastBlockMessage && m_lastBlockMessageTime.isValid()
        && m_lastBlockMessageTime.elapsed() < kMessageRepeatMs) {
        return;
    }
    m_lastBlockMessage = text;
    m_lastBlockMessageTime.restart();
    emit message(text);
}

void PollWorker::emitDisconnectedBatch(double t)
{
    if (m_tags.isEmpty())
        return;
    SampleBatch batch;
    batch.t = t;
    batch.samples.reserve(m_tags.size());
    for (const Tag &tag : std::as_const(m_tags)) {
        TagSample s;
        s.tagId = tag.id;
        s.quality = tag.fitsAddressSpace() ? Quality::Disconnected : Quality::Error;
        batch.samples.append(s);
    }
    emit samplesReady(batch);
}

void PollWorker::scheduleNextPoll(double cycleStartMs)
{
    const double spent = nowMs() - cycleStartMs;
    m_timer->start(static_cast<int>(std::max(0.0, periodMs() - spent)));
}

void PollWorker::onPollTimer()
{
    const double startMs = nowMs();
    const double t = startMs / 1000.0;
    if (m_state == ConnectionState::Reconnecting) {
        emitDisconnectedBatch(t);
        scheduleNextPoll(startMs);
        return;
    }
    if (m_state != ConnectionState::Connected)
        return;

    servePending();
    if (m_state == ConnectionState::Connected)
        pollCycle(t);
    else if (m_state == ConnectionState::Reconnecting)
        emitDisconnectedBatch(t); // a write hit ConnectionLost: this cycle is a gap

    // enterReconnecting() arms its own timer; only a healthy cycle re-arms here.
    if (m_state == ConnectionState::Connected)
        scheduleNextPoll(startMs);
}

void PollWorker::pollCycle(double t)
{
    if (m_tags.isEmpty())
        return;

    SampleBatch batch;
    batch.t = t;
    batch.samples.resize(m_tags.size());
    for (int i = 0; i < m_tags.size(); ++i) {
        batch.samples[i].tagId = m_tags.at(i).id;
        batch.samples[i].quality = Quality::Disconnected;
    }

    // Tags the planner left out (they do not fit the address space) are never read.
    for (int i = 0; i < m_tags.size(); ++i) {
        if (!m_tags.at(i).fitsAddressSpace())
            batch.samples[i].quality = Quality::Error;
    }

    bool lost = false;
    QString lostReason;

    // Decodes the tag at `idx` from `data`, where its first register is at data[off].
    const auto decodeInto = [&](int idx, const QList<quint16> &data, int off) {
        TagSample &s = batch.samples[idx];
        const Tag &tag = m_tags.at(idx);
        const int regs = registerCount(tag.type);
        if (off + regs > data.size()) {
            s.quality = Quality::Error;
            return;
        }
        const quint16 r0 = data.at(off);
        const quint16 r1 = regs == 2 ? data.at(off + 1) : quint16(0);
        s.raw = combineRegisters(tag, r0, r1);
        s.value = decodeValue(tag, s.raw);
        s.quality = Quality::Good;
    };

    // Reports a failed request (one message per request) and discards stale socket data.
    // Returns true when the connection was lost.
    const auto failed = [&](const ModbusResult &r, Area area, int start, int count) {
        emitBlockMessage(QStringLiteral("Read %1 %2..%3 failed: %4")
                             .arg(areaShort(area))
                             .arg(start + m_addressBase)
                             .arg(start + count - 1 + m_addressBase)
                             .arg(r.message));
        // Discard anything left in the socket so a late reply is not read as the next answer.
        if (r.error != ModbusError::Exception)
            m_conn->flush();
        if (r.error == ModbusError::ConnectionLost) {
            lost = true;
            lostReason = r.message;
            return true;
        }
        return false;
    };

    // Reads each tag of the block on its own, so every tag gets its own quality.
    const auto readPerTag = [&](const ReadBlock &block) {
        for (int k = 0; k < block.tagIds.size(); ++k) {
            const int idx = m_tagIndex.value(block.tagIds.at(k), -1);
            if (idx < 0)
                continue;
            const int regs = registerCount(m_tags.at(idx).type);
            const int start = block.start + block.offsets.at(k);
            QList<quint16> data;
            const ModbusResult r = m_conn->readRegisters(block.area, static_cast<quint16>(start),
                                                         static_cast<quint16>(regs), data);
            if (r.ok()) {
                decodeInto(idx, data, 0);
                continue;
            }
            batch.samples[idx].quality = qualityFor(r.error);
            if (failed(r, block.area, start, regs))
                return false;
        }
        return true;
    };

    for (int bi = 0; bi < m_blocks.size(); ++bi) {
        const ReadBlock &block = m_blocks.at(bi);
        if (block.tagIds.size() > 1 && m_perTagBlocks.contains(bi)) {
            if (!readPerTag(block))
                break; // remaining tags stay Disconnected
            continue;
        }

        QList<quint16> data;
        const ModbusResult r = m_conn->readRegisters(block.area, block.start, block.count, data);
        if (r.ok()) {
            for (int k = 0; k < block.tagIds.size(); ++k) {
                const int idx = m_tagIndex.value(block.tagIds.at(k), -1);
                if (idx >= 0)
                    decodeInto(idx, data, block.offsets.at(k));
            }
            continue;
        }

        // A device may refuse some registers of a block (e.g. one half of a 32-bit value) while
        // serving the others: read the tags one by one, now and in later cycles.
        if (r.error == ModbusError::Exception && block.tagIds.size() > 1) {
            m_perTagBlocks.insert(bi);
            if (!readPerTag(block))
                break;
            continue;
        }

        const Quality q = qualityFor(r.error);
        for (const TagId id : block.tagIds) {
            const int idx = m_tagIndex.value(id, -1);
            if (idx >= 0)
                batch.samples[idx].quality = q;
        }
        if (failed(r, block.area, block.start, block.count))
            break; // remaining tags stay Disconnected
    }

    emit samplesReady(batch);

    if (lost)
        enterReconnecting(lostReason);
}

void PollWorker::failAllReads(const ModbusResult &result)
{
    const QList<PendingRead> reads = std::exchange(m_reads, {});
    for (const PendingRead &rd : reads)
        emit readFinished(rd.id, result, {});
}

void PollWorker::readRegisters(quint64 requestId, Area area, quint16 address, int count)
{
    if (m_state != ConnectionState::Connected || !m_conn->isOpen()) {
        emit readFinished(requestId, notConnectedResult(), {});
        return;
    }
    if (count < 1 || count > 125 || static_cast<int>(address) + count > 65536) {
        ModbusResult r;
        r.error = ModbusError::InvalidArgument;
        r.message = QStringLiteral("Invalid read: %1 registers from %2 (1..125 registers, within 0..65535)")
                        .arg(count)
                        .arg(address + m_addressBase);
        emit readFinished(requestId, r, {});
        return;
    }
    m_reads.append({requestId, area, address, count});
    if (!m_writePassQueued) {
        m_writePassQueued = true;
        QTimer::singleShot(0, this, &PollWorker::drainWrites);
    }
}

// One-off reads are served after the queued writes, in request order. They never touch tag
// qualities, samples or the status bar: an error is only reported through readFinished, except
// ConnectionLost, which is handled like a failed poll.
void PollWorker::serveReads()
{
    while (!m_reads.isEmpty()) {
        const PendingRead rd = m_reads.takeFirst();
        ModbusResult r;
        QList<quint16> data;
        if (m_state != ConnectionState::Connected || !m_conn->isOpen())
            r = notConnectedResult();
        else
            r = m_conn->readRegisters(rd.area, rd.address, static_cast<quint16>(rd.count), data);

        if (!r.ok()) {
            data.clear();
            // Discard anything left in the socket so a late reply is not read as the next answer.
            if (r.error != ModbusError::Exception)
                m_conn->flush();
        }
        emit readFinished(rd.id, r, data);
        if (r.error == ModbusError::ConnectionLost) {
            enterReconnecting(r.message); // also fails the remaining reads and writes
            return;
        }
    }
}

void PollWorker::servePending()
{
    serveWrites();
    if (m_state == ConnectionState::Connected)
        serveReads();
}

} // namespace mb
