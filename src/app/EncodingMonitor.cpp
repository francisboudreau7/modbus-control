#include "EncodingMonitor.h"

#include "EncodingDialog.h"
#include "RichText.h"
#include "TagCodec.h"
#include "TagTableModel.h"

#include <QMessageBox>
#include <QPushButton>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace mb {

namespace {

bool wide(const Tag &tag)
{
    return registerCount(tag.type) == 2;
}

// Prompt texts are rich text built with mb::rich: a bold headline, then the readings laid out for
// comparison.
/// "1 tag reads" / "2 tags read" style counts.
QString tagCount(qsizetype n, const QString &one, const QString &many)
{
    return n == 1 ? QStringLiteral("1 tag %1").arg(one) : QStringLiteral("%1 tags %2").arg(n).arg(many);
}

QString areaPrefix(Area area)
{
    return area == Area::HoldingRegister ? QStringLiteral("HR") : QStringLiteral("IR");
}

} // namespace

EncodingMonitor::EncodingMonitor(TagTableModel *model, QWidget *dialogParent, QObject *parent)
    : QObject(parent), m_model(model), m_dialogParent(dialogParent)
{
    m_runDialog = [](EncodingDialog &dialog) { return dialog.exec() == QDialog::Accepted; };
    m_splitPrompt = [this](const QString &text, const QString &changeLabel,
                           const QString &keepLabel) {
        QMessageBox box(QMessageBox::Warning, QStringLiteral("Encoding check"), text,
                        QMessageBox::NoButton, m_dialogParent);
        box.setTextFormat(Qt::RichText);
        QPushButton *change = changeLabel.isEmpty()
                                  ? nullptr
                                  : box.addButton(changeLabel, QMessageBox::AcceptRole);
        QPushButton *keep = box.addButton(keepLabel, QMessageBox::ActionRole);
        QPushButton *later = box.addButton(QStringLiteral("Decide later"), QMessageBox::RejectRole);
        box.setDefaultButton(later);
        box.setEscapeButton(later);
        box.exec();
        if (change && box.clickedButton() == change)
            return SplitAnswer::Change;
        if (box.clickedButton() == keep)
            return SplitAnswer::Keep;
        return SplitAnswer::Later;
    };
    m_reviewPrompt = [this](const QString &text) {
        QMessageBox box(QMessageBox::Warning, QStringLiteral("Encoding check"), text,
                        QMessageBox::NoButton, m_dialogParent);
        box.setTextFormat(Qt::RichText);
        QPushButton *review = box.addButton(QStringLiteral("Review..."), QMessageBox::AcceptRole);
        QPushButton *close = box.addButton(QStringLiteral("Close"), QMessageBox::RejectRole);
        box.setDefaultButton(close);
        box.exec();
        return box.clickedButton() == review;
    };
    for (const Tag &tag : m_model->tags())
        m_snapshot.insert(tag.id, tag);
}

const Tag *EncodingMonitor::findTag(TagId id) const
{
    const int row = m_model->rowOf(id);
    return row < 0 ? nullptr : &m_model->tagAt(row);
}

std::optional<ByteOrder> EncodingMonitor::deviceOrder() const
{
    std::optional<ByteOrder> order;
    for (const Tag &tag : m_model->tags()) {
        if (!wide(tag) || !tag.encodingVerified)
            continue;
        if (!order)
            order = tag.byteOrder;
        else if (*order != tag.byteOrder)
            return std::nullopt;
    }
    return order;
}

bool EncodingMonitor::isPending(TagId id) const
{
    return m_pending.contains(id);
}

bool EncodingMonitor::awaitingDialog(TagId id) const
{
    return (m_openDialog && m_openId == id)
           || std::any_of(m_queue.begin(), m_queue.end(),
                          [id](const Request &r) { return r.id == id; });
}

QList<RegisterPair> EncodingMonitor::recent(TagId id, int count) const
{
    const QList<RegisterPair> all = m_readings.value(id);
    return all.size() <= count ? all : all.mid(all.size() - count);
}

QString EncodingMonitor::valueText(double value, const Tag &tag)
{
    QString text = QString::number(value, 'g', 7);
    if (!tag.unit.isEmpty())
        text += QLatin1Char(' ') + tag.unit;
    return text;
}

void EncodingMonitor::finishPending(TagId id)
{
    m_pending.remove(id);
    m_collected.remove(id);
}

void EncodingMonitor::onSamples(const SampleBatch &batch)
{
    for (const TagSample &sample : batch.samples) {
        const Tag *found = findTag(sample.tagId);
        if (found && !wide(*found)) {
            m_lastQuality[sample.tagId] = sample.quality;
            if (sample.quality == Quality::Exception && m_connected) {
                // The device refuses to read this register alone: try it as part of a pair.
                const bool pending = m_splitPending.remove(sample.tagId);
                const bool connect = m_connectWait.remove(sample.tagId);
                if (pending)
                    queueRefused(sample.tagId, ProbeMode::Pending);
                if (connect)
                    queueRefused(sample.tagId, ProbeMode::Connect);
            }
        }
        if (sample.quality != Quality::Good)
            continue;
        if (found && !wide(*found)) {
            // First Good sample of a 16-bit tag: time for its split check.
            if (m_connected && m_splitPending.remove(sample.tagId))
                queueProbe(sample.tagId, ProbeMode::Pending);
            if (m_connected && m_connectWait.remove(sample.tagId))
                queueProbe(sample.tagId, ProbeMode::Connect);
            continue;
        }
        if (!found)
            continue;
        const TagId id = sample.tagId;
        const RegisterPair pair = deviceRegisters(*found, sample.raw);
        QList<RegisterPair> &list = m_readings[id];
        list.append(pair);
        while (list.size() > kMaxReadings)
            list.removeFirst();
        if (m_openDialog && m_openId == id)
            m_openDialog->addReading(pair);
        if (m_pending.contains(id) && !awaitingDialog(id)) {
            if (++m_collected[id] >= kReadings)
                evaluatePending(id);
        }
    }

    if (m_checkActive) {
        ++m_checkBatches;
        bool ready = true;
        for (const Tag &tag : m_model->tags()) {
            if (wide(tag) && !m_pending.contains(tag.id) && !m_sessionChecked.contains(tag.id)
                && m_readings.value(tag.id).size() < kReadings) {
                ready = false;
                break;
            }
        }
        if (ready || m_checkBatches >= kCheckBatches)
            runConnectCheck();
    }
    if (m_passActive) {
        if (++m_passBatches >= kCheckBatches)
            m_connectWait.clear(); // tags that never read Good are skipped
        maybeFinishConnectPass();
    }
    pump();
}

