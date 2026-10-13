#include "EncodingDialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace mb {

namespace {

constexpr int kMaxReadings = 10;
constexpr int kMaxNames = 5;

QString plausibility(double score)
{
    if (score >= 0.75)
        return EncodingDialog::tr("Good");
    if (score >= 0.5)
        return EncodingDialog::tr("Possible");
    if (score > 0.0)
        return EncodingDialog::tr("Unlikely");
    return EncodingDialog::tr("Implausible");
}

QString hex16(quint16 v)
{
    return QStringLiteral("0x") + QStringLiteral("%1").arg(v, 4, 16, QLatin1Char('0')).toUpper();
}

} // namespace

EncodingDialog::EncodingDialog(const Tag &tag, const QList<RegisterPair> &readings,
                               const QList<Tag> &others, std::optional<ByteOrder> deviceOrder,
                               int addressBase, QWidget *parent)
    : QDialog(parent), m_tag(tag), m_readings(readings), m_others(others),
      m_deviceOrder(deviceOrder), m_addressBase(addressBase)
{
    while (m_readings.size() > kMaxReadings)
        m_readings.removeFirst();

    setWindowTitle(tr("Detect encoding - %1").arg(tag.name));

    m_intro = new QLabel(this);
    m_intro->setObjectName(QStringLiteral("introLabel"));
    m_intro->setTextFormat(Qt::RichText);
    m_intro->setWordWrap(true);
    m_intro->hide();

    m_registers = new QLabel(this);
    m_registers->setObjectName(QStringLiteral("registersLabel"));
    m_registers->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_display = new QLineEdit(this);
    m_display->setObjectName(QStringLiteral("displayValueEdit"));

    m_verdict = new QLabel(this);
    m_verdict->setObjectName(QStringLiteral("verdictLabel"));
    m_verdict->setWordWrap(true);

    m_table = new QTableWidget(0, 4, this);
    m_table->setObjectName(QStringLiteral("candidateTable"));
    m_table->setHorizontalHeaderLabels(
        {tr("Encoding"), tr("Value"), tr("Plausibility"), tr("Match")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    m_applyOthers = new QCheckBox(this);
    m_applyOthers->setObjectName(QStringLiteral("applyOthersCheck"));
    m_applyOthers->setChecked(true);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));

    auto *form = new QFormLayout;
    form->addRow(tr("Registers"), m_registers);
    form->addRow(tr("Value shown on the device (optional)"), m_display);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_intro);
    layout->addLayout(form);
    layout->addWidget(m_verdict);
    layout->addWidget(m_table, 1);
    layout->addWidget(m_applyOthers);
    layout->addWidget(m_buttons);

    connect(m_display, &QLineEdit::textChanged, this, [this] { rerank(); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] { onSelectionChanged(); });
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(520, 420);
    rerank();
}

void EncodingDialog::addReading(const RegisterPair &reading)
{
    m_readings.append(reading);
    while (m_readings.size() > kMaxReadings)
        m_readings.removeFirst();
    rerank();
}

void EncodingDialog::setIntro(const QString &rich)
{
    m_intro->setText(rich);
    m_intro->setVisible(!rich.isEmpty());
}

void EncodingDialog::setDisplayValue(const QString &text)
{
    if (m_display->text() == text)
        rerank(); // setText would not emit textChanged
    else
        m_display->setText(text);
}

EncodingResult EncodingDialog::result() const
{
    return m_result;
}

int EncodingDialog::selectedCandidate() const
{
    const QModelIndexList rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return -1;
    const int r = rows.first().row();
    return (r >= 0 && r < m_rowCandidate.size()) ? m_rowCandidate.at(r) : -1;
}

std::optional<Encoding> EncodingDialog::chosenEncoding() const
{
    const int c = selectedCandidate();
    if (c < 0)
        return std::nullopt;
    return m_result.candidates.at(c).encoding;
}

bool EncodingDialog::matchedDisplayValue() const
{
    const int c = selectedCandidate();
    return c >= 0 && m_result.candidates.at(c).matchesDisplay.value_or(false);
}

QList<const Tag *> EncodingDialog::othersToChange() const
{
    QList<const Tag *> out;
    const auto chosen = chosenEncoding();
    if (!chosen)
        return out;
    // Only tags whose encoding is not confirmed: never change what the user set or confirmed.
    for (const Tag &t : m_others)
        if (!t.encodingVerified && t.byteOrder != chosen->order)
            out.append(&t);
    return out;
}

QList<TagId> EncodingDialog::applyOrderTo() const
{
    QList<TagId> ids;
    if (m_applyOthers->isHidden() || !m_applyOthers->isChecked())
        return ids;
    for (const Tag *t : othersToChange())
        ids.append(t->id);
    return ids;
}

void EncodingDialog::updateApplyOthers()
{
    const auto chosen = chosenEncoding();
    const QList<const Tag *> todo = othersToChange();
    if (!chosen || todo.isEmpty()) {
        m_applyOthers->setVisible(false);
        return;
    }
    QStringList names;
    for (int i = 0; i < todo.size() && i < kMaxNames; ++i)
        names << todo.at(i)->name;
    QString list = names.join(QStringLiteral(", "));
    if (todo.size() > kMaxNames)
        list += tr(" and %1 more").arg(todo.size() - kMaxNames);
    m_applyOthers->setText(
        tr("Also use %1 for the other unconfirmed 32-bit tags: %2").arg(toString(chosen->order), list));
    m_applyOthers->setVisible(true);
}

