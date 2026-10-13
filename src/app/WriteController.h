#pragma once

#include "ModbusConnection.h"
#include "Tag.h"

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

public:
    bool quickConfirm() const;

signals:
    void quickConfirmChanged(bool on);
    /// The user chose "Detect encoding..." in the unconfirmed-encoding prompt.
    void detectEncodingRequested(mb::TagId id);
    /// Connect to PollWorker::writeRegisters (queued).
    void writeRegisters(quint64 requestId, quint16 address, const QList<quint16> &values);
    /// Status bar text.
    void message(const QString &text);

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
};

} // namespace mb
