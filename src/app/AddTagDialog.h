#pragma once

#include "EncodingDetect.h"
#include "ModbusConnection.h"
#include "Tag.h"

#include <QDialog>
#include <QList>

#include <functional>
#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class TstAddTagDialog;

namespace mb {

class EncodingDialog;
struct ProbeState;

/// "Add tag" dialog (title "Add tag"), opened by the tag table's Add button. Nothing about the
/// register is guessed: the area has no selection and the address is empty when it opens.
///
/// Children (object names, for tests):
/// - "nameEdit": QLineEdit, empty, placeholder "Automatic"; empty = the model names it "Tag N".
/// - "areaCombo": "Holding register", "Input register"; current index -1 when the dialog opens.
/// - "addressEdit": QLineEdit, empty; integers only, shown with the address base given to the
///   constructor (base 1: 1..65536; stored address = shown - base).
/// - "typeCombo": int16, uint16, int32, uint32, float32 (toString(ValueType)); int16 at start.
/// - "orderCombo": ABCD, CDAB, BADC, DCBA; enabled only for 32-bit types; ABCD at start.
/// - "unitEdit": QLineEdit, optional.
/// - "writableCheck": "Writable"; unchecked; enabled only for "Holding register".
/// - "overlapLabel": red text, hidden unless the tag (area, address, register count of the type)
///   shares a register with an existing tag: "Overlaps 'Setpoint' (HR 110-111)." (the first such
///   tag), or the 32-bit tag would go past 65535: "A 32-bit value needs two registers: HR 65535 is
///   the last one."
/// - "readButton": "Read"; "detectButton": "Detect encoding". Both enabled only while connected
///   with an area and a valid address chosen and no probe running.
/// - "previewLabel": rich text (RichText.h). Disconnected: "Connect to read the register and detect
///   its encoding. The tag is checked when you connect." Connected with no area/address yet: empty.
///   After a probe: the registers read, e.g. "HR 109: 0x0000 · HR 110: 0x4251 · HR 111: 0x3333",
///   then a grid of meanings: "HR 110 as int16: 16977", "HR 110-111 as float32: 52.3 (ABCD),
///   <x> (CDAB)", "HR 109-110 as float32: ..." (7 significant digits, a pair whose registers were
///   not both read is left out). Refused own register: "HR 110 can't be read alone (illegal data
///   address)." and the pairs that did read. Other errors: "HR 110 could not be read: <message>."
/// - "detectLabel": the Detect answer (rich text, empty until Detect ran):
///   Sixteen: "HR 110 holds a 16-bit value (16977): no float32 around it." (type set to int16
///   unless uint16 was chosen; order unchanged).
///   ThirtyTwo: the EncodingDialog opens (below); after Apply: "float32 CDAB, HR 110-111." and,
///   when the start moved, "The value starts one register earlier: the address is now HR 109."
///   OwnZero: "HR 110 reads 0: can't tell a 16-bit from a 32-bit value. Try again when the value
///   is not 0, or choose the type yourself."  RefusedNoPair: "HR 110 can't be read, alone or as
///   part of a 32-bit value: check the register number."  NotRead: the error as in previewLabel.
/// - OK button "Add": enabled when an area is chosen, the address is valid and there is no
///   overlap. Cancel adds nothing.
///
/// Probes (Read, Detect, and automatically 400 ms after the area or address last changed while
/// connected): one-off reads through the read function, one at a time, in this order:
/// (address, 1); if OK: (address - 1, 1) and (address + 1, 1), each skipped outside 0..65535, a
/// failed neighbour read just leaves it out; if refused with exception 2: (address, 2) and
/// (address - 1, 2), skipped when outside 0..65535; any other error stops the probe. The result is
/// a ProbeReadings. Changing area or address cancels a running probe (late answers are ignored).
/// Detect runs a fresh probe, then suggestLayout(area, address, readings, deviceOrder). For
/// ThirtyTwo it opens EncodingDialog for <the new tag as Float32 at suggestion.start, byte order of
/// suggestion.ranking.best or the device order> with readings {suggestion.pair}, `others` = the
/// existing 32-bit tags, the device order and the address base, and an intro (rich headline
/// "HR 110 can't be read alone." for Refused / "HR 110 looks like half of a float32." for
/// SplitFound, plus the paragraph "Choose the encoding of HR 110-111, or type the value shown on
/// the device."). Accepted: address := start, type and order := chosenEncoding(); the byte order
/// for other tags (applyOrderTo) is ignored here. Cancelled: fields unchanged.
///
/// tag() is verified (detectionConfirmed() true) when Detect's answer was applied — Sixteen, or
/// ThirtyTwo with the EncodingDialog accepted — and area, address, type and byte order have not
/// changed since; any later change of those fields clears it.
class AddTagDialog : public QDialog
{
    Q_OBJECT
public:
    /// Called with the answer of a one-off read: on success result.ok() and `values` has `count`
    /// registers.
    using ReadDone = std::function<void(const ModbusResult &result, const QList<quint16> &values)>;
    /// Issues one read of `count` holding/input registers at `address`; `done` is called exactly
    /// once, later (never from inside the call), possibly after the dialog was closed (the dialog
    /// guards against that).
    using ReadFn = std::function<void(Area area, quint16 address, int count, ReadDone done)>;

