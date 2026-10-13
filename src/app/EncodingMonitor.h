#pragma once

#include "EncodingDetect.h"
#include "ModbusConnection.h"
#include "SampleBatch.h"
#include "Tag.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>

#include <functional>
#include <optional>

class QWidget;
class TstEncodingMonitor; // test access to the seams
class TstAppController;   // idem

namespace mb {

class EncodingDialog;
class TagTableModel;

/// Detects the encoding of 32-bit tags from their readings (see detectEncoding and the User
/// Guide, "Encoding detection"). Owned by AppController, which forwards samples, connection changes and tag
/// changes. All changes go through TagTableModel::setEncoding / setEncodingVerified, so the
/// model emits tagsEdited() (the project becomes modified and the worker gets the new tags).
///
/// Pending tags: after onTagsChanged(), a tag is pending when it is 32-bit and is new, was
/// 16-bit before, or its area or address changed. A tag becoming pending is marked unverified
/// (setEncodingVerified(false)): its encoding is not confirmed until detection or the user
/// confirms it, so writes to it ask first. A change of type among the 32-bit types or of
/// byte order alone does not make it pending (that is the user's choice, or the monitor's own
/// change), and removes it from the pending set. While connected, a pending tag collects its Good
/// readings (deviceRegisters of each TagSample); after kReadings of them it runs detectEncoding
/// with deviceOrder():
/// - Clear, best == the tag's encoding: setEncodingVerified(true) (the readings confirm it).
///   Clear, best differs and currentEncodingSuspicious(): setEncoding(best, verified = false) and
///   message "Detected float32 CDAB for 'X' (reads 52.3 °C). Confirm it before writing.".
///   Clear, best differs but the current encoding reads plausibly: handled as Ambiguous (ask).
///   The tag stops being pending (except when asking, until the dialog closes).
/// - Ambiguous: opens the EncodingDialog through the dialog seam (one dialog at a time; other
///   tags that become ambiguous meanwhile wait in a queue). Accepted: applies the chosen encoding
///   (verified = true) and the byte order to applyOrderTo() tags (only unconfirmed tags are
///   offered; they stay unverified). Cancelled: the tag's encoding is kept, still unverified; it
///   stops being pending. NoMatch (only with a display value) ends pending the same way.
/// - Undecidable: stays pending, keeps only the latest kReadings readings, and decides on the first
///   reading set that is not Undecidable.
///
/// On connect: once every 32-bit tag has kReadings Good readings (tags that never read Good are
/// skipped after 10 poll batches), runs detectEncoding for each; tags with
/// currentEncodingSuspicious() are marked unverified (setEncodingVerified(false)) and listed in
/// one prompt through the review seam: "These tags read implausible values with their current
/// encoding: 'X' reads -3.1e+27 as float32 ABCD (float32 CDAB gives 52.3). Writes to them are
/// blocked until you confirm their encoding." with "Review..." (opens the dialog for each listed
/// tag in turn, as detectNow) and "Close". Pending tags are not part of this check.
///
/// detectNow(id): opens the dialog for a 32-bit tag with its recent readings (up to 10, kept for
/// every 32-bit tag while connected), and feeds it live readings while it is open. Accepted:
/// applies like Ambiguous above, verified = true. The dialog gets the address base of the model.
///
/// 16-bit tags ("split check": probably half of a float32?). After onTagsChanged(), a 16-bit tag
/// that is new, was 32-bit before, or whose area or address changed is split-pending (it is NOT
/// marked unverified). While connected, on its first Good sample the monitor probes its
/// neighbourhood: emits readRequested(id, area, address - 1, 3) (clipped at 0 and 65535: 2
/// registers), and when that fails, one 2-register read per side; a side that fails counts as
/// absent. A neighbour register used by another tag of the project counts as absent too (that
/// register is mapped as something else; and a Change there would make one tag overwrite another).
/// Probes run one at a time (request ids are the monitor's own, increasing); a result for
/// a tag that was removed, moved or retyped since, or from before a disconnect, is dropped. A probe
/// that ends in Timeout or ConnectionLost stops all split probes until onTagsReplaced() (some
/// devices drop the connection on reads outside their map), with the message "Stopped checking
/// 16-bit tags: the device did not answer a read next to 'X'." Tags with confirmed16Bit are not
/// checked automatically (pending or on connect). With the registers,
/// detectSplitValue(tag, own, below, above):
/// - nullopt: an automatic check of a 16-bit tag that is unverified without a note (a stale flag,
///   e.g. a flagged tag that was moved, or a float32 tag changed to int16) sets it verified. A
///   detectNow check of a flagged tag keeps the flag ("No float32 found around 'X' (HR 110) right
///   now; it stays flagged. Choose Keep int16 when it is 16-bit." through the split prompt with
///   only Keep and Later: changeLabel empty); of an unflagged tag: message "'X' (HR 110) looks like
///   a 16-bit value: no float32 found around it."
/// - a SplitValue: setEncodingVerified(false), setEncodingNote("HR 110-111 read as float32 CDAB
///   give 52.3") (value: 7 significant digits), then the split prompt (seam; one prompt or
///   dialog open at a time, others wait in the same queue): "'Setpoint' (HR 110, int16) reads
///   13107, but HR 110-111 read as float32 CDAB give 52.3: this tag is probably half of a float32
///   value." with the answers Change ("Change to float32 CDAB at HR 110": setLayout(id, start,
///   Float32, order, verified = true)), Keep ("Keep int16": setEncodingVerified(true) and
///   setConfirmed16Bit(true), note cleared) and Later ("Decide later": stays flagged). When the tag
///   has gain != 1, offset != 0 or quick values, the text adds " Change also resets its gain and
///   offset to 1 and 0 and clears its quick values." Register numbers use the model's address
///   base; the type name is the tag's (int16/uint16). An empty changeLabel means "no Change
///   button".
/// On connect, every 16-bit tag that was not checked yet in this session (since the last
/// onTagsReplaced(); a moved tag is re-checked by the pending path) is split-checked once after its
/// first Good sample (tags that never read Good are skipped after 10 batches); reconnects do not
/// check the same tags again. The same once-per-session rule applies to the 32-bit on-connect
/// check. Both on-connect prompts wait while a prompt or dialog is open, and are shown when it
/// closes. The tags found suspicious are flagged as above and
/// listed in one prompt through the review seam: "These 16-bit tags are probably half of a 32-bit
/// value: 'X' (HR 110-111 read as float32 CDAB give 52.3). Writes to them ask first." with
/// "Review..." (the split prompt for each, in turn) and "Close". detectNow on a 16-bit tag runs the
/// split check now (when connected; otherwise message "Connect to check 'X'.") and shows the split
/// prompt when it is suspicious.
class EncodingMonitor : public QObject
{
    Q_OBJECT
public:
    static constexpr int kReadings = 3;

