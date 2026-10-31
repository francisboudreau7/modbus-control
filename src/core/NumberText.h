#pragma once

#include <QString>
#include <QStringView>

#include <optional>

namespace mb {

/// Parses a number typed by the user, accepting both decimal marks (v1.3.3): "72.3", "72,3",
/// "-0,5", "1e-3", "1,5E2". Leading/trailing spaces are ignored. A single ',' or '.' is the
/// decimal mark; text with both, with two marks, with thousands separators ("1,234.5",
/// "1 234"), empty text, or a non-finite result (inf, nan) gives nullopt.
std::optional<double> parseNumber(QStringView text);

/// Same rules; when `ok` is not null it is set to whether parsing succeeded (0.0 on failure).
/// Drop-in for QString::toDouble(&ok) at input sites.
double parseNumber(QStringView text, bool *ok);

/// The text a number input shows: '.' as the decimal mark whatever the system locale
/// (QLocale::c()), `significant` significant digits ('g' format).
QString numberText(double value, int significant = 6);

} // namespace mb
