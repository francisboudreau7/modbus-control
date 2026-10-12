#include "EncodingMonitor.h"

#include "EncodingDialog.h"
#include "TagTableModel.h"

#include <QMessageBox>
#include <QPushButton>
#include <QStringList>

#include <algorithm>

namespace mb {

namespace {

bool wide(const Tag &tag)
{
    return registerCount(tag.type) == 2;
}

} // namespace

EncodingMonitor::EncodingMonitor(TagTableModel *model, QWidget *dialogParent, QObject *parent)
    : QObject(parent), m_model(model), m_dialogParent(dialogParent)
{
    m_runDialog = [](EncodingDialog &dialog) { return dialog.exec() == QDialog::Accepted; };
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
        if (!found || !wide(*found))
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
}

void EncodingMonitor::onDisconnected()
{
    m_connected = false;
    m_readings.clear();
    for (auto it = m_collected.begin(); it != m_collected.end(); ++it)
        it.value() = 0;
    m_checkActive = false;
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
        if (!wide(tag)) {
            finishPending(tag.id);
            m_readings.remove(tag.id);
            continue;
        }
        const auto it = m_snapshot.constFind(tag.id);
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
}

void EncodingMonitor::detectNow(TagId id)
{
    const Tag *tag = findTag(id);
    if (!tag || !wide(*tag))
        return;
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
    while (!m_openDialog && !m_queue.isEmpty()) {
        const Request request = m_queue.takeFirst();
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

} // namespace mb
