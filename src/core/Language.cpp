#include "Language.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTranslator>

#include <memory>

namespace mb {

namespace {

const QString kKey = QStringLiteral("ui/language");

std::unique_ptr<QTranslator> g_qtTranslator;
std::unique_ptr<QTranslator> g_appTranslator;
QString g_current = QStringLiteral("en");

void removeTranslators()
{
    for (auto *t : {&g_qtTranslator, &g_appTranslator}) {
        if (*t) {
            QCoreApplication::removeTranslator(t->get());
            t->reset();
        }
    }
}

std::unique_ptr<QTranslator> load(const QString &file)
{
    auto t = std::make_unique<QTranslator>();
    if (!t->load(file) || !QCoreApplication::installTranslator(t.get()))
        return nullptr;
    return t;
}

} // namespace

UiLanguage readUiLanguage(const QSettings &settings)
{
    const QString v = settings.value(kKey).toString();
    if (v == QLatin1String("en"))
        return UiLanguage::English;
    if (v == QLatin1String("fr"))
        return UiLanguage::French;
    return UiLanguage::System;
}

void writeUiLanguage(QSettings &settings, UiLanguage language)
{
    switch (language) {
    case UiLanguage::System: settings.setValue(kKey, QStringLiteral("system")); break;
    case UiLanguage::English: settings.setValue(kKey, QStringLiteral("en")); break;
    case UiLanguage::French: settings.setValue(kKey, QStringLiteral("fr")); break;
    }
}

QString resolveLanguage(UiLanguage language, const QStringList &systemUiLanguages)
{
    switch (language) {
    case UiLanguage::English: return QStringLiteral("en");
    case UiLanguage::French: return QStringLiteral("fr");
    case UiLanguage::System: break;
    }
    if (!systemUiLanguages.isEmpty()) {
        const QString first = systemUiLanguages.constFirst().toLower();
        if (first == QLatin1String("fr") || first.startsWith(QLatin1String("fr-"))
            || first.startsWith(QLatin1String("fr_")))
            return QStringLiteral("fr");
    }
    return QStringLiteral("en");
}

bool installTranslators(const QString &code)
{
    removeTranslators();
    if (code == QLatin1String("fr"))
        g_qtTranslator = load(QStringLiteral(":/i18n/qtbase_fr.qm"));
    else if (code != QLatin1String("en"))
        return false;
    g_appTranslator = load(QStringLiteral(":/i18n/modbus_control_%1.qm").arg(code));
    if (!g_appTranslator)
        return false;
    g_current = code;
    return true;
}

QString currentLanguage()
{
    return g_current;
}

} // namespace mb
