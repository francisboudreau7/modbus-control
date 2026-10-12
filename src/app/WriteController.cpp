#include "WriteController.h"

#include "EncodingDetect.h"
#include "TagCodec.h"
#include "TagTableModel.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>

#include <algorithm>
#include <cmath>

namespace mb {

namespace {

QString typeText(const Tag &tag)
{
    return toString(tag.type);
}

/// Raw value as shown in the confirmation: decimal for 16-bit, 0x%08X (ABCD pattern) for 32-bit.
QString rawText(const Tag &tag, const QList<quint16> &regs)
{
    if (registerCount(tag.type) == 2 && regs.size() >= 2)
        return QStringLiteral("0x")
               + QString::number(combineRegisters(tag, regs.at(0), regs.at(1)), 16)
                     .rightJustified(8, QLatin1Char('0'))
                     .toUpper();
    const quint16 raw = regs.value(0);
    return tag.type == ValueType::Int16 ? QString::number(static_cast<qint16>(raw))
                                        : QString::number(raw);
}

QString registerRange(const Tag &tag, int base)
{
    const int n = registerCount(tag.type);
    return n == 2 ? QStringLiteral("HR %1-%2").arg(tag.address + base).arg(tag.address + 1 + base)
                  : QStringLiteral("HR %1").arg(tag.address + base);
}

QString valueText(double v)
{
    return QString::number(v, 'g', 6);
}

QString tagLabel(const Tag &tag, int base)
{
    return QStringLiteral("%1 (HR %2)").arg(tag.name).arg(tag.address + base);
}

QString unitSuffix(const Tag &tag)
{
    return tag.unit.isEmpty() ? QString() : QStringLiteral(" ") + tag.unit;
}

/// "0 to 100", "at least 0" or "at most 100" (no unit); empty when no limit is set.
QString limitsText(const Tag &tag)
{
    if (tag.writeMin && tag.writeMax)
        return QObject::tr("%1 to %2").arg(valueText(*tag.writeMin), valueText(*tag.writeMax));
    if (tag.writeMin)
        return QObject::tr("at least %1").arg(valueText(*tag.writeMin));
    if (tag.writeMax)
        return QObject::tr("at most %1").arg(valueText(*tag.writeMax));
    return QString();
}

/// "'Setpoint' accepts 0 to 100 °C; 150 was not written."
QString limitViolationText(const Tag &tag, double value)
{
    return QObject::tr("'%1' accepts %2%3; %4 was not written.")
        .arg(tag.name, limitsText(tag), unitSuffix(tag), valueText(value));
}

/// Typical of a wrong byte order or register: not finite, huge or tiny non-zero.
bool implausibleFloat(double x)
{
    if (!std::isfinite(x))
        return true;
    const double a = std::abs(x);
    return a > 1e9 || (a > 0.0 && a < 1e-6);
}

/// One line edit per tag; values are parsed with the C locale.
class ValuesDialog : public QDialog
{
public:
    ValuesDialog(const QList<Tag> &tags, const QList<double> &current, int base, QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(QObject::tr("Write registers"));
        auto *form = new QFormLayout(this);
        auto *validator = new QDoubleValidator(this);
        validator->setLocale(QLocale::c());
        validator->setNotation(QDoubleValidator::StandardNotation);
        for (qsizetype i = 0; i < tags.size(); ++i) {
            // Shortest round-trip form, so an untouched field writes back exactly the same value.
            const double v = i < current.size() ? current.at(i) : 0.0;
            auto *edit = new QLineEdit(QString::number(v, 'g', QLocale::FloatingPointShortest), this);
            edit->setValidator(validator);
            const QString unit = tags.at(i).unit.isEmpty() ? QString()
                                                           : QStringLiteral(" [%1]").arg(tags.at(i).unit);
            form->addRow(tagLabel(tags.at(i), base) + unit, edit);
            m_edits.append(edit);
        }
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    std::optional<QList<double>> values() const
    {
        QList<double> out;
        for (const QLineEdit *edit : m_edits) {
            bool ok = false;
            const double v = QLocale::c().toDouble(edit->text().trimmed(), &ok);
            if (!ok)
                return std::nullopt;
            out.append(v);
        }
        return out;
    }

private:
    QList<QLineEdit *> m_edits;
};

} // namespace

WriteController::WriteController(TagTableModel *model, QWidget *dialogParent, QObject *parent)
    : QObject(parent), m_model(model), m_dialogParent(dialogParent)
{
    m_confirm = [this](const QString &question) {
        return QMessageBox::question(m_dialogParent, tr("Confirm write"), question,
                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
               == QMessageBox::Yes;
    };
    m_offerSingleWrites = [this](const QString &text) {
        QMessageBox box(QMessageBox::Warning, tr("Write rejected"), text, QMessageBox::NoButton,
                        m_dialogParent);
        QPushButton *use = box.addButton(tr("Use FC06 only and reconnect"), QMessageBox::AcceptRole);
        box.addButton(tr("Close"), QMessageBox::RejectRole);
        box.exec();
        return box.clickedButton() == use;
    };
    m_quickConfirmPrompt = [this](const QString &question) {
        QMessageBox box(QMessageBox::Question, tr("Confirm write"), question,
                        QMessageBox::Yes | QMessageBox::No, m_dialogParent);
        box.setDefaultButton(QMessageBox::No);
        QCheckBox dontAsk(tr("Don't ask again this session"));
        box.setCheckBox(&dontAsk);
        const bool yes = box.exec() == QMessageBox::Yes;
        return QuickAnswer{yes, yes && dontAsk.isChecked()};
    };
    m_warn = [this](const QString &text) {
        QMessageBox::warning(m_dialogParent, tr("Write"), text);
    };
    m_warnImplausible = [this](const QString &text) {
        QMessageBox box(QMessageBox::Warning, tr("Write"), text, QMessageBox::NoButton, m_dialogParent);
        QPushButton *anyway = box.addButton(tr("Write anyway"), QMessageBox::AcceptRole);
        QPushButton *cancel = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(cancel);
        box.exec();
        return box.clickedButton() == anyway;
    };
    m_confirmEncoding = [this](const QString &text) {
        QMessageBox box(QMessageBox::Warning, tr("Confirm write"), text, QMessageBox::NoButton,
                        m_dialogParent);
        QPushButton *confirm = box.addButton(tr("Confirm and continue"), QMessageBox::AcceptRole);
        QPushButton *detect = box.addButton(tr("Detect encoding..."), QMessageBox::ActionRole);
        QPushButton *cancel = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(cancel);
        box.setEscapeButton(cancel);
        box.exec();
        if (box.clickedButton() == confirm)
            return EncodingAnswer::Confirm;
        if (box.clickedButton() == detect)
            return EncodingAnswer::Detect;
        return EncodingAnswer::Cancel;
    };
    m_askValues = [this](const QList<Tag> &tags,
                         const QList<double> &current) -> std::optional<QList<double>> {
        ValuesDialog dlg(tags, current, m_model->addressBase(), m_dialogParent);
        while (dlg.exec() == QDialog::Accepted) {
            if (auto v = dlg.values())
                return v;
            m_warn(tr("Please enter a valid number in every field."));
        }
        return std::nullopt;
    };
}

void WriteController::requestWrite(TagId id, double scaledValue)
{
    writeValue(id, scaledValue, false);
}

bool WriteController::stillSame(const Tag &tag) const
{
    const int row = m_model->rowOf(tag.id);
    if (row < 0)
        return false;
    // encodingVerified is not part of the configuration: confirming it must not abort a write.
    // But a tag that became unconfirmed meanwhile (any width: e.g. the on-connect check flagged it
    // while a confirmation was open) must not be written.
    Tag current = m_model->tagAt(row);
    if (!current.encodingVerified)
        return false;
    current.encodingVerified = tag.encodingVerified;
    return current == tag;
}

void WriteController::writeValue(TagId id, double scaledValue, bool quick)
{
    if (!m_connected) {
        emit message(tr("Not connected — cannot write."));
        return;
    }
    const int row = m_model->rowOf(id);
    if (row < 0)
        return;
    const Tag tag = m_model->tagAt(row);
    const int base = m_model->addressBase();
    if (!tag.isWritable())
        return;
    const quint64 generation = m_connectionGeneration;

    // 2. write limits
    if (std::isfinite(scaledValue) && !tag.withinWriteLimits(scaledValue)) {
        m_warn(limitViolationText(tag, scaledValue));
        return;
    }

    // 3. 32-bit tags need a valid reading
    const bool wide = registerCount(tag.type) == 2;
    const std::optional<double> current = m_model->goodValue(id);
    if (wide && !current) {
        m_warn(tr("No valid reading of '%1' yet. Check the connection, register number and byte "
                  "order before writing.")
                   .arg(tag.name));
        return;
    }

    // 3b. tag whose encoding is not confirmed yet (32-bit), or a 16-bit tag flagged as probably
    // half of a 32-bit value
    if (!tag.encodingVerified) {
        QString text;
        if (wide) {
            text = tr("The encoding of '%1' is not confirmed: it reads %2 as %3. Writing "
                      "with a wrong encoding sends a wrong value.")
                       .arg(tag.name, valueText(*current) + unitSuffix(tag),
                            toString(encodingOf(tag)));
        } else {
            const QString note = m_model->encodingNote(id);
            const QString regNo = QStringLiteral("%1 %2")
                                      .arg(tag.area == Area::InputRegister ? QStringLiteral("IR")
                                                                           : QStringLiteral("HR"))
                                      .arg(tag.address + base);
            text = tr("'%1' is probably half of a 32-bit value: %2 Writing a 16-bit value there "
                      "changes half of that value, and the device then reads a wrong number.")
                       .arg(tag.name, note.isEmpty() ? tr("%1 may be half of a 32-bit value.").arg(regNo)
                                                     : note);
        }
        switch (m_confirmEncoding(text)) {
        case EncodingAnswer::Confirm:
            m_model->setEncodingVerified(id, true);
            break;
        case EncodingAnswer::Detect:
            emit detectEncodingRequested(id);
            return;
        case EncodingAnswer::Cancel:
            return;
        }
    }

    // 4. encodable
    const auto raw = encodeRegisters(tag, scaledValue);
    if (!raw) {
        m_warn(tr("Value %1 is out of range for %2 (%3, gain %4, offset %5).")
                   .arg(valueText(scaledValue), tag.name, typeText(tag), valueText(tag.gain),
                        valueText(tag.offset)));
        return;
    }

    // 5. implausible current float
    if (tag.type == ValueType::Float32 && current && implausibleFloat(*current)) {
        if (!m_warnImplausible(
                tr("The current value of '%1' is %2, which is implausible. This usually means a "
                   "wrong byte order or register number, so the value you write may also be "
                   "misinterpreted by the device.\n\nWrite anyway?")
                    .arg(tag.name, valueText(*current))))
            return;
    }

    const QString unit = unitSuffix(tag);
    const QString typeInfo = wide ? tr(" [%1, %2]").arg(typeText(tag), toString(tag.byteOrder))
                                  : QString();
    QString question = tr("Write %1%2 (raw %3) to %4 '%5'%6?")
                           .arg(valueText(scaledValue), unit, rawText(tag, *raw),
                                registerRange(tag, base), tag.name, typeInfo);
    question += tr("\nCurrent value: %1")
                    .arg(current ? valueText(*current) + unit : tr("unknown"));
    const QString limits = limitsText(tag);
    if (!limits.isEmpty())
        question += tr("\nLimits: %1%2").arg(limits, unit);
    if (quick && !wide) {
        if (m_quickConfirm) {
            const QuickAnswer answer = m_quickConfirmPrompt(question);
            if (!answer.confirmed)
                return;
            if (answer.dontAskAgain)
                setQuickConfirm(false);
        }
    } else if (!m_confirm(question)) {
        return;
    }

    // The dialogs run an event loop: the connection, the tag or its reading may have changed.
    if (!m_connected || generation != m_connectionGeneration) {
        emit message(tr("Not connected — cannot write."));
        return;
    }
    if (!stillSame(tag) || (wide && !m_model->goodValue(id))) {
        m_warn(tr("'%1' changed or lost its reading while the confirmation was open. Nothing "
                  "was written.").arg(tag.name));
        return;
    }
    const quint64 reqId = m_nextRequest++;
    m_pending.insert(reqId, {id});
    emit message(tr("Writing %1 to %2 '%3' ...").arg(valueText(scaledValue), registerRange(tag, base), tag.name));
    emit writeRegisters(reqId, tag.address, *raw);
}

void WriteController::requestWriteSelection(const QList<int> &rows)
{
    if (!m_connected) {
        emit message(tr("Not connected — cannot write."));
        return;
    }

    const int base = m_model->addressBase();
    const quint64 generation = m_connectionGeneration;
    QList<Tag> tags;
    for (int row : rows) {
        if (row < 0 || row >= m_model->rowCount())
            continue;
        const Tag &tag = m_model->tagAt(row);
        if (tags.contains(tag))
            continue;
        if (!tag.isWritable()) {
            m_warn(tr("'%1' is not a writable holding register.").arg(tag.name));
            return;
        }
        tags.append(tag);
    }
    if (tags.size() < 2 || tags.size() > 123) {
        m_warn(tr("Select between 2 and 123 writable holding registers (selected: %1).").arg(tags.size()));
        return;
    }
    std::sort(tags.begin(), tags.end(), [](const Tag &a, const Tag &b) { return a.address < b.address; });
    int totalRegisters = 0;
    for (qsizetype i = 0; i < tags.size(); ++i) {
        totalRegisters += registerCount(tags.at(i).type);
        if (i == 0)
            continue;
        const Tag &prev = tags.at(i - 1);
        const Tag &next = tags.at(i);
        const int expected = prev.address + registerCount(prev.type);
        if (next.address < expected) {
            m_warn(tr("'%1' (%2) overlaps '%3' (%4).")
                       .arg(next.name, registerRange(next, base), prev.name, registerRange(prev, base)));
            return;
        }
        if (next.address != expected) {
            m_warn(tr("The selected registers are not consecutive: '%1' (%2) should start at HR %3 "
                      "to follow '%4' (%5), but starts at HR %6.")
                       .arg(next.name, registerRange(next, base))
                       .arg(expected + base)
                       .arg(prev.name, registerRange(prev, base))
                       .arg(next.address + base));
            return;
        }
    }
    if (totalRegisters > 123) {
        m_warn(tr("Too many registers: the selection spans %1 registers (maximum 123).").arg(totalRegisters));
        return;
    }

    // 32-bit tags need a valid reading.
    QStringList noReading;
    for (const Tag &tag : tags)
        if (registerCount(tag.type) == 2 && !m_model->goodValue(tag.id))
            noReading.append(tag.name);
    if (!noReading.isEmpty()) {
        m_warn(tr("No valid reading of %1 yet. Check the connection, register number and byte "
                  "order before writing.")
                   .arg(noReading.join(QStringLiteral(", "))));
        return;
    }

    // Prefill with the current scaled values (Value column), else decode of 0.
    QList<double> current;
    for (const Tag &tag : tags) {
        double v = decodeValue(tag, 0);
        const int row = m_model->rowOf(tag.id);
        if (row >= 0) {
            // EditRole carries the full-precision value; the display text is rounded.
            const QVariant live = m_model->data(m_model->index(row, TagTableModel::Value), Qt::EditRole);
            if (live.isValid())
                v = live.toDouble();
        }
        current.append(v);
    }

    const auto values = m_askValues(tags, current);
    if (!values || values->size() != tags.size())
        return;

    // Write limits (all violations in one warning).
    QStringList outside;
    for (qsizetype i = 0; i < tags.size(); ++i)
        if (std::isfinite(values->at(i)) && !tags.at(i).withinWriteLimits(values->at(i)))
            outside.append(limitViolationText(tags.at(i), values->at(i)));
    if (!outside.isEmpty()) {
        m_warn(tr("Values outside the write limits:\n%1").arg(outside.join(QLatin1Char('\n'))));
        return;
    }

    // 3b. unconfirmed encodings (32-bit tags, and 16-bit tags flagged as half of one): one prompt
    // for all of them
    QStringList unconfirmed;
    QList<TagId> unconfirmedIds;
    for (const Tag &tag : std::as_const(tags)) {
        if (tag.encodingVerified)
            continue;
        if (registerCount(tag.type) == 2)
            unconfirmed.append(tr("'%1' (reads %2 as %3)")
                                   .arg(tag.name, valueText(m_model->goodValue(tag.id).value_or(0.0))
                                                      + unitSuffix(tag),
                                        toString(encodingOf(tag))));
        else
            unconfirmed.append(tr("'%1' (probably half of a 32-bit value)").arg(tag.name));
        unconfirmedIds.append(tag.id);
    }
    if (!unconfirmed.isEmpty()) {
        const QString text = tr("The encoding of these tags is not confirmed: %1. Writing with a "
                                "wrong encoding sends a wrong value.")
                                 .arg(unconfirmed.join(QStringLiteral(", ")));
        switch (m_confirmEncoding(text)) {
        case EncodingAnswer::Confirm:
            for (TagId id : std::as_const(unconfirmedIds))
                m_model->setEncodingVerified(id, true);
            break;
        case EncodingAnswer::Detect:
            emit detectEncodingRequested(unconfirmedIds.first());
            return;
        case EncodingAnswer::Cancel:
            return;
        }
    }

    QList<quint16> raws;
    QStringList bad;
    for (qsizetype i = 0; i < tags.size(); ++i) {
        const auto raw = encodeRegisters(tags.at(i), values->at(i));
        if (!raw)
            bad.append(tr("%1 = %2 (%3, gain %4, offset %5)")
                           .arg(tags.at(i).name, valueText(values->at(i)), typeText(tags.at(i)),
                                valueText(tags.at(i).gain), valueText(tags.at(i).offset)));
        else
            raws += *raw;
    }
    if (!bad.isEmpty()) {
        m_warn(tr("Out of range values:\n%1").arg(bad.join(QLatin1Char('\n'))));
        return;
    }

    QStringList implausible;
    for (const Tag &tag : tags) {
        if (tag.type != ValueType::Float32)
            continue;
        const auto cur = m_model->goodValue(tag.id);
        if (cur && implausibleFloat(*cur))
            implausible.append(QStringLiteral("%1 = %2").arg(tag.name, valueText(*cur)));
    }
    if (!implausible.isEmpty()
        && !m_warnImplausible(
            tr("The current value of these float tags is implausible. This usually means a wrong "
               "byte order or register number, so the values you write may also be misinterpreted "
               "by the device:\n%1\n\nWrite anyway?")
                .arg(implausible.join(QLatin1Char('\n')))))
        return;

    const quint16 first = tags.first().address;
    const int last = tags.last().address + registerCount(tags.last().type) - 1;
    const int shownFirst = first + base;
    const int shownLast = last + base;
    if (!m_confirm(tr("Write %1 registers HR %2..%3?").arg(totalRegisters).arg(shownFirst).arg(shownLast)))
        return;

    // The dialogs run an event loop: the connection, the tags or their readings may have changed.
    if (!m_connected || generation != m_connectionGeneration) {
        emit message(tr("Not connected — cannot write."));
        return;
    }
    for (const Tag &tag : std::as_const(tags)) {
        if (!stillSame(tag) || (registerCount(tag.type) == 2 && !m_model->goodValue(tag.id))) {
            m_warn(tr("'%1' changed or lost its reading while the dialog was open. Nothing was "
                      "written.").arg(tag.name));
            return;
        }
    }
    QList<TagId> ids;
    for (const Tag &tag : tags)
        ids.append(tag.id);
    const quint64 reqId = m_nextRequest++;
    m_pending.insert(reqId, ids);
    emit message(tr("Writing %1 registers HR %2..%3 ...").arg(totalRegisters).arg(shownFirst).arg(shownLast));
    emit writeRegisters(reqId, first, raws);
}

void WriteController::onWriteFinished(quint64 requestId, const ModbusResult &result)
{
    const auto it = m_pending.find(requestId);
    if (it == m_pending.end())
        return;
    const QList<TagId> ids = it.value();
    m_pending.erase(it);

    QStringList names;
    for (TagId id : ids) {
        const int row = m_model->rowOf(id);
        if (row >= 0)
            names.append(m_model->tagAt(row).name);
    }

    if (result.ok()) {
        for (TagId id : ids)
            m_model->setStatusText(id, QString());
        emit message(tr("Write OK: %1").arg(names.join(QStringLiteral(", "))));
    } else {
        const QString text = tr("Write failed: %1").arg(result.message);
        for (TagId id : ids)
            m_model->setStatusText(id, text);
        emit message(text);

        // A multi-register write (FC16) rejected as an illegal function: offer FC06 mode.
        int registers = 0;
        for (TagId id : ids) {
            const int row = m_model->rowOf(id);
            if (row >= 0)
                registers += registerCount(m_model->tagAt(row).type);
        }
        if (result.error == ModbusError::Exception && result.exceptionCode == 1 && registers > 1
            && !m_singleRegisterWrites
            && m_offerSingleWrites(
                tr("The device rejected the write of %1 with \"Illegal function\". It probably does "
                   "not support FC16 (write multiple registers). Nothing was written.\n\n"
                   "With FC06 only, each register is written with its own FC06 request. The value "
                   "is not written again automatically: write it again after reconnecting.")
                    .arg(names.join(QStringLiteral(", "))))
            && !m_singleRegisterWrites) { // another offer may have switched while this one was open
            emit singleRegisterWritesRequested();
        }
    }
}

void WriteController::setSingleRegisterWrites(bool on) { m_singleRegisterWrites = on; }

bool WriteController::singleRegisterWrites() const { return m_singleRegisterWrites; }

void WriteController::setConnected(bool connected)
{
    if (!connected)
        ++m_connectionGeneration; // a write prepared before a disconnect must not be sent
    m_connected = connected;
    if (!connected)
        m_pending.clear();
}

void WriteController::requestQuickWrite(mb::TagId id, double scaledValue)
{
    writeValue(id, scaledValue, true);
}

void WriteController::setQuickConfirm(bool on)
{
    if (m_quickConfirm == on)
        return;
    m_quickConfirm = on;
    emit quickConfirmChanged(on);
}

bool WriteController::quickConfirm() const { return m_quickConfirm; }

} // namespace mb