void EncodingMonitor::evaluatePending(TagId id)
{
    const Tag *found = findTag(id);
    if (!found || !wide(*found)) {
        finishPending(id);
        return;
    }
    const Tag tag = *found;
    m_sessionChecked.insert(id); // the pending path covers this tag; no on-connect check later
    EncodingOptions options;
    options.deviceOrder = deviceOrder();
    const EncodingResult result = detectEncoding(tag, recent(id, kReadings), options);
    switch (result.verdict) {
    case EncodingVerdict::Clear: {
        if (!result.best) {
            finishPending(id);
            break;
        }
        if (*result.best == encodingOf(tag)) {
            // The readings agree with the encoding the tag was given.
            finishPending(id);
            m_model->setEncodingVerified(id, true);
            if (const Tag *now = findTag(id))
                m_snapshot.insert(id, *now);
            break;
        }
        // Change the encoding on its own only when the current one reads implausibly; a
        // plausible current reading (e.g. an int32 17000 that also reads as float 58.0) is the
        // user's call.
        if (!currentEncodingSuspicious(tag, result)) {
            enqueue(id);
            break;
        }
        finishPending(id);
        const Encoding best = *result.best;
        QString shown;
        for (const EncodingCandidate &c : result.candidates) {
            if (c.encoding == best && !c.values.isEmpty())
                shown = valueText(c.values.last(), tag);
        }
        m_model->setEncoding(id, best.type, best.order, false);
        if (const Tag *now = findTag(id))
            m_snapshot.insert(id, *now);
        emit message(QStringLiteral("Detected %1 for '%2' (reads %3). Confirm it before writing.")
                         .arg(toString(best), tag.name, shown));
        break;
    }
    case EncodingVerdict::Ambiguous:
        enqueue(id);
        break;
    case EncodingVerdict::Undecidable:
        m_readings[id] = recent(id, kReadings);
        break;
    case EncodingVerdict::NoMatch:
        finishPending(id);
        break;
    }
}

void EncodingMonitor::onConnected()
{
    m_connected = true;
    m_readings.clear();
    for (auto it = m_collected.begin(); it != m_collected.end(); ++it)
        it.value() = 0;
    m_checkActive = true;
    m_checkBatches = 0;

    ++m_generation;
    m_probes.clear();
    m_probe.reset();
    m_connectFound.clear();
    m_connectWait.clear();
    for (const Tag &tag : m_model->tags()) {
        if (!wide(tag) && !m_splitPending.contains(tag.id) && !tag.confirmed16Bit
            && !m_sessionChecked.contains(tag.id))
            m_connectWait.insert(tag.id);
    }
    m_passActive = true;
    m_passBatches = 0;
}

void EncodingMonitor::onDisconnected()
{
    m_connected = false;
    m_readings.clear();
    for (auto it = m_collected.begin(); it != m_collected.end(); ++it)
        it.value() = 0;
    m_checkActive = false;
    m_lastQuality.clear();

    ++m_generation;
    m_probes.clear();
    m_probe.reset();
    m_connectFound.clear();
    m_connectWait.clear();
    m_passActive = false;
}

void EncodingMonitor::runConnectCheck()
{
    m_checkActive = false;
    QStringList entries;
    QList<TagId> ids;
    for (const Tag &tag : m_model->tags()) {
        if (!wide(tag) || m_pending.contains(tag.id) || m_sessionChecked.contains(tag.id))
            continue;
        const QList<RegisterPair> readings = recent(tag.id, kReadings);
        if (readings.size() < kReadings)
            continue;
        m_sessionChecked.insert(tag.id);
        EncodingOptions options;
        options.deviceOrder = deviceOrder();
        const EncodingResult result = detectEncoding(tag, readings, options);
        if (!currentEncodingSuspicious(tag, result) || !result.best)
            continue;
        const Encoding current = encodingOf(tag);
        QString currentValue;
        QString bestValue;
        for (const EncodingCandidate &c : result.candidates) {
            if (c.values.isEmpty())
                continue;
            if (c.encoding == current)
                currentValue = valueText(c.values.last(), tag);
            if (c.encoding == *result.best)
                bestValue = valueText(c.values.last(), tag);
        }
        const int first = tag.address + m_model->addressBase();
        entries << QStringLiteral("'%1' (%2 %3-%4): %5 as %6, <b>%7</b> as %8")
                       .arg(rich::escaped(tag.name), areaPrefix(tag.area)).arg(first).arg(first + 1)
                       .arg(rich::escaped(currentValue), toString(current), rich::escaped(bestValue),
                            toString(*result.best));
        ids << tag.id;
    }
    if (ids.isEmpty())
        return;
    for (TagId id : std::as_const(ids))
        m_model->setEncodingVerified(id, false);
    for (TagId id : std::as_const(ids)) {
        if (const Tag *now = findTag(id))
            m_snapshot.insert(id, *now);
    }
    const QString text =
        rich::headline(tagCount(ids.size(),
                                QStringLiteral("reads an implausible value with its current "
                                               "encoding:"),
                                QStringLiteral("read implausible values with their current "
                                               "encoding:")))
        + rich::bulletsRich(entries)
        + rich::paragraph(QStringLiteral("Writes to them ask first until you confirm their "
                                         "encoding. Review them now?"));
    // Shown by pump() as soon as no prompt or dialog is open (now, usually).
    m_deferred32 = Review32{text, ids};
    pump();
}