void EncodingDialog::onSelectionChanged()
{
    if (!m_rebuilding) {
        const auto chosen = chosenEncoding();
        if (chosen)
            m_userChoice = chosen;
    }
    // With a display value, only a matching row can be applied (NoMatch: none).
    const int c = selectedCandidate();
    const bool displayGiven = c >= 0 && m_result.candidates.at(c).matchesDisplay.has_value();
    m_buttons->button(QDialogButtonBox::Ok)
        ->setEnabled(c >= 0 && (!displayGiven || matchedDisplayValue()));
    updateApplyOthers();
}

void EncodingDialog::rerank()
{
    EncodingOptions options;
    options.deviceOrder = m_deviceOrder;
    options.displayValue = m_display->text();
    m_result = m_detect(m_tag, m_readings, options);

    // Registers of the latest reading (a 16-bit tag shows its single register).
    const QString prefix = m_tag.area == Area::InputRegister ? QStringLiteral("IR")
                                                             : QStringLiteral("HR");
    const int first = int(m_tag.address) + m_addressBase;
    if (m_readings.isEmpty()) {
        m_registers->setText(tr("No reading yet"));
    } else {
        const RegisterPair &p = m_readings.last();
        if (registerCount(m_tag.type) == 2)
            m_registers->setText(QStringLiteral("%1 %2-%3: %4 %5")
                                     .arg(prefix).arg(first).arg(first + 1)
                                     .arg(hex16(p.r0), hex16(p.r1)));
        else
            m_registers->setText(
                QStringLiteral("%1 %2: %3").arg(prefix).arg(first).arg(hex16(p.r0)));
    }

    const QString typed = m_display->text().trimmed();
    if (!typed.isEmpty() && !parseDisplayValue(typed)) {
        m_verdict->setText(tr("Can't read \"%1\" as a number: type it like 52.3.").arg(typed));
    } else switch (m_result.verdict) {
    case EncodingVerdict::Clear:
        m_verdict->setText(
            m_result.best ? tr("%1 fits best.").arg(toString(*m_result.best)) : QString());
        break;
    case EncodingVerdict::Ambiguous:
        m_verdict->setText(tr("Several encodings are possible: type the value shown on the "
                              "device to decide."));
        break;
    case EncodingVerdict::Undecidable:
        m_verdict->setText(tr("Every encoding reads the same value (e.g. 0): wait for another "
                              "value or type the value shown on the device."));
        break;
    case EncodingVerdict::NoMatch:
        m_verdict->setText(tr("No encoding gives the value shown on the device: check the "
                              "register number."));
        break;
    }

    // One row per group; the first candidate of a group is its preferred member. Groups that do
    // not match the display value go after the matching ones (stable otherwise).
    struct Group {
        int first = -1;
        QStringList members;
    };
    QList<Group> groups;
    for (int i = 0; i < m_result.candidates.size(); ++i) {
        const EncodingCandidate &c = m_result.candidates.at(i);
        if (c.group < 0)
            continue;
        while (groups.size() <= c.group)
            groups.append(Group{});
        if (groups[c.group].first < 0)
            groups[c.group].first = i;
        groups[c.group].members << toString(c.encoding);
    }
    groups.removeIf([](const Group &g) { return g.first < 0; });
    std::stable_partition(groups.begin(), groups.end(), [this](const Group &g) {
        return m_result.candidates.at(g.first).matchesDisplay.value_or(true);
    });

    const auto previous = m_userChoice;
    m_rebuilding = true;
    m_table->clearSelection();
    m_table->setRowCount(0);
    m_rowCandidate.clear();
    int selectRow = groups.isEmpty() ? -1 : 0;
    bool keptPrevious = false;
    for (int row = 0; row < groups.size(); ++row) {
        const Group &g = groups.at(row);
        const EncodingCandidate &c = m_result.candidates.at(g.first);
        m_rowCandidate.append(g.first);
        m_table->insertRow(row);

        QString value;
        if (!c.values.isEmpty()) {
            value = QString::number(c.values.last(), 'g', 7);
            if (!m_tag.unit.isEmpty())
                value += QLatin1Char(' ') + m_tag.unit;
        }
        QString match;
        if (c.matchesDisplay)
            match = *c.matchesDisplay ? QString(QChar(0x2713)) : tr("No");

        auto *enc = new QTableWidgetItem(toString(c.encoding));
        enc->setToolTip(tr("Equivalent encodings: %1").arg(g.members.join(QStringLiteral(", "))));
        m_table->setItem(row, 0, enc);
        m_table->setItem(row, 1, new QTableWidgetItem(value));
        m_table->setItem(row, 2, new QTableWidgetItem(plausibility(c.score)));
        m_table->setItem(row, 3, new QTableWidgetItem(match));

        if (previous) {
            for (const EncodingCandidate &m : m_result.candidates)
                if (m.group == c.group && m.encoding == *previous) {
                    selectRow = row;
                    keptPrevious = true;
                }
        }
    }
    if (selectRow >= 0)
        m_table->selectRow(selectRow);

    // The user's choice is dropped once its encoding is in no group any more.
    if (previous && !keptPrevious)
        m_userChoice.reset();
    // Still rebuilding: this automatic selection must not count as the user's choice.
    onSelectionChanged();
    m_rebuilding = false;
}

} // namespace mb
