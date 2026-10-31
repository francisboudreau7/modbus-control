#pragma once

#include <QDoubleSpinBox>

namespace mb {

/// QDoubleSpinBox for user input that accepts "72,3" as well as "72.3" (v1.3.3, see
/// NumberText.h) and always shows a '.' decimal mark (QLocale::c(), no group separators),
/// whatever the system locale. Use it instead of QDoubleSpinBox for every numeric input.
class NumberSpinBox : public QDoubleSpinBox
{
    Q_OBJECT
public:
    explicit NumberSpinBox(QWidget *parent = nullptr);

    QValidator::State validate(QString &input, int &pos) const override;
    double valueFromText(const QString &text) const override;
    QString textFromValue(double value) const override;
    void fixup(QString &input) const override;
};

} // namespace mb