bool EncodingMonitor::askReview(const QString &text)
{
    if (!m_reviewPrompt)
        return false;
    m_promptOpen = true;
    const bool review = m_reviewPrompt(text);
    m_promptOpen = false;
    return review;
}

void EncodingMonitor::onTagsChanged()
{
    const QList<Tag> tags = m_model->tags();
    QList<TagId> unverify; // newly pending tags: not confirmed until detection or the user says so
    QSet<TagId> present;
    QHash<TagId, Tag> snapshot;
    for (const Tag &tag : tags) {
        present.insert(tag.id);
        snapshot.insert(tag.id, tag);
        const auto it = m_snapshot.constFind(tag.id);
        if (!wide(tag)) {
            finishPending(tag.id);
            m_readings.remove(tag.id);
            const bool relocated = it == m_snapshot.constEnd() || wide(*it)
                                   || it->area != tag.area || it->address != tag.address;
            if (relocated) {
                m_lastQuality.remove(tag.id);
                forgetSplit(tag.id);
                m_sessionChecked.remove(tag.id);
                m_queue.removeAll(Request{tag.id, {}, 0});
                m_splitPending.insert(tag.id);
            }
            continue;
        }
        forgetSplit(tag.id);
        m_lastQuality.remove(tag.id);
        const bool moved = it == m_snapshot.constEnd() || !wide(*it) || it->area != tag.area
                           || it->address != tag.address;
        if (moved) {
            m_sessionChecked.remove(tag.id);
            if (tag.encodingVerified)
                unverify.append(tag.id);
            m_pending.insert(tag.id);
            m_collected[tag.id] = 0;
            m_readings.remove(tag.id);
            m_queue.removeAll(Request{tag.id, {}, 0});
        } else if (it->type != tag.type || it->byteOrder != tag.byteOrder) {
            finishPending(tag.id);
        }
    }
    for (auto it = m_snapshot.constBegin(); it != m_snapshot.constEnd(); ++it) {
        if (present.contains(it.key()))
            continue;
        finishPending(it.key());
        forgetSplit(it.key());
        m_lastQuality.remove(it.key());
        m_sessionChecked.remove(it.key());
        m_readings.remove(it.key());
        m_queue.removeAll(Request{it.key(), {}, 0});
    }
    m_snapshot = snapshot;
    // After the snapshot: the nested tagsEdited -> onTagsChanged sees only the flag change.
    for (TagId id : std::as_const(unverify)) {
        m_model->setEncodingVerified(id, false);
        if (const Tag *now = findTag(id))
            m_snapshot.insert(id, *now);
    }
}

void EncodingMonitor::onTagsReplaced()
{
    ++m_epoch;
    m_snapshot.clear();
    for (const Tag &tag : m_model->tags())
        m_snapshot.insert(tag.id, tag);
    m_pending.clear();
    m_collected.clear();
    m_readings.clear();
    m_queue.clear();
    m_splitPending.clear();
    m_connectWait.clear();
    m_connectFound.clear();
    m_probes.clear();
    m_probe.reset();
    m_passActive = false;
    m_sessionChecked.clear();
    m_lastQuality.clear();
    m_deferred32.reset();
    m_deferred16.reset();
    m_splitStopped = false;
}

void EncodingMonitor::detectNow(TagId id)
{
    const Tag *tag = findTag(id);
    if (!tag)
        return;
    if (!wide(*tag)) {
        if (m_lastQuality.value(id, Quality::Unknown) == Quality::Exception)
            refusedCheckNow(*tag);
        else
            splitCheckNow(*tag);
        pump();
        return;
    }
    enqueue(id);
    pump();
}

void EncodingMonitor::enqueue(TagId id)
{
    const Tag *tag = findTag(id);
    if (!tag || !wide(*tag) || awaitingDialog(id))
        return;
    m_queue.append(Request{id, *tag, m_epoch});
}

bool EncodingMonitor::stillValid(const Request &request) const
{
    const Tag *tag = findTag(request.id);
    return request.epoch == m_epoch && tag && wide(*tag) && tag->area == request.tag.area
           && tag->address == request.tag.address;
}

void EncodingMonitor::pump()
{
    while (!m_openDialog && !m_promptOpen) {
        if (!m_queue.isEmpty()) {
            const Request request = m_queue.takeFirst();
            if (request.refused)
                runRefusedRequest(request);
            else if (request.split)
                runSplitRequest(request);
            else
                runRequest(request);
        } else if (m_deferred32) {
            // On-connect review prompts: only when no prompt or dialog is open.
            const Review32 review = *std::exchange(m_deferred32, std::nullopt);
            if (askReview(review.text)) {
                for (TagId id : review.ids)
                    enqueue(id);
            }
        } else if (m_deferred16) {
            const Review16 review = *std::exchange(m_deferred16, std::nullopt);
            if (askReview(review.text)) {
                for (const SplitFinding &f : review.flagged) {
                    if (const Tag *tag = findTag(f.id))
                        enqueueSplit(*tag, f.split, f.own, f.epoch);
                }
            }
        } else {
            break;
        }
    }
}

