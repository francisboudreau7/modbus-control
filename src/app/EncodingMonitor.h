#pragma once

#include "EncodingDetect.h"
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
/// 16-bit before, or its area or address changed. A change of type among the 32-bit types or of
/// byte order alone does not make it pending (that is the user's choice, or the monitor's own
/// change), and removes it from the pending set. While connected, a pending tag collects its Good
/// readings (deviceRegisters of each TagSample); after kReadings of them it runs detectEncoding
/// with deviceOrder():
/// - Clear: if the best encoding differs from the tag's, setEncoding(best, verified = false) and
///   message "Detected float32 CDAB for 'X' (reads 52.3 °C). Confirm it before writing."; if it
///   is the same, nothing changes. The tag stops being pending.
/// - Ambiguous: opens the EncodingDialog through the dialog seam (one dialog at a time; other
///   tags that become ambiguous meanwhile wait in a queue). Accepted: applies the chosen encoding
///   (verified = true) and the byte order to applyOrderTo() tags (their encodingVerified is left
///   as it is). Cancelled: the tag's encoding is kept; it stops being pending.
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

signals:
    /// Status bar text.
    void message(const QString &text);

private:
    friend class ::TstEncodingMonitor;
    friend class ::TstAppController;

    // Seams (tests replace them); defaults are the real dialogs.
    /// Runs the dialog modally; true = accepted.
    std::function<bool(EncodingDialog &dialog)> m_runDialog;
    /// On-connect warning; true = "Review...".
    std::function<bool(const QString &text)> m_reviewPrompt;

    // Implementation-defined; the task may change the private section freely.
    TagTableModel *m_model = nullptr;
    QWidget *m_dialogParent = nullptr;
    bool m_connected = false;
    QHash<TagId, Tag> m_snapshot;
    QSet<TagId> m_pending;
    QHash<TagId, QList<RegisterPair>> m_readings;
};

} // namespace mb