    /// existing: the project's tags (overlap check, `others` for EncodingDialog).
    AddTagDialog(const QList<Tag> &existing, int addressBase, std::optional<ByteOrder> deviceOrder,
                 QWidget *parent = nullptr);
    ~AddTagDialog() override;

    /// The read function used by probes; without one the dialog behaves as disconnected.
    void setReadFunction(ReadFn read);
    /// Connection state changes while the dialog is open (enables Read/Detect, preview text). A
    /// disconnect cancels a running probe.
    void setConnected(bool connected);

    /// The tag to add, from the fields: id 0, name (trimmed; empty = automatic), area, address,
    /// type, byteOrder, unit, writable (false for input registers), other fields default.
    /// encodingVerified = true (the EncodingMonitor decides for new tags; see detectionConfirmed).
    Tag tag() const;
    /// True when the type and address come from an applied Detect answer (see above).
    bool detectionConfirmed() const;

private:
    friend class ::TstAddTagDialog;

    /// Test seam: runs the EncodingDialog; true = accepted. Default: exec() == Accepted.
    std::function<bool(EncodingDialog &dialog)> m_runEncodingDialog;

    // Implementation-defined; the task may change the rest of the private section freely.
    QList<Tag> m_existing;
    int m_addressBase = 0;
    std::optional<ByteOrder> m_deviceOrder;
    ReadFn m_read;
    bool m_connected = false;
    quint64 m_generation = 0; ///< bumped to drop answers of a cancelled probe
    bool m_confirmed = false;

    QLineEdit *m_name = nullptr;
    QComboBox *m_area = nullptr;
    QLineEdit *m_address = nullptr;
    QComboBox *m_type = nullptr;
    QComboBox *m_order = nullptr;
    QLineEdit *m_unit = nullptr;
    QCheckBox *m_writable = nullptr;
    QLabel *m_overlap = nullptr;
    QPushButton *m_readButton = nullptr;
    QPushButton *m_detectButton = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_detect = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    QTimer *m_autoRead = nullptr;

    bool m_probing = false;   ///< a probe (Read or Detect) is running
    bool m_applying = false;  ///< fields are being set by Detect: field signals are ignored

    std::optional<quint16> parsedAddress() const;
    bool canProbe() const;
    QString regLabel(int address) const;
    QString rangeLabel(int first, int count) const;
    QString disconnectedHint() const;
    /// Re-evaluates overlap, enabled states and the preview hint from the fields.
    void refresh();
    /// Area or address edited: cancels a probe, clears the answers, restarts the timer.
    void onTargetChanged();
    /// Type or order edited.
    void onEncodingChanged();
    void startProbe(bool forDetect);
    void probeNext(const std::shared_ptr<ProbeState> &state);
    void probeAnswered(const std::shared_ptr<ProbeState> &state, const ModbusResult &result,
                       const QList<quint16> &values);
    void finishProbe(const std::shared_ptr<ProbeState> &state);
    QString previewText(const ProbeState &state) const;
    void applyDetect(const ProbeState &state);
};

} // namespace mb