void EncodingMonitor::runRequest(const Request &request)
{
    if (!stillValid(request))
        return;
    const Tag tag = *findTag(request.id);
    QList<Tag> others;
    for (const Tag &other : m_model->tags()) {
        if (other.id != tag.id && wide(other))
            others.append(other);
    }
    EncodingDialog dialog(tag, recent(tag.id, kMaxReadings), others, deviceOrder(),
                          m_model->addressBase(), m_dialogParent);
    m_openDialog = &dialog;
    m_openId = tag.id;
    const bool accepted = m_runDialog && m_runDialog(dialog);
    m_openDialog = nullptr;
    m_openId = 0;
    if (!stillValid(request))
        return; // removed or reconfigured meanwhile: the result no longer applies
    if (accepted)
        applyChoice(request, dialog);
    finishPending(request.id);
}

void EncodingMonitor::applyChoice(const Request &request, const EncodingDialog &dialog)
{
    const std::optional<Encoding> chosen = dialog.chosenEncoding();
    if (!chosen)
        return;
    m_model->setEncoding(request.id, chosen->type, chosen->order, true);
    if (const Tag *now = findTag(request.id))
        m_snapshot.insert(request.id, *now);
    applyOrderToOthers(dialog, chosen->order);
}

void EncodingMonitor::applyOrderToOthers(const EncodingDialog &dialog, ByteOrder order)
{
    for (TagId otherId : dialog.applyOrderTo()) {
        const Tag *other = findTag(otherId);
        if (!other || !wide(*other))
            continue;
        const Tag copy = *other;
        // Only the byte order was chosen for them: their encoding stays to be confirmed.
        m_model->setEncoding(otherId, copy.type, order, false);
        if (const Tag *now = findTag(otherId))
            m_snapshot.insert(otherId, *now);
    }
}

// --- 16-bit split check -------------------------------------------------------------

void EncodingMonitor::forgetSplit(TagId id)
{
    m_splitPending.remove(id);
    m_connectWait.remove(id);
    m_probes.removeIf([id](const Probe &p) { return p.id == id; });
    m_connectFound.removeIf([id](const SplitFinding &f) { return f.id == id; });
}

bool EncodingMonitor::splitProbeValid(const Probe &probe) const
{
    const Tag *tag = findTag(probe.id);
    return m_connected && probe.generation == m_generation && probe.epoch == m_epoch && tag
           && !wide(*tag) && tag->area == probe.tag.area && tag->address == probe.tag.address
           && tag->type == probe.tag.type;
}

bool EncodingMonitor::splitRequestValid(const Request &request) const
{
    const Tag *tag = findTag(request.id);
    return request.epoch == m_epoch && tag && !wide(*tag) && tag->area == request.tag.area
           && tag->address == request.tag.address && tag->type == request.tag.type;
}

void EncodingMonitor::queueProbe(TagId id, ProbeMode mode)
{
    const Tag *tag = findTag(id);
    if (!tag || wide(*tag) || !m_connected)
        return;
    if (mode != ProbeMode::Manual && tag->confirmed16Bit)
        return; // the user answered Keep: not checked automatically any more
    if (m_splitStopped)
        return;
    const bool busy = (m_probe && m_probe->id == id)
                      || std::any_of(m_probes.begin(), m_probes.end(),
                                     [id](const Probe &p) { return p.id == id; });
    if (busy)
        return;
    Probe probe;
    probe.id = id;
    probe.tag = *tag;
    probe.epoch = m_epoch;
    probe.generation = m_generation;
    probe.mode = mode;
    m_probes.append(probe);
    startNextProbe();
}

void EncodingMonitor::startNextProbe()
{
    while (!m_probe && !m_probes.isEmpty()) {
        Probe probe = m_probes.takeFirst();
        if (splitProbeValid(probe))
            issueRead(probe);
    }
}

bool EncodingMonitor::registerUsedByOther(const Tag &self, int reg) const
{
    return registerOwnerOther(self, reg).has_value();
}

std::optional<Tag> EncodingMonitor::registerOwnerOther(const Tag &self, int reg) const
{
    for (const Tag &other : m_model->tags()) {
        if (other.id != self.id && other.area == self.area && reg >= other.address
            && reg < other.address + registerCount(other.type))
            return other;
    }
    return std::nullopt;
}

bool EncodingMonitor::pairBlocked(const Tag &self, int start)
{
    std::optional<Tag> owner = registerOwnerOther(self, start);
    if (!owner)
        owner = registerOwnerOther(self, start + 1);
    if (!owner)
        return false;
    const int base = m_model->addressBase();
    emit message(QStringLiteral("Not applied: %1 %2-%3 is now used by '%4'.")
                     .arg(areaPrefix(self.area)).arg(start + base).arg(start + 1 + base)
                     .arg(owner->name));
    return true;
}

