#pragma once

#include "ModbusConnection.h"
#include "Tag.h"

#include <QHash>
#include <QList>
#include <QObject>

#include <functional>
#include <optional>

class QWidget;
class TstQuickWrite; // test access to the prompt hooks

namespace mb {

class TagTableModel;

/// Turns user write requests into PollWorker writes and reports the results.
/// Register numbers in all texts use TagTableModel::addressBase() (protocol address + base).
///
/// requestWrite, checks in this order (each failure -> warning box, nothing written):
/// 1. not connected -> message and stop;
/// 2. outside the tag's write limits (Tag::withinWriteLimits) -> warning naming the tag and
///    its limits ("'Setpoint' accepts 0 to 100 °C; 150 was not written.");
/// 3. 32-bit tags (2 registers) need TagTableModel::goodValue(): without a Good reading ->
///    warning "No valid reading of 'X' yet. Check the connection, register number and byte
///    order before writing.";
/// 4. encodeRegisters() fails (out of the type's range) -> warning;
/// 5. float32 tag whose current value is implausible (not finite, |x| > 1e9, or
///    0 < |x| < 1e-6, typical of a wrong byte order or register) -> an extra warning that
///    explains this and needs an explicit "Write anyway" (default button: Cancel);
/// then the confirmation ("Write 12.5 °C (raw 125) to HR 100 'Setpoint'?"; 32-bit: raw as
/// 0x%08X, "HR 6639-6640", type and byte order named) with an extra line "Current value: X"
/// (or "Current value: unknown") and, when limits are set, "Limits: min to max". Then
/// writeRegisters is emitted.
///
/// requestWriteSelection: rows must all be writable holding registers that are contiguous
/// when sorted by address (each tag starts right after the previous tag's last register;
/// 16- and 32-bit tags may be mixed; at most 123 registers in total); otherwise explain why
/// in a warning box. Shows a dialog with one value field per tag (prefilled with the current
/// value), applies checks 2, 3 and 5 to every tag (limits listed together in one warning),
/// encodes all, confirms, and emits one writeRegisters with all registers.
///
/// requestQuickWrite: same checks as requestWrite. 16-bit tags: while quickConfirm() is on the
/// confirmation has a check box "Don't ask again this session"; when it is ticked and the
/// write confirmed, quick confirmation turns off (quickConfirmChanged); while it is off the
/// write is sent without asking. 32-bit tags are always confirmed with the normal
/// confirmation (no check box), whatever quickConfirm() says.
///
/// onWriteFinished: success -> message "Write OK: ..."; failure -> message "Write failed: ..." (no dialog) and
/// TagTableModel::setStatusText() on the affected tags. Exception: a write of more than one
/// register that failed with Modbus exception 1 (illegal function) while singleRegisterWrites()
/// is off also shows a warning (replaceable prompt) explaining that the device probably does
/// not support FC16 and nothing was written, with the buttons "Use FC06 only and reconnect" and
/// "Close"; the first emits singleRegisterWritesRequested(). The value is not written again.
class WriteController : public QObject
{
    Q_OBJECT
public:
    WriteController(TagTableModel *model, QWidget *dialogParent, QObject *parent = nullptr);

public slots:
    void requestWrite(mb::TagId id, double scaledValue);
    void requestWriteSelection(const QList<int> &rows);
    void onWriteFinished(quint64 requestId, const mb::ModbusResult &result);
    void setConnected(bool connected);
    void requestQuickWrite(mb::TagId id, double scaledValue);
    /// Default true (each application start).
    void setQuickConfirm(bool on);
    /// Whether the connection uses FC06 for multi-register values (set on every connect).
    void setSingleRegisterWrites(bool on);

public:
    bool quickConfirm() const;
    bool singleRegisterWrites() const;

signals:
    void quickConfirmChanged(bool on);
    /// The user chose "Use FC06 only and reconnect" after an FC16 rejection.
    void singleRegisterWritesRequested();
    /// Connect to PollWorker::writeRegisters (queued).
    void writeRegisters(quint64 requestId, quint16 address, const QList<quint16> &values);
    /// Status bar text.
    void message(const QString &text);

private:
    friend class ::TstQuickWrite;

    /// Answer of the quick-write confirmation dialog.
    struct QuickAnswer {
        bool confirmed = false;
        bool dontAskAgain = false;
    };
    /// Shared by requestWrite (quick = false) and requestQuickWrite (quick = true).
    void writeValue(TagId id, double scaledValue, bool quick);

    // Prompts are replaceable (tests); defaults are the real dialogs.
    std::function<QuickAnswer(const QString &question)> m_quickConfirmPrompt;
    std::function<bool(const QString &question)> m_confirm;
    std::function<std::optional<QList<double>>(const QList<Tag> &, const QList<double> &current)> m_askValues;
    std::function<void(const QString &)> m_warn;
    /// Extra warning for an implausible current float value; true = "Write anyway".
    std::function<bool(const QString &text)> m_warnImplausible;

    TagTableModel *m_model = nullptr;
    QWidget *m_dialogParent = nullptr;
    bool m_connected = false;
    bool m_quickConfirm = true;
    quint64 m_connectionGeneration = 0; ///< bumped on every disconnect
    bool m_singleRegisterWrites = false;
    /// FC16 rejected: true = "Use FC06 only and reconnect". Replaceable in tests.
    std::function<bool(const QString &text)> m_offerSingleWrites;
    /// True when the tag still exists with exactly this configuration.
    bool stillSame(const Tag &tag) const;
    quint64 m_nextRequest = 1;
    QHash<quint64, QList<TagId>> m_pending;
};

} // namespace mb
