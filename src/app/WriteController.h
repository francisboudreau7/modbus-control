#pragma once

#include "ModbusConnection.h"
#include "Tag.h"

#include <QHash>
#include <QList>
#include <QObject>

#include <functional>
#include <optional>

class QWidget;

namespace mb {

class TagTableModel;

/// Turns user write requests into PollWorker writes and reports the results.
/// Register numbers in all texts use TagTableModel::addressBase() (protocol address + base).
///
/// requestWrite: if not connected -> message and stop. Encodes the value with
/// encodeRegisters() (1 register for 16-bit types, 2 for 32-bit types); out of range ->
/// warning box. Otherwise asks for confirmation ("Write 12.5 °C (raw 125) to HR 100
/// 'Setpoint'?"; 32-bit: raw shown as 0x%08X and "HR 6639-6640", type and byte order named)
/// and emits writeRegisters with those registers.
///
/// requestWriteSelection: rows must all be writable holding registers that are contiguous
/// when sorted by address (each tag starts right after the previous tag's last register;
/// 16- and 32-bit tags may be mixed; at most 123 registers in total); otherwise explain why
/// in a warning box. Shows a dialog with one value field per tag (prefilled with the current
/// value), encodes all, confirms, and emits one writeRegisters with all registers.
///
/// onWriteFinished: success -> message "Write OK: ..."; failure -> message "Write failed: ..." (no dialog) and
/// TagTableModel::setStatusText() on the affected tags.
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

signals:
    /// Connect to PollWorker::writeRegisters (queued).
    void writeRegisters(quint64 requestId, quint16 address, const QList<quint16> &values);
    /// Status bar text.
    void message(const QString &text);

private:
    // Prompts are replaceable (tests); defaults are the real dialogs.
    std::function<bool(const QString &question)> m_confirm;
    std::function<std::optional<QList<double>>(const QList<Tag> &, const QList<double> &current)> m_askValues;
    std::function<void(const QString &)> m_warn;

    TagTableModel *m_model = nullptr;
    QWidget *m_dialogParent = nullptr;
    bool m_connected = false;
    quint64 m_nextRequest = 1;
    QHash<quint64, QList<TagId>> m_pending;
};

} // namespace mb