void EncodingMonitor::issueRead(Probe &probe)
{
    if (probe.refused) {
        if (!probe.aloneDone) {
            probe.requestId = m_nextRequest++;
            m_probe = probe;
            // Last: a synchronous answer (tests) runs onReadFinished before this returns.
            emit readRequested(probe.requestId, probe.tag.area, probe.tag.address, 1);
        } else if (!issueRefusedRead(probe)) {
            completeRefused(probe);
            maybeFinishConnectPass();
        }
        return;
    }
    const int address = probe.tag.address;
    if (!probe.sidesChecked) {
        // A neighbour that another tag maps counts as absent: it is not read at all.
        probe.belowUsable = address > 0 && !registerUsedByOther(probe.tag, address - 1);
        probe.aboveUsable = address < 65535 && !registerUsedByOther(probe.tag, address + 1);
        probe.sidesChecked = true;
    }
    int first = address;
    int count = 2;
    switch (probe.stage) {
    case 0:
        first = probe.belowUsable ? address - 1 : address;
        count = (probe.aboveUsable ? address + 1 : address) - first + 1;
        break;
    case 1:
        first = address - 1;
        break;
    default:
        break;
    }
    probe.requestId = m_nextRequest++;
    m_probe = probe;
    // Last: a synchronous answer (tests) runs onReadFinished before this returns.
    emit readRequested(probe.requestId, probe.tag.area, quint16(first), count);
}

void EncodingMonitor::onReadFinished(quint64 requestId, const ModbusResult &result,
                                     const QList<quint16> &values)
{
    if (!m_probe || m_probe->requestId != requestId)
        return;
    Probe probe = *m_probe;
    m_probe.reset();
    if (!splitProbeValid(probe)) {
        startNextProbe();
        maybeFinishConnectPass();
        pump();
        return;
    }
    if (result.error == ModbusError::Timeout || result.error == ModbusError::ConnectionLost) {
        // Some devices drop the connection on reads outside their map: stop probing.
        m_splitStopped = true;
        m_probes.clear();
        const Tag *now = findTag(probe.id);
        emit message(QStringLiteral("Stopped checking 16-bit tags: the device did not answer a "
                                    "read next to '%1'.")
                         .arg(now ? now->name : probe.tag.name));
        m_connectWait.clear(); // nothing more will be probed: let the connect pass finish
        maybeFinishConnectPass();
        pump();
        return;
    }
    if (probe.refused && !probe.aloneDone) {
        // The tag's own register, read alone: the pair probes only make sense when the device
        // refuses it with "Illegal data address".
        probe.aloneDone = true;
        const bool refusedAlone = result.error == ModbusError::Exception && result.exceptionCode == 2;
        if (refusedAlone && issueRefusedRead(probe))
            return;
        if (refusedAlone) {
            completeRefused(probe);
        } else if (result.ok()) {
            // Readable on its own (e.g. the tag only shared a failed block with a refused one):
            // not a refused read. The normal split check takes over, with its own read.
            queueProbe(probe.id, probe.mode);
        } else {
            if (const Tag *now = findTag(probe.id)) {
                m_sessionChecked.insert(now->id);
                if (probe.mode == ProbeMode::Manual) {
                    emit message(QStringLiteral("'%1' (%2 %3) could not be read: %4.")
                                     .arg(now->name, areaPrefix(now->area))
                                     .arg(now->address + m_model->addressBase())
                                     .arg(result.message));
                }
            }
        }
        startNextProbe();
        maybeFinishConnectPass();
        pump();
        return;
    }
    if (probe.refused) {
        if (result.ok() && values.size() == 2) {
            const RegisterPair pair{values[0], values[1]};
            (probe.stage == 0 ? probe.pairOwn : probe.pairBelow) = pair;
        }
        ++probe.stage;
        if (issueRefusedRead(probe))
            return;
        completeRefused(probe);
        startNextProbe();
        maybeFinishConnectPass();
        pump();
        return;
    }
    const bool hasBelow = probe.belowUsable;
    const bool hasAbove = probe.aboveUsable;
    const int expected = probe.stage == 0 ? 1 + (hasBelow ? 1 : 0) + (hasAbove ? 1 : 0) : 2;
    const bool ok = result.ok() && values.size() == expected;
    bool done = true;
    switch (probe.stage) {
    case 0:
        if (ok) {
            int i = 0;
            if (hasBelow)
                probe.below = values[i++];
            probe.own = values[i++];
            if (hasAbove)
                probe.above = values[i];
        } else if (hasBelow && hasAbove) {
            probe.stage = 1; // one read per side
            done = false;
        }
        break;
    case 1:
        if (ok) {
            probe.below = values[0];
            probe.own = values[1];
        }
        if (hasAbove) {
            probe.stage = 2;
            done = false;
        }
        break;
    default:
        if (ok) {
            probe.own = values[0];
            probe.above = values[1];
        }
        break;
    }
    if (!done) {
        issueRead(probe);
        return;
    }
    completeProbe(probe);
    startNextProbe();
    maybeFinishConnectPass();
    pump();
}

QString EncodingMonitor::splitNote(const Tag &tag, const SplitValue &split) const
{
    const int base = m_model->addressBase();
    return QStringLiteral("%1 %2-%3 read as %4 give %5")
        .arg(areaPrefix(tag.area)).arg(split.address + base).arg(split.address + 1 + base)
        .arg(toString(split.encoding), QString::number(split.value, 'g', 7));
}

void EncodingMonitor::flagSplit(const Tag &tag, const SplitValue &split)
{
    m_model->setEncodingVerified(tag.id, false);
    m_model->setEncodingNote(tag.id, splitNote(tag, split));
    if (const Tag *now = findTag(tag.id))
        m_snapshot.insert(tag.id, *now);
}

