#pragma once

#include "EncodingDetect.h"
#include "Tag.h"

#include <QDialog>
#include <QList>

#include <functional>
#include <optional>

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class TstEncodingDialog;

namespace mb {

/// "Detect encoding" dialog for one 32-bit tag (title "Detect encoding - <tag name>").
///
/// Children (object names, for tests):
/// - "registersLabel": the latest registers in hex, "HR 100-101: 0x4251 0x3333" (register numbers
///   with the address base given to the constructor), or "No reading yet".
/// - "displayValueEdit": QLineEdit "Value shown on the device (optional)"; every edit re-runs
///   detectEncoding with it.
/// - "verdictLabel": one sentence per verdict: Clear "float32 CDAB fits best.", Ambiguous
///   "Several encodings are possible: type the value shown on the device to decide.",
///   Undecidable "Every encoding reads the same value (e.g. 0): wait for another value or type
///   the value shown on the device.", NoMatch "No encoding gives the value shown on the device:
///   check the register number."
/// - "candidateTable": one row per group (best first), columns Encoding (the group's preferred
///   member; the tooltip lists the equivalent encodings), Value (latest reading, 7 significant
///   digits, with the tag's unit), Plausibility ("Good" score >= 0.75, "Possible" >= 0.5,
///   "Unlikely" > 0, "Implausible"), Match (a check mark, "No", or empty without a display
///   value). With a display value, groups that do not match are listed after the matching ones.
///   Selection: single row; the best group is selected after each re-ranking unless the user
///   picked a row since (that choice is kept while its group exists).
/// - "applyOthersCheck": "Also use <order> for the other 32-bit tags: A, B" (checked by default,
///   hidden when there are no other 32-bit tags or they already all use that order); <order>
///   follows the selected row.
/// - OK ("Apply") enabled when a row is selected; Cancel.
class EncodingDialog : public QDialog
{
    Q_OBJECT
public:
    /// others: the project's other 32-bit tags (candidates for the same byte order).
    EncodingDialog(const Tag &tag, const QList<RegisterPair> &readings, const QList<Tag> &others,
                   std::optional<ByteOrder> deviceOrder, int addressBase = 0,
                   QWidget *parent = nullptr);

    /// Appends a live reading (the dialog keeps the last 10) and re-ranks.
    void addReading(const RegisterPair &reading);
    /// Sets the display value field (as if typed) and re-ranks.
    void setDisplayValue(const QString &text);

    /// The current ranking.
    EncodingResult result() const;
    /// The selected encoding, if a row is selected.
    std::optional<Encoding> chosenEncoding() const;
    /// True when the chosen encoding was confirmed by the display value (the selected group
    /// matches it).
    bool matchedDisplayValue() const;
    /// Ids of the other tags to switch to the chosen byte order (empty when the check box is
    /// hidden or unticked). Their types are not changed.
    QList<TagId> applyOrderTo() const;

private:
    friend class ::TstEncodingDialog;

    using DetectFn = std::function<EncodingResult(const Tag &, const QList<RegisterPair> &,
                                                  const EncodingOptions &)>;

    /// Re-runs m_detect and rebuilds the labels, the table and the selection.
    void rerank();
    void onSelectionChanged();
    void updateApplyOthers();
    /// Index into m_result.candidates of the selected row's preferred member, or -1.
    int selectedCandidate() const;
    /// Other tags whose byte order differs from the chosen one.
    QList<const Tag *> othersToChange() const;

    Tag m_tag;
    QList<RegisterPair> m_readings;
    QList<Tag> m_others;
    std::optional<ByteOrder> m_deviceOrder;
    int m_addressBase = 0;
    EncodingResult m_result;

    /// Test seam: the ranking function, detectEncoding by default. A test replaces it and then
    /// calls setDisplayValue()/addReading() to apply it (the constructor has already ranked once
    /// with the default).
    DetectFn m_detect = [](const Tag &tag, const QList<RegisterPair> &readings,
                           const EncodingOptions &options) {
        return detectEncoding(tag, readings, options);
    };
    /// One entry per table row: index of the group's preferred member in m_result.candidates.
    QList<int> m_rowCandidate;
    /// Preferred encoding of the group the user selected; kept across re-ranks while some group
    /// still contains that encoding (groups are identified by membership).
    std::optional<Encoding> m_userChoice;
    bool m_rebuilding = false;

    QLabel *m_registers = nullptr;
    QLineEdit *m_display = nullptr;
    QLabel *m_verdict = nullptr;
    QTableWidget *m_table = nullptr;
    QCheckBox *m_applyOthers = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

} // namespace mb
