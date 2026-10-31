#include "NumberText.h"

#include <QLocale>

#include <cmath>

namespace mb {

std::optional<double> parseNumber(QStringView text)
{
    QString s = text.trimmed().toString();
    if (s.isEmpty())
        return std::nullopt;
    const qsizetype commas = s.count(u',');
    const qsizetype points = s.count(u'.');
    if (commas + points > 1)
        return std::nullopt;
    s.replace(u',', u'.');
    bool ok = false;
    // QLocale::c() rejects group separators and spaces inside the number.
    const double value = QLocale::c().toDouble(s, &ok);
    if (!ok || !std::isfinite(value))
        return std::nullopt;
    return value;
}

double parseNumber(QStringView text, bool *ok)
{
    const std::optional<double> value = parseNumber(text);
    if (ok)
        *ok = value.has_value();
    return value.value_or(0.0);
}

QString numberText(double value, int significant)
{
    return QLocale::c().toString(value, 'g', significant);
}

} // namespace mb
