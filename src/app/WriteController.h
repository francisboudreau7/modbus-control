#pragma once

#include "ModbusConnection.h"
#include "Tag.h"
#include "WriteLog.h"

#include <QHash>
#include <QList>
#include <QObject>

#include <functional>
#include <optional>

class QWidget;
class TstQuickWrite;    // test access to the prompt hooks
class TstAppController; // idem

namespace mb {

class TagTableModel;

/// Turns user write requests into PollWorker writes and reports the results.
/// Register numbers in all texts use TagTableModel::addressBase() (protocol address + base).
///
/// All prompt texts are rich text built with mb::rich (RichText.h): a bold headline saying what
/// the prompt is about, the facts as label/value rows (or a grid, or bullets), a short
/// consequence line, and technical details (registers, type, raw value) in a small note. The
/// default dialogs use Qt::RichText; tag names and values are escaped. Errors with something to
/// look at end with "Check:" and a bullet list.
///
/// requestWrite, checks in this order (each failure -> warning box, nothing written):
/// 1. not connected -> message and stop;
/// 2. outside the tag's write limits (Tag::withinWriteLimits) -> warning with the headline
///    "150 °C is outside the limits of 'Setpoint'." and "'Setpoint' accepts 0 to 100 °C. Nothing
///    was written.";
/// 3. 32-bit tags (2 registers) need TagTableModel::goodValue(): without a Good reading ->
///    warning "No valid reading of 'X' yet." with Check: the connection, the register number, the
///    type and byte order;
/// 3a. (v1.3.3) TagTableModel::writeNeedsRead(id) (the tag was made writable and has had no Good
///    reading since; any width) -> warning headline "No valid reading of 'X' since it was made
///    writable.", the line "Nothing was written." and Check: the register number (registerText),
///    the area, the type. requestWriteSelection applies it to every tag (listed together);
/// 3b. 32-bit tag whose encoding is not confirmed (Tag::encodingVerified false) -> prompt with the
///    headline "The encoding of 'X' is not confirmed.", the rows "Reads: 52.3 °C" and "As: float32
///    CDAB (HR 110-111)" and the line "If this value is wrong for the device, the encoding is
///    wrong, and a write would send a wrong value.", with "Confirm and continue" / "Detect
///    encoding..." / Cancel (default). Confirm: TagTableModel::setEncodingVerified(id, true) and
///    the checks go on (stillSame() ignores encodingVerified). Detect: emits
///    detectEncodingRequested(id) and stops (nothing written). Cancel: stops.
///    requestWriteSelection asks once for all unverified tags of the selection (headline "The
///    encoding of these tags is not confirmed:", one bullet per tag; Confirm marks them all).
///    A 16-bit tag with encodingVerified false (flagged as probably half of a 32-bit value) gets
///    the same prompt and buttons: headline "'X' is probably half of a 32-bit value." (without a
///    TagTableModel::encodingNote(id): "'X' may be half of a 32-bit value."), the row "Reads:
///    13107 as int16 (HR 110)" and, with a note, the row "Found: <note>", then "Writing a 16-bit
///    value here would change half of that value, and the device would read a wrong number."
///    "Confirm and continue" here means "it really is 16-bit". After the dialogs, a tag that
///    became unverified meanwhile (any width) is not written;
/// 4. encodeRegisters() fails (out of the type's range) -> warning;
/// 5. float32 tag whose current value is implausible (not finite, |x| > 1e9, or
///    0 < |x| < 1e-6, typical of a wrong byte order or register) -> an extra warning ("'X' reads
///    an implausible value: -3.1e+27.", why, Check: the byte order, the register number) that
///    needs an explicit "Write anyway" (default button: Cancel);
/// then the confirmation: headline "Write to 'Setpoint'?", rows "Current value:" (or "unknown"),
/// "New value:" (bold) and, when limits are set, "Limits: min to max", and the note "HR 6639-6640
/// · float32 CDAB · raw 0x42513333" (16-bit: "HR 100 · int16 · raw 125"). Then writeRegisters is
/// emitted.
///
/// requestWriteSelection: rows must all be writable holding registers that are contiguous
/// when sorted by address (each tag starts right after the previous tag's last register;
/// 16- and 32-bit tags may be mixed; at most 123 registers in total); otherwise explain why
/// in a warning box. Shows a dialog with one value field per tag (prefilled with the current
/// value), applies checks 2, 3 and 5 to every tag (limits listed together in one warning),
/// encodes all, confirms ("Write 3 tags?", a grid Tag | Current | New and the note "HR 100-103 ·
/// FC16"), and emits one writeRegisters with all registers (one FC16 request).
///
/// requestQuickWrite: same checks as requestWrite. 16-bit tags: while quickConfirm() is on the
/// confirmation has a check box "Don't ask again this session"; when it is ticked and the
/// write confirmed, quick confirmation turns off (quickConfirmChanged); while it is off the
/// write is sent without asking. 32-bit tags are always confirmed with the normal
/// confirmation (no check box), whatever quickConfirm() says.
///
/// onWriteFinished: success -> message "Write OK: ..."; failure -> message "Write failed: ..." (no dialog) and
/// TagTableModel::setStatusText() on the affected tags. Exception: a write of more than one
/// register that failed with Modbus exception 1 (illegal function) also shows a warning (through
/// the replaceable warning seam, headline "The device rejected the write of 'X'.") explaining that
/// the device does not accept FC16 (which 32-bit values and multi-register writes need) and that
/// nothing was written. There is no fallback to single-register writes.
///
/// Write log (v1.3.3): when writeRegisters is emitted, one WriteLogEntry per tag of the request is
/// prepared (time now, tag id/name, the tag's address/count and register text "HR 5009-5010" with
/// the address base, functionCode 6 for one register in the request else 16, oldValue = the
/// current value as shown in the confirmation (or "unknown"), newValue with the unit, raw = the
/// tag's registers sent ("0x%04X" each, space-separated), source "table" / "selection" / "quick",
/// server = setServer()). onWriteFinished completes them (ok, result "OK" or the error text of the
/// ModbusResult, e.g. "Exception 2: Illegal data address") and emits writeLogged for each, in tag
/// order. Requests still pending when setConnected(false) is called are logged with ok false and
/// result "No reply (disconnected)"; a late reply for them is ignored. Refused writes (checks
/// 1-5, cancelled prompts) are not logged.
///
/// Coils (v1.3.4): requestWrite / requestQuickWrite on a writable coil take 0.0 (OFF) or 1.0 (ON)
/// (anything else -> warning "'X' is a coil: write ON or OFF."). Checks 1, 3a and the
/// confirmation apply (limits, the 32-bit and encoding checks and the implausible check do not).
/// The confirmation reads "Switch 'Pump' ON?" (or OFF), rows "Current state: OFF" (or
/// "unknown") / "New state: ON", note "CO 12 · FC05". Quick writes of coils follow the 16-bit
/// quickConfirm rule (the check box is offered). Then writeCoils is emitted (one coil: FC05).
/// requestWriteSelection: either all rows are writable holding registers (as above) or all are
/// writable coils, contiguous by address (at most 1968); a mix is refused ("Select either
/// registers or coils, not both."). The values dialog offers ON/OFF per coil; one writeCoils with
/// all values (FC15), confirmation note "CO 10-13 · FC15". Write log entries of coils: functionCode
/// 5 or 15, registers "CO 12", oldValue/newValue "OFF"/"ON", raw "0"/"1" (space-separated).
/// onWriteFinished: an FC15 refused with exception 1 gets the same kind of warning as FC16
/// ("The device does not accept FC15 (write multiple coils)").
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
    /// v1.3.3: the server text of log entries, e.g. "192.168.1.20:502 unit 1" (AppController sets
    /// it on connect).
    void setServer(const QString &text);

public:
    bool quickConfirm() const;

signals:
    void quickConfirmChanged(bool on);
    /// The user chose "Detect encoding..." in the unconfirmed-encoding prompt.
    void detectEncodingRequested(mb::TagId id);
    /// Connect to PollWorker::writeRegisters (queued).
    void writeRegisters(quint64 requestId, quint16 address, const QList<quint16> &values);
    /// v1.3.4: connect to PollWorker::writeCoils (queued). Values 0/1.
    void writeCoils(quint64 requestId, quint16 address, const QList<quint16> &values);
    /// Status bar text.
    void message(const QString &text);
    /// v1.3.3: a write was answered (or abandoned at disconnect); see "Write log" above.
    void writeLogged(const mb::WriteLogEntry &entry);

private:
    friend class ::TstQuickWrite;
    friend class ::TstAppController;

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
    /// Answer of the unconfirmed-encoding prompt (check 3b).
    enum class EncodingAnswer { Confirm, Detect, Cancel };
    std::function<EncodingAnswer(const QString &text)> m_confirmEncoding;

    TagTableModel *m_model = nullptr;
    QWidget *m_dialogParent = nullptr;
    bool m_connected = false;
    bool m_quickConfirm = true;
    quint64 m_connectionGeneration = 0; ///< bumped on every disconnect
    /// True when the tag still exists with exactly this configuration (encodingVerified aside),
    /// and its encoding is confirmed now (encodingVerified true, any width).
    bool stillSame(const Tag &tag) const;
    quint64 m_nextRequest = 1;
    QHash<quint64, QList<TagId>> m_pending;
    /// Registers sent by each pending request (so the reply is judged by what was sent, even if
    /// the tags changed meanwhile).
    QHash<quint64, int> m_pendingRegisters;
    QString m_server;
    /// Log entries waiting for the reply of each pending request (T233).
    QHash<quint64, QList<WriteLogEntry>> m_pendingLog;
};

} // namespace mb
