#include "NumberSpinBox.h"

#include "NumberText.h"

#include <QLocale>

#include <algorithm>
#include <cmath>

namespace mb {

namespace {

// The editable part of the spin box text, without prefix and suffix.
QString stripAffixes(const QString &text, const QString &prefix, const QString &suffix)
{
    QString s = text;
    if (!prefix.isEmpty() && s.startsWith(prefix))
        s.remove(0, prefix.size());
    if (!suffix.isEmpty() && s.endsWith(suffix))
        s.chop(suffix.size());
    return s.trimmed();
}

} // namespace

NumberSpinBox::NumberSpinBox(QWidget *parent) : QDoubleSpinBox(parent)
{
    QLocale c = QLocale::c();
    c.setNumberOptions(QLocale::OmitGroupSeparator);
    setLocale(c);
}

QValidator::State NumberSpinBox::validate(QString &input, int &pos) const
{
    const QString body = stripAffixes(input, prefix(), suffix());
    if (body.isEmpty() || body == QStringLiteral("-") || body == QStringLiteral("+"))
        return QValidator::Intermediate;
    if (!specialValueText().isEmpty() && input == specialValueText())
        return QValidator::Acceptable;
    // Let the base class judge the text with '.' as the decimal mark (decimals, range).
    QString dotted = input;
    dotted.replace(u',', u'.');
    if (dotted.count(u'.') > 1)
        return QValidator::Invalid;
    return QDoubleSpinBox::validate(dotted, pos);
}

double NumberSpinBox::valueFromText(const QString &text) const
{
    if (!specialValueText().isEmpty() && text == specialValueText())
        return minimum();
    return parseNumber(stripAffixes(text, prefix(), suffix())).value_or(value());
}

QString NumberSpinBox::textFromValue(double value) const
{
    if (m_significantDigits <= 0 || !std::isfinite(value))
        return locale().toString(value, 'f', decimals());
    int dec = decimals();
    if (value != 0.0) {
        const int magnitude = int(std::floor(std::log10(std::abs(value))));
        dec = std::clamp(m_significantDigits - 1 - magnitude, 0, decimals());
    } else {
        dec = 0;
    }
    QString text = locale().toString(value, 'f', dec);
    const QChar point = locale().decimalPoint().isEmpty() ? u'.' : locale().decimalPoint().at(0);
    if (dec > 0 && text.contains(point)) {
        while (text.endsWith(u'0'))
            text.chop(1);
        if (text.endsWith(point))
            text.chop(1);
    }
    return text;
}

void NumberSpinBox::setSignificantDigits(int digits)
{
    m_significantDigits = std::max(0, digits);
    setValue(value()); // refresh the text
}

QValidator::State NumberValidator::validate(QString &input, int &) const
{
    if (parseNumber(input))
        return Acceptable;
    for (const QChar c : std::as_const(input)) {
        if (!(c.isDigit() || QStringLiteral("+-.,eE ").contains(c)))
            return Invalid;
    }
    return Intermediate;
}

void NumberSpinBox::fixup(QString &input) const
{
    input.replace(u',', u'.');
    QDoubleSpinBox::fixup(input);
}

} // namespace mb