    EncodingMonitor(TagTableModel *model, QWidget *dialogParent, QObject *parent = nullptr);

    /// The byte order shared by every verified 32-bit tag, if there is at least one and they all
    /// agree; else nullopt.
    std::optional<ByteOrder> deviceOrder() const;
    bool isPending(TagId id) const;

public slots:
    void onSamples(const mb::SampleBatch &batch);
    void onConnected();
    /// Forgets collected readings (pending tags stay pending).
    void onDisconnected();
    /// After the user (or the monitor) edited tags: finds new pending tags.
    void onTagsChanged();
    /// After the whole tag list was replaced (New, Open, session): takes a new snapshot; no tag
    /// becomes pending (the on-connect check covers loaded tags).
    void onTagsReplaced();
    /// "Detect encoding..." for one tag.
    void detectNow(mb::TagId id);

    /// Answer to a one-off read requested with readRequested (connect PollWorker::readFinished).
    void onReadFinished(quint64 requestId, const mb::ModbusResult &result,
                        const QList<quint16> &values);

signals:
    /// Status bar text.
    void message(const QString &text);
    /// A one-off read for the split check (connect to PollWorker::readRegisters, queued).
    void readRequested(quint64 requestId, mb::Area area, quint16 address, int count);

private:
    friend class ::TstEncodingMonitor;
    friend class ::TstAppController;

    // Seams (tests replace them); defaults are the real dialogs.
    /// Runs the dialog modally; true = accepted.
    std::function<bool(EncodingDialog &dialog)> m_runDialog;
    /// On-connect warning; true = "Review...".
    std::function<bool(const QString &text)> m_reviewPrompt;
    /// Split prompt answers.
    enum class SplitAnswer { Change, Keep, Later };
    /// The split prompt: text, label of the Change button, label of the Keep button.
    std::function<SplitAnswer(const QString &text, const QString &changeLabel,
                              const QString &keepLabel)> m_splitPrompt;

    // Implementation-defined; the task may change the private section freely.
    static constexpr int kMaxReadings = 10; ///< recent readings kept per 32-bit tag
    static constexpr int kCheckBatches = 10; ///< on-connect check gives up on silent tags after this