void EncodingMonitor::enqueueSplit(const Tag &tag, const SplitValue &split, quint16 own,
                                   quint64 epoch)
{
    m_queue.removeAll(Request{tag.id, {}, 0});
    Request request;
    request.id = tag.id;
    request.tag = tag;
    request.epoch = epoch;
    request.split = true;
    request.splitValue = split;
    request.own = own;
    m_queue.append(request);
}

void EncodingMonitor::completeProbe(const Probe &probe)
{
    const Tag *found = findTag(probe.id);
    if (!found)
        return;
    const Tag tag = *found;
    if (!probe.own) {
        if (probe.mode == ProbeMode::Manual)
            emit message(QStringLiteral("Could not read the registers around '%1'.").arg(tag.name));
        return;
    }
    m_sessionChecked.insert(tag.id); // checked once: not again on reconnect
    // A neighbour another tag maps by now counts as absent too.
    std::optional<quint16> below = probe.below;
    std::optional<quint16> above = probe.above;
    if (below && registerUsedByOther(tag, tag.address - 1))
        below.reset();
    if (above && registerUsedByOther(tag, tag.address + 1))
        above.reset();
    const std::optional<SplitValue> split = detectSplitValue(tag, *probe.own, below, above);
    if (!split) {
        const QString where = QStringLiteral("%1 %2").arg(areaPrefix(tag.area))
                                  .arg(tag.address + m_model->addressBase());
        if (probe.mode == ProbeMode::Manual) {
            if (!tag.encodingVerified) {
                // Flagged earlier and nothing found right now: the flag stays (the user decides).
                Request request;
                request.id = tag.id;
                request.tag = tag;
                request.epoch = probe.epoch;
                request.split = true;
                request.noSplit = true;
                request.own = *probe.own;
                m_queue.removeAll(Request{tag.id, {}, 0});
                m_queue.append(request);
                return;
            }
            emit message(QStringLiteral("'%1' (%2) looks like a 16-bit value: no float32 found "
                                        "around it.")
                             .arg(tag.name, where));
        } else if (!tag.encodingVerified && m_model->encodingNote(tag.id).isEmpty()) {
            // A stale flag (the tag moved, or was a float32 tag): nothing to warn about.
            m_model->setEncodingVerified(tag.id, true);
            if (const Tag *now = findTag(tag.id))
                m_snapshot.insert(tag.id, *now);
        }
        return;
    }
    if (probe.mode == ProbeMode::Connect) {
        m_connectFound.append(SplitFinding{tag.id, tag, probe.epoch, *split, *probe.own});
        return;
    }
    flagSplit(tag, *split);
    enqueueSplit(tag, *split, *probe.own, probe.epoch);
}

void EncodingMonitor::splitCheckNow(const Tag &tag)
{
    if (!m_connected) {
        emit message(QStringLiteral("Connect to check '%1'.").arg(tag.name));
        return;
    }
    if (m_splitStopped) {
        emit message(QStringLiteral("Stopped checking 16-bit tags: the device did not answer a "
                                    "read next to a 16-bit tag."));
        return;
    }
    queueProbe(tag.id, ProbeMode::Manual);
}

void EncodingMonitor::maybeFinishConnectPass()
{
    if (!m_passActive || !m_connectWait.isEmpty())
        return;
    if (m_probe && m_probe->mode == ProbeMode::Connect)
        return;
    if (std::any_of(m_probes.begin(), m_probes.end(),
                    [](const Probe &p) { return p.mode == ProbeMode::Connect; }))
        return;
    m_passActive = false;
    const QList<SplitFinding> found = std::exchange(m_connectFound, {});
    QList<SplitFinding> flagged;
    QStringList entries;
    for (const SplitFinding &f : found) {
        const Tag *tag = findTag(f.id);
        if (f.epoch != m_epoch || !tag || wide(*tag) || tag->area != f.tag.area
            || tag->address != f.tag.address || tag->type != f.tag.type)
            continue;
        flagSplit(*tag, f.split);
        const double reading = decodeValue(*tag, combineRegisters(*tag, f.own));
        const int base = m_model->addressBase();
        entries << QStringLiteral("'%1' (%2 %3): %4 as %5, <b>%6</b> as %7 (%2 %8-%9)")
                       .arg(rich::escaped(tag->name), areaPrefix(tag->area)).arg(tag->address + base)
                       .arg(QString::number(reading, 'g', 7), toString(tag->type),
                            QString::number(f.split.value, 'g', 7), toString(f.split.encoding))
                       .arg(f.split.address + base).arg(f.split.address + 1 + base);
        flagged << f;
    }
    if (flagged.isEmpty())
        return;
    const QString text =
        rich::headline(tagCount(flagged.size(),
                                QStringLiteral("is probably half of a float32 value:"),
                                QStringLiteral("are probably half of a float32 value:")))
        + rich::bulletsRich(entries)
        + rich::paragraph(QStringLiteral("Writes to them ask first. Review them now?"));
    // Shown by pump() as soon as no prompt or dialog is open (now, usually).
    m_deferred16 = Review16{text, flagged};
    pump();
}

