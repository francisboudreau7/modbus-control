#pragma once

#include <QString>
#include <QStringList>

class QSettings;

namespace mb {

/// v1.3.4: interface language choice (Options > Language), applied at the next start.
enum class UiLanguage { System, English, French };

/// Reads QSettings key "ui/language": "system" (also the default and any unknown value),
/// "en" or "fr".
UiLanguage readUiLanguage(const QSettings &settings);
/// Writes QSettings key "ui/language" ("system", "en" or "fr").
void writeUiLanguage(QSettings &settings, UiLanguage language);

/// Language code to load: "fr" or "en". English and French return their code; System returns
/// "fr" when the first entry of `systemUiLanguages` (QLocale::system().uiLanguages()) is French
/// ("fr", "fr-CA", "fr_FR", ...), "en" otherwise (also for an empty list).
QString resolveLanguage(UiLanguage language, const QStringList &systemUiLanguages);

/// Installs the application translators for `code` on the running QCoreApplication:
/// - "fr": Qt's own catalog (":/i18n/qtbase_fr.qm", standard buttons and dialogs) and ours
///   (":/i18n/modbus_control_fr.qm");
/// - "en": ":/i18n/modbus_control_en.qm", which only holds the English plural forms of the %n
///   strings ("1 tag", "2 tags").
/// Any other code installs nothing. Returns true when our catalog was loaded. Calling it again
/// first removes the translators a previous call installed. Numbers are not affected: the app
/// keeps formatting with the point whatever the language.
bool installTranslators(const QString &code);

/// The code passed to the last successful installTranslators() call, "en" before any call.
QString currentLanguage();

} // namespace mb
