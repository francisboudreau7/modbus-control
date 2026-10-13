#pragma once

#include "BlockPlanner.h"
#include "ModbusConnection.h"
#include "SampleBatch.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>

#include <memory>

class QTimer;

namespace mb {

enum class ConnectionState : quint8 {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting, ///< connection lost; retrying with backoff (0.5 s doubling, max 10 s)
};

QString toString(ConnectionState state);

/// Owns the ModbusConnection and polls it. Lives on its own QThread: create it, call
/// moveToThread(), and talk to it only through queued signals/slots. All slots are
/// safe to invoke in any state.
///
/// Each poll cycle (every pollPeriodMs, measured start-to-start; a cycle that overruns
/// starts the next one immediately) first serves queued writes, then reads every
/// planned block and emits exactly one samplesReady() with one TagSample per tag.
/// A failed block marks its tags with the matching Quality; a ConnectionLost error
/// ends the cycle, marks remaining tags Disconnected and enters Reconnecting.
/// When a block of more than one tag fails with a Modbus exception (a device may refuse some
/// registers, e.g. one half of a 32-bit value, but serve the others), each of its tags is read
/// on its own in the same cycle so every tag gets its own quality, and the block keeps being
/// read per tag until the next setTags() or (re)connect. Timeouts and lost connections do not
/// trigger this fallback.
class PollWorker : public QObject
{
    Q_OBJECT
public:
    explicit PollWorker(QObject *parent = nullptr);
    ~PollWorker() override;

    /// Result of a multi-register (FC16) write as reported to the client: a Modbus exception 1
    /// (illegal function) gets the hint " The device does not accept FC16 (write multiple
    /// registers), which 32-bit values and multi-register writes need." appended to its message.
    /// Anything else is returned unchanged.
    static ModbusResult withFc16Hint(ModbusResult result);

public slots:
    /// Connects (or reconnects with new settings) and starts polling. Starts a new time
    /// base (emits timeBaseReset) if none exists yet.
    void connectTo(const mb::ConnectionSettings &settings);
    /// Stops polling and closes the connection. Pending writes fail with NotConnected.
    void disconnectFrom();
    /// Replaces the tag list and re-plans the read blocks.
    void setTags(const QList<mb::Tag> &tags);
    /// Starts a new time base: t restarts at 0 and timeBaseReset() is emitted.
    void resetTimeBase();
    /// 0 or 1: register numbers in messages are shown as protocol address + base, matching
    /// the UI's register numbering. Does not affect what is read or written.
    void setAddressBase(int base);
    /// Queues a write of consecutive holding registers starting at `address`;
    /// 1 value -> FC06, more -> ONE FC16 request covering all registers (atomic: the
    /// device never holds half old, half new).
    /// Answered by exactly one writeFinished(requestId, ...).
    void writeRegisters(quint64 requestId, quint16 address, const QList<quint16> &values);
    /// Queues a one-off read of `count` (1..125) consecutive registers of `area` starting at
    /// `address` (address + count <= 65536), served between poll cycles like writes (before the
    /// next cycle's reads). Not part of polling and never shown in samples. Answered by exactly
    /// one readFinished(requestId, ...): on success result.ok() and `values` has `count` registers;
    /// otherwise the error (NotConnected when not Connected, or on disconnect while queued;
    /// InvalidArgument for a bad count/range) and empty `values`. A failed one-off read never
    /// changes the connection state, except ConnectionLost, which is handled like a failed poll.
    void readRegisters(quint64 requestId, mb::Area area, quint16 address, int count);

signals:
    void stateChanged(mb::ConnectionState state, const QString &detail);
    void timeBaseReset(const QDateTime &wallClockAtZero);
    void samplesReady(const mb::SampleBatch &batch);
    void writeFinished(quint64 requestId, const mb::ModbusResult &result);
    void readFinished(quint64 requestId, const mb::ModbusResult &result, const QList<quint16> &values);
    /// Human-readable message for the status bar (block errors, reconnect attempts).
    void message(const QString &text);

private:
    struct PendingWrite {
        quint64 id = 0;
        quint16 address = 0;
        QList<quint16> values;
    };

    struct PendingRead {
        quint64 id = 0;
        Area area = Area::HoldingRegister;
        quint16 address = 0;
        int count = 0;
    };

    void setState(ConnectionState state, const QString &detail);
    int nextBackoffMs();
    void attemptConnect(bool firstAttempt);
    void enterReconnecting(const QString &reason);
    void closeConnection();
    void stopTimers();
    void onPollTimer();
    void onReconnectTimer();
    void scheduleNextPoll(double cycleStartMs);
    void serveWrites();
    void drainWrites();
    void serveReads();
    void servePending(); ///< queued writes, then queued reads
    void failAllReads(const ModbusResult &result);
    void failAllWrites(const ModbusResult &result);
    void emitDisconnectedBatch(double t);
    void pollCycle(double t);
    void emitBlockMessage(const QString &text);
    double nowMs() const { return m_clock.isValid() ? m_clock.nsecsElapsed() / 1.0e6 : 0.0; }
    int periodMs() const;
    QString endpoint() const;

    std::unique_ptr<ModbusConnection> m_conn;
    ConnectionSettings m_settings;
    QList<Tag> m_tags;
    QHash<TagId, int> m_tagIndex;
    QList<ReadBlock> m_blocks;
    QSet<int> m_perTagBlocks; ///< indices into m_blocks that are read tag by tag
    QTimer *m_timer = nullptr;          ///< poll (or disconnected-gap) cycle, single shot
    QTimer *m_reconnectTimer = nullptr; ///< backoff retry, single shot
    QElapsedTimer m_clock;
    ConnectionState m_state = ConnectionState::Disconnected;
    QList<PendingWrite> m_writes;
    QList<PendingRead> m_reads;
    bool m_writePassQueued = false;
    int m_backoffStep = 0;
    int m_addressBase = 0;
    QString m_lastBlockMessage;
    QElapsedTimer m_lastBlockMessageTime;
};
} // namespace mb

Q_DECLARE_METATYPE(mb::ConnectionState)