void EncodingMonitor::runSplitRequest(const Request &request)
{
    if (!splitRequestValid(request))
        return;
    const Tag tag = *findTag(request.id);
    const SplitValue split = request.splitValue;
    const QString where = QStringLiteral("%1 %2").arg(areaPrefix(tag.area))
                              .arg(tag.address + m_model->addressBase());
    const double reading = decodeValue(tag, combineRegisters(tag, request.own));
    QString text;
    QString changeLabel;
    if (request.noSplit) {
        text = rich::headline(QStringLiteral("No float32 found around '%1' (%2) right now.")
                                  .arg(tag.name, where))
               + rich::paragraphRich(QStringLiteral("It stays flagged. Choose %1 if it is a 16-bit "
                                                    "value.")
                                         .arg(rich::bold(QStringLiteral("Keep %1")
                                                             .arg(toString(tag.type)))));
    } else {
        const int base = m_model->addressBase();
        text = rich::headline(QStringLiteral("'%1' is probably half of a float32 value.")
                                  .arg(tag.name))
               + rich::rows(
                   {{QStringLiteral("As %1 (%2):").arg(toString(tag.type), where),
                     QString::number(reading, 'g', 7)},
                    {QStringLiteral("As %1 (%2 %3-%4):")
                         .arg(toString(split.encoding), areaPrefix(tag.area))
                         .arg(split.address + base)
                         .arg(split.address + 1 + base),
                     QString::number(split.value, 'g', 7), true}})
               + rich::paragraph(
                   QStringLiteral("Writing a 16-bit value here would change half of the float."));
        if (tag.gain != 1.0 || tag.offset != 0.0 || tag.quickA || tag.quickB)
            text += rich::note(QStringLiteral("Change also resets its gain and offset to 1 and 0 "
                                              "and clears its quick values."));
        changeLabel = QStringLiteral("Change to %1 at %2 %3")
                          .arg(toString(split.encoding), areaPrefix(tag.area))
                          .arg(split.address + m_model->addressBase());
    }
    const QString keepLabel = QStringLiteral("Keep %1").arg(toString(tag.type));
    m_promptOpen = true;
    const SplitAnswer answer =
        m_splitPrompt ? m_splitPrompt(text, changeLabel, keepLabel) : SplitAnswer::Later;
    m_promptOpen = false;
    if (!splitRequestValid(request))
        return; // removed or reconfigured meanwhile: the answer no longer applies
    switch (request.noSplit && answer == SplitAnswer::Change ? SplitAnswer::Later : answer) {
    case SplitAnswer::Change: {
        if (pairBlocked(*findTag(request.id), split.address))
            break;
        // The snapshot first: the nested tagsEdited -> onTagsChanged must not see a new
        // 32-bit tag to detect.
        Tag moved = *findTag(request.id);
        moved.address = split.address;
        moved.type = split.encoding.type;
        moved.byteOrder = split.encoding.order;
        moved.encodingVerified = true;
        m_snapshot.insert(request.id, moved);
        m_model->setLayout(request.id, split.address, split.encoding.type, split.encoding.order,
                           true);
        if (const Tag *now = findTag(request.id))
            m_snapshot.insert(request.id, *now);
        break;
    }
    case SplitAnswer::Keep:
        m_model->setEncodingVerified(request.id, true);
        m_model->setConfirmed16Bit(request.id, true);
        if (const Tag *now = findTag(request.id))
            m_snapshot.insert(request.id, *now);
        break;
    case SplitAnswer::Later:
        break;
    }
}

// --- Refused 16-bit reads -----------------------------------------------------------

void EncodingMonitor::queueRefused(TagId id, ProbeMode mode)
{
    const Tag *tag = findTag(id);
    if (!tag || wide(*tag) || !m_connected)
        return;
    if (mode != ProbeMode::Manual && tag->confirmed16Bit)
        return;
    if (m_splitStopped)
        return;
    const bool busy = (m_probe && m_probe->id == id)
                      || std::any_of(m_probes.begin(), m_probes.end(),
                                     [id](const Probe &p) { return p.id == id; });
    if (busy)
        return;
    Probe probe;
    probe.id = id;
    probe.tag = *tag;
    probe.epoch = m_epoch;
    probe.generation = m_generation;
    probe.mode = mode;
    probe.refused = true;
    m_probes.append(probe);
    startNextProbe();
}

void EncodingMonitor::refusedCheckNow(const Tag &tag)
{
    if (!m_connected) {
        emit message(QStringLiteral("Connect to check '%1'.").arg(tag.name));
        return;
    }
    if (m_splitStopped) {
        emit message(QStringLiteral("Stopped checking 16-bit tags: the device did not answer a "
                                    "read next to a 16-bit tag."));
        return;
    }
    queueRefused(tag.id, ProbeMode::Manual);
}

bool EncodingMonitor::issueRefusedRead(Probe &probe)
{
    for (; probe.stage < 2; ++probe.stage) {
        const int start = probe.tag.address - probe.stage;
        if (start < 0 || start + 1 > 65535 || registerUsedByOther(probe.tag, start)
            || registerUsedByOther(probe.tag, start + 1))
            continue;
        probe.requestId = m_nextRequest++;
        m_probe = probe;
        // Last: a synchronous answer (tests) runs onReadFinished before this returns.
        emit readRequested(probe.requestId, probe.tag.area, quint16(start), 2);
        return true;
    }
    return false;
}

