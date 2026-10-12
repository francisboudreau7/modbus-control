#include "EncodingMonitor.h"

#include "EncodingDialog.h"
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
        QPushButton *change = box.addButton(changeLabel, QMessageBox::AcceptRole);
        QPushButton *keep = box.addButton(keepLabel, QMessageBox::ActionRole);
        QPushButton *later = box.addButton(QStringLiteral("Decide later"), QMessageBox::RejectRole);
        box.setDefaultButton(later);
        box.setEscapeButton(later);
        box.exec();
        if (box.clickedButton() == change)
            return SplitAnswer::Change;
        if (box.clickedButton() == keep)
            return SplitAnswer::Keep;
        return SplitAnswer::Later;
    };
    m_reviewPrompt = [this](const QString &text) {
        QMessageBox box(QMessageBox::Warning, QStringLiteral("Encoding check"), text,
                        QMessageBox::NoButton, m_dialogParent);
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
        if (sample.quality != Quality::Good)
            continue;
        const Tag *found = findTag(sample.tagId);
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
            if (wide(tag) && !m_pending.contains(tag.id)
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
        if (!wide(tag) && !m_splitPending.contains(tag.id))
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
        if (!wide(tag) || m_pending.contains(tag.id))
            continue;
        const QList<RegisterPair> readings = recent(tag.id, kReadings);
        if (readings.size() < kReadings)
            continue;
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
        entries << QStringLiteral("'%1' reads %2 as %3 (%4 gives %5)")
                       .arg(tag.name, currentValue, toString(current), toString(*result.best),
                            bestValue);
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
        QStringLiteral("These tags read implausible values with their current encoding: %1. "
                       "Writes to them are blocked until you confirm their encoding.")
            .arg(entries.join(QStringLiteral("; ")));
    if (m_reviewPrompt && m_reviewPrompt(text)) {
        for (TagId id : std::as_const(ids))
            detectNow(id);
    }
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
                forgetSplit(tag.id);
                m_queue.removeAll(Request{tag.id, {}, 0});
                m_splitPending.insert(tag.id);
            }
            continue;
        }
        forgetSplit(tag.id);
        const bool moved = it == m_snapshot.constEnd() || !wide(*it) || it->area != tag.area
                           || it->address != tag.address;
        if (moved) {
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
}

void EncodingMonitor::detectNow(TagId id)
{
    const Tag *tag = findTag(id);
    if (!tag)
        return;
    if (!wide(*tag)) {
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
    while (!m_openDialog && !m_promptOpen && !m_queue.isEmpty()) {
        const Request request = m_queue.takeFirst();
        if (request.split)
            runSplitRequest(request);
        else
            runRequest(request);
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
    for (TagId otherId : dialog.applyOrderTo()) {
        const Tag *other = findTag(otherId);
        if (!other || !wide(*other))
            continue;
        const Tag copy = *other;
        // Only the byte order was chosen for them: their encoding stays to be confirmed.
        m_model->setEncoding(otherId, copy.type, chosen->order, false);
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

void EncodingMonitor::issueRead(Probe &probe)
{
    const int address = probe.tag.address;
    int first = address;
    int count = 2;
    switch (probe.stage) {
    case 0:
        first = address > 0 ? address - 1 : 0;
        count = (address < 65535 ? address + 1 : 65535) - first + 1;
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
    const int address = probe.tag.address;
    const bool hasBelow = address > 0;
    const bool hasAbove = address < 65535;
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
    const std::optional<SplitValue> split =
        detectSplitValue(tag, *probe.own, probe.below, probe.above);
    if (!split) {
        if (probe.mode == ProbeMode::Manual) {
            if (!tag.encodingVerified) {
                m_model->setEncodingVerified(tag.id, true);
                if (const Tag *now = findTag(tag.id))
                    m_snapshot.insert(tag.id, *now);
            }
            emit message(QStringLiteral("'%1' (%2 %3) looks like a 16-bit value: no float32 found "
                                        "around it.")
                             .arg(tag.name, areaPrefix(tag.area))
                             .arg(tag.address + m_model->addressBase()));
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
        entries << QStringLiteral("'%1' (%2)").arg(tag->name, splitNote(*tag, f.split));
        flagged << f;
    }
    if (flagged.isEmpty())
        return;
    const QString text =
        QStringLiteral("These 16-bit tags are probably half of a 32-bit value: %1. Writes to them "
                       "ask first.")
            .arg(entries.join(QStringLiteral(", ")));
    if (m_reviewPrompt && m_reviewPrompt(text)) {
        for (const SplitFinding &f : std::as_const(flagged)) {
            if (const Tag *tag = findTag(f.id))
                enqueueSplit(*tag, f.split, f.own, f.epoch);
        }
        pump();
    }
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
    const QString text =
        QStringLiteral("'%1' (%2, %3) reads %4, but %5: this tag is probably half of a float32 "
                       "value.")
            .arg(tag.name, where, toString(tag.type), QString::number(reading, 'g', 7),
                 splitNote(tag, split));
    const QString changeLabel = QStringLiteral("Change to %1 at %2 %3")
                                    .arg(toString(split.encoding), areaPrefix(tag.area))
                                    .arg(split.address + m_model->addressBase());
    const QString keepLabel = QStringLiteral("Keep %1").arg(toString(tag.type));
    m_promptOpen = true;
    const SplitAnswer answer =
        m_splitPrompt ? m_splitPrompt(text, changeLabel, keepLabel) : SplitAnswer::Later;
    m_promptOpen = false;
    if (!splitRequestValid(request))
        return; // removed or reconfigured meanwhile: the answer no longer applies
    switch (answer) {
    case SplitAnswer::Change: {
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
        if (const Tag *now = findTag(request.id))
            m_snapshot.insert(request.id, *now);
        break;
    case SplitAnswer::Later:
        break;
    }
}

} // namespace mb
