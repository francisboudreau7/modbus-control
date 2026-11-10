#pragma once

#include <QDoubleSpinBox>
#include <QValidator>

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

    /// Shows `digits` significant digits (fixed notation, trailing zeros dropped: 0.00004, 600,
    /// 0.833333) instead of decimals() fixed decimals, so small and large values keep their
    /// precision. 0 (the default) = fixed decimals. Typing is still limited by decimals(): set it
    /// high enough (12) for the smallest value that matters.
    void setSignificantDigits(int digits);
    int significantDigits() const { return m_significantDigits; }

private:
    int m_significantDigits = 0;
};

/// Validator for number line edits: Acceptable when mb::parseNumber accepts the text (comma or
/// point), Intermediate while it only holds digits, signs, marks, spaces and exponents, else
/// Invalid (v1.3.3).
class NumberValidator : public QValidator
{
    Q_OBJECT
public:
    using QValidator::QValidator;
    State validate(QString &input, int &pos) const override;
};

} // namespace mb