void EncodingMonitor::completeRefused(const Probe &probe)
{
    const Tag *found = findTag(probe.id);
    if (!found)
        return;
    const Tag tag = *found;
    m_sessionChecked.insert(tag.id); // checked once: not again on reconnect
    const int base = m_model->addressBase();
    // A pair another tag has taken since it was read no longer counts.
    std::optional<RegisterPair> pairOwn = probe.pairOwn;
    std::optional<RegisterPair> pairBelow = probe.pairBelow;
    if (pairOwn && (registerUsedByOther(tag, tag.address) || registerUsedByOther(tag, tag.address + 1)))
        pairOwn.reset();
    if (pairBelow
        && (registerUsedByOther(tag, tag.address - 1) || registerUsedByOther(tag, tag.address)))
        pairBelow.reset();
    if (!pairOwn && !pairBelow) {
        if (probe.mode == ProbeMode::Manual) {
            emit message(QStringLiteral("'%1' (%2 %3) can't be read, alone or as part of a 32-bit "
                                        "value: check the register number.")
                             .arg(tag.name, areaPrefix(tag.area)).arg(tag.address + base));
        }
        return;
    }
    // The 32-bit view of the tag at `start`, and how plausible the pair reads in it.
    const std::optional<ByteOrder> order = deviceOrder();
    const auto viewAt = [&](int start) {
        Tag view = tag;
        view.address = quint16(start);
        view.type = ValueType::Float32;
        view.byteOrder = order.value_or(ByteOrder::ABCD);
        view.gain = 1.0;
        view.offset = 0.0;
        view.quickA.reset();
        view.quickB.reset();
        return view;
    };
    const auto scoreOf = [&](int start, const RegisterPair &pair) {
        EncodingOptions options;
        options.deviceOrder = order;
        const EncodingResult result = detectEncoding(viewAt(start), {pair}, options);
        return result.candidates.isEmpty() ? 0.0 : result.candidates.first().score;
    };
    int start = tag.address;
    RegisterPair pair;
    if (pairOwn && pairBelow) {
        // Tie: the pair starting at the tag's own address.
        const bool below = scoreOf(tag.address - 1, *pairBelow)
                           > scoreOf(tag.address, *pairOwn);
        start = below ? tag.address - 1 : tag.address;
        pair = below ? *pairBelow : *pairOwn;
    } else if (pairBelow) {
        start = tag.address - 1;
        pair = *pairBelow;
    } else {
        pair = *pairOwn;
    }

    const QString prefix = areaPrefix(tag.area);
    m_model->setEncodingVerified(tag.id, false);
    m_model->setEncodingNote(
        tag.id, QStringLiteral("%1 %2 can't be read alone; %1 %3-%4 can be read as a 32-bit value")
                    .arg(prefix).arg(tag.address + base).arg(start + base).arg(start + 1 + base));
    if (const Tag *now = findTag(tag.id))
        m_snapshot.insert(tag.id, *now);

    m_queue.removeAll(Request{tag.id, {}, 0});
    Request request;
    request.id = tag.id;
    request.tag = tag;
    request.epoch = probe.epoch;
    request.refused = true;
    request.generation = probe.generation;
    request.view = viewAt(start);
    request.start = quint16(start);
    request.pair = pair;
    m_queue.append(request);
}

void EncodingMonitor::runRefusedRequest(const Request &request)
{
    if (!splitRequestValid(request) || request.generation != m_generation)
        return; // removed/reconfigured, or queued on an earlier connection
    const Tag tag = *findTag(request.id);
    if (pairBlocked(tag, request.start))
        return;
    const int base = m_model->addressBase();
    const QString prefix = areaPrefix(tag.area);
    QList<Tag> others;
    for (const Tag &other : m_model->tags()) {
        if (other.id != tag.id && wide(other))
            others.append(other);
    }
    EncodingDialog dialog(request.view, {request.pair}, others, deviceOrder(), base,
                          m_dialogParent);
    dialog.setIntro(
        rich::headline(QStringLiteral("'%1' (%2 %3, %4) can't be read alone.")
                           .arg(tag.name, prefix).arg(tag.address + base).arg(toString(tag.type)))
        + rich::paragraph(QStringLiteral("The device answered \"Illegal data address\". %1 %2-%3 "
                                         "can be read as a 32-bit value: choose its encoding, or "
                                         "type the value shown on the device.")
                              .arg(prefix).arg(request.start + base).arg(request.start + 1 + base)));
    m_openDialog = &dialog;
    m_openId = tag.id;
    const bool accepted = m_runDialog && m_runDialog(dialog);
    m_openDialog = nullptr;
    m_openId = 0;
    if (!accepted || !splitRequestValid(request))
        return; // cancelled (the tag stays flagged), or removed/reconfigured meanwhile
    const std::optional<Encoding> chosen = dialog.chosenEncoding();
    if (!chosen)
        return;
    if (pairBlocked(*findTag(request.id), request.start))
        return;
    // The snapshot first: the nested tagsEdited -> onTagsChanged must not see a new 32-bit tag
    // to detect.
    Tag moved = *findTag(request.id);
    moved.address = request.start;
    moved.type = chosen->type;
    moved.byteOrder = chosen->order;
    moved.encodingVerified = true;
    m_snapshot.insert(request.id, moved);
    m_model->setLayout(request.id, request.start, chosen->type, chosen->order, true);
    if (const Tag *now = findTag(request.id))
        m_snapshot.insert(request.id, *now);
    applyOrderToOthers(dialog, chosen->order);
}

} // namespace mb

void mb::EncodingMonitor::confirmDetected(mb::TagId id)
{
    const mb::Tag *tag = findTag(id);
    if (!tag)
        return;
    if (!wide(*tag)) {
        forgetSplit(id);
        m_sessionChecked.insert(id);
        return;
    }
    finishPending(id);
    m_readings.remove(id);
    m_queue.removeAll(Request{id, {}, 0});
    // The nested tagsEdited -> onTagsChanged sees only the flag change (same area/address/layout).
    if (!tag->encodingVerified)
        m_model->setEncodingVerified(id, true);
    if (const mb::Tag *now = findTag(id))
        m_snapshot.insert(id, *now);
    m_sessionChecked.insert(id);
}
