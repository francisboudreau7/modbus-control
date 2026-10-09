#pragma once

#include "BlockPlanner.h"
#include "ModbusConnection.h"
#include "SampleBatch.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>

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
class PollWorker : public QObject
{
    Q_OBJECT
public:
    explicit PollWorker(QObject *parent = nullptr);
    ~PollWorker() override;

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
    /// Queues a write of consecutive holding registers starting at `address`;
    /// 1 value -> FC06, more -> FC16, or one FC06 per register (ascending addresses, stop
    /// at the first failure) when ConnectionSettings::singleRegisterWrites is set.
    /// Answered by exactly one writeFinished(requestId, ...).
    void writeRegisters(quint64 requestId, quint16 address, const QList<quint16> &values);

signals:
    void stateChanged(mb::ConnectionState state, const QString &detail);
    void timeBaseReset(const QDateTime &wallClockAtZero);
    void samplesReady(const mb::SampleBatch &batch);
    void writeFinished(quint64 requestId, const mb::ModbusResult &result);
    /// Human-readable message for the status bar (block errors, reconnect attempts).
    void message(const QString &text);

private:
    struct PendingWrite {
        quint64 id = 0;
        quint16 address = 0;
        QList<quint16> values;
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
    QTimer *m_timer = nullptr;          ///< poll (or disconnected-gap) cycle, single shot
    QTimer *m_reconnectTimer = nullptr; ///< backoff retry, single shot
    QElapsedTimer m_clock;
    ConnectionState m_state = ConnectionState::Disconnected;
    QList<PendingWrite> m_writes;
    bool m_writePassQueued = false;
    int m_backoffStep = 0;
    QString m_lastBlockMessage;
    QElapsedTimer m_lastBlockMessageTime;
};
} // namespace mb

Q_DECLARE_METATYPE(mb::ConnectionState)