    /// An encoding dialog (or, when `split`, a split prompt) to run. `tag` is the configuration
    /// when it was requested.
    struct Request {
        TagId id = 0;
        Tag tag;
        quint64 epoch = 0;
        bool split = false;
        SplitValue splitValue; ///< split prompts only
        quint16 own = 0;       ///< split prompts only: the tag's register as probed
        bool noSplit = false;  ///< split prompts only: nothing found now, the tag stays flagged
        friend bool operator==(const Request &a, const Request &b) { return a.id == b.id; }
    };

    const Tag *findTag(TagId id) const;
    /// The tag still exists, is 32-bit and sits at the same area/address as `request.tag`.
    bool stillValid(const Request &request) const;
    bool awaitingDialog(TagId id) const;
    void enqueue(TagId id);
    void pump();
    void runRequest(const Request &request);
    void applyChoice(const Request &request, const EncodingDialog &dialog);
    void evaluatePending(TagId id);
    void runConnectCheck();
    void finishPending(TagId id);
    QList<RegisterPair> recent(TagId id, int count) const;
    static QString valueText(double value, const Tag &tag);

    // 16-bit split check.
    enum class ProbeMode { Pending, Connect, Manual };
    /// One neighbourhood read sequence (the whole read, or one read per side when that fails).
    struct Probe {
        TagId id = 0;
        Tag tag;
        quint64 epoch = 0;
        quint64 generation = 0; ///< connection generation when queued
        ProbeMode mode = ProbeMode::Pending;
        quint64 requestId = 0;
        int stage = 0; ///< 0 = whole read, 1 = below side, 2 = above side
        std::optional<quint16> own;
        std::optional<quint16> below;
        std::optional<quint16> above;
        bool belowUsable = true; ///< the register exists and no other tag of the project uses it
        bool aboveUsable = true;
        bool sidesChecked = false; ///< belowUsable/aboveUsable are set
    };
    /// A suspicious tag found by the on-connect pass.
    struct SplitFinding {
        TagId id = 0;
        Tag tag;
        quint64 epoch = 0;
        SplitValue split;
        quint16 own = 0;
    };
    /// An on-connect review prompt that had to wait for an open prompt or dialog.
    struct Review32 {
        QString text;
        QList<TagId> ids;
    };
    struct Review16 {
        QString text;
        QList<SplitFinding> flagged;
    };

    bool askReview(const QString &text);
    bool registerUsedByOther(const Tag &self, int reg) const;
    bool splitProbeValid(const Probe &probe) const;
    bool splitRequestValid(const Request &request) const;
    void queueProbe(TagId id, ProbeMode mode);
    void startNextProbe();
    void issueRead(Probe &probe);
    void completeProbe(const Probe &probe);
    void flagSplit(const Tag &tag, const SplitValue &split);
    QString splitNote(const Tag &tag, const SplitValue &split) const;
    void enqueueSplit(const Tag &tag, const SplitValue &split, quint16 own, quint64 epoch);
    void runSplitRequest(const Request &request);
    void maybeFinishConnectPass();
    void forgetSplit(TagId id);
    void splitCheckNow(const Tag &tag);

    TagTableModel *m_model = nullptr;
    QWidget *m_dialogParent = nullptr;
    bool m_connected = false;
    quint64 m_epoch = 0; ///< bumped when the whole tag list is replaced
    QHash<TagId, Tag> m_snapshot;
    QSet<TagId> m_pending;
    QHash<TagId, int> m_collected; ///< Good readings of a pending tag since it became pending
    QHash<TagId, QList<RegisterPair>> m_readings;
    QList<Request> m_queue;
    EncodingDialog *m_openDialog = nullptr;
    TagId m_openId = 0;
    bool m_checkActive = false;
    int m_checkBatches = 0;
    bool m_promptOpen = false; ///< a split prompt is open (modal)
    QSet<TagId> m_splitPending;
    QSet<TagId> m_connectWait; ///< on-connect pass: 16-bit tags waiting for a Good sample
    QList<SplitFinding> m_connectFound;
    bool m_passActive = false;
    int m_passBatches = 0;
    quint64 m_generation = 0; ///< bumped on every connect and disconnect
    quint64 m_nextRequest = 1;
    QList<Probe> m_probes;
    std::optional<Probe> m_probe; ///< the one in flight
    QSet<TagId> m_sessionChecked; ///< tags the on-connect checks (16 and 32-bit) already covered
    std::optional<Review32> m_deferred32;
    std::optional<Review16> m_deferred16;
    bool m_splitStopped = false; ///< a probe timed out: no more split probes until onTagsReplaced
};

} // namespace mb
