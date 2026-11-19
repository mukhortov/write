#include "translator.h"

#include <CoreFoundation/CoreFoundation.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace {

QJsonObject readCatalog(const QString &directory, const QString &language) {
    QFile file(directory + QLatin1Char('/') + language + QStringLiteral(".json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool translates(const QJsonValue &value) {
    return (value.isString() && !value.toString().isEmpty()) || value.isObject();
}

// English first (it carries the plural forms of counted strings), then the
// base language and the regional catalog over it (pt, then pt-BR).
QStringList catalogChain(const QString &language) {
    QStringList codes{QStringLiteral("en"), language.section(QLatin1Char('-'), 0, 0), language};
    codes.removeDuplicates();
    return codes;
}

QStringList missingFrom(const QString &directory, const QString &language, const QJsonObject &english) {
    // English stands in only for English.
    QStringList codes = catalogChain(language);
    if (language.section(QLatin1Char('-'), 0, 0) != QLatin1String("en"))
        codes.removeAll(QStringLiteral("en"));
    QList<QJsonObject> catalogs;
    for (const QString &code : std::as_const(codes))
        catalogs << readCatalog(directory, code);
    QStringList missing;
    for (auto it = english.begin(); it != english.end(); ++it) {
        if (std::none_of(catalogs.cbegin(), catalogs.cend(),
                         [&](const QJsonObject &catalog) { return translates(catalog.value(it.key())); }))
            missing << it.key();
    }
    return missing;
}

CFArrayRef toCFArray(const QStringList &strings) {
    CFMutableArrayRef array = CFArrayCreateMutable(kCFAllocatorDefault, strings.size(), &kCFTypeArrayCallBacks);
    for (const QString &string : strings) {
        CFStringRef item = string.toCFString();
        CFArrayAppendValue(array, item);
        CFRelease(item);
    }
    return array;
}

}

Translator::Translator(const QString &catalogDirectory, const QStringList &preferred, QObject *parent)
    : QTranslator(parent),
      m_language(preferredLanguage(completeLanguages(catalogDirectory), preferred)) {
    for (const QString &code : catalogChain(m_language)) {
        const QJsonObject catalog = readCatalog(catalogDirectory, code);
        for (auto it = catalog.begin(); it != catalog.end(); ++it)
            if (translates(it.value()))
                m_strings.insert(it.key(), {it.value(), code});
    }
}

// Unregistered before the catalog goes, since a worker thread can still be
// translating through QCoreApplication while the app shuts down; one
// outliving the application (a test's) has nothing to unregister from.
Translator::~Translator() {
    if (QCoreApplication::instance())
        QCoreApplication::removeTranslator(this);
}

bool Translator::isEmpty() const {
    return m_strings.isEmpty();
}

QStringList Translator::missingStrings(const QString &catalogDirectory, const QString &language) {
    return missingFrom(catalogDirectory, language, readCatalog(catalogDirectory, QStringLiteral("en")));
}

QStringList Translator::completeLanguages(const QString &catalogDirectory) {
    const QJsonObject english = readCatalog(catalogDirectory, QStringLiteral("en"));
    QStringList languages{QStringLiteral("en")};
    const QDir directory(catalogDirectory);
    for (const QFileInfo &file : directory.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        const QString code = file.completeBaseName();
        if (code != QLatin1String("en") && missingFrom(catalogDirectory, code, english).isEmpty())
            languages << code;
    }
    return languages;
}

// CoreFoundation matches languages as macOS does for any app: de-AT finds
// de, zh-Hans-CN finds zh-CN, nb finds no, and en-GB ahead of German keeps
// English. With no preferences given it reads the app's own, which System
// Settings can set apart from the Mac's.
QString Translator::preferredLanguage(const QStringList &available, const QStringList &preferred) {
    QStringList offered{QStringLiteral("en")};
    offered += available;
    offered.removeDuplicates();
    CFArrayRef offeredArray = toCFArray(offered);
    CFArrayRef preferredArray = preferred.isEmpty() ? nullptr : toCFArray(preferred);
    CFArrayRef chosen = CFBundleCopyLocalizationsForPreferences(offeredArray, preferredArray);
    QString language;
    if (chosen && CFArrayGetCount(chosen) > 0)
        language = QString::fromCFString(static_cast<CFStringRef>(CFArrayGetValueAtIndex(chosen, 0)));
    if (chosen)
        CFRelease(chosen);
    if (preferredArray)
        CFRelease(preferredArray);
    CFRelease(offeredArray);
    return offered.contains(language) ? language : QStringLiteral("en");
}

// CLDR cardinal plural categories for whole counts, for the catalogs'
// languages.
QString Translator::pluralCategory(const QString &language, long long count) {
    const long long n = count < 0 ? -count : count, n10 = n % 10, n100 = n % 100;
    const bool millions = n != 0 && n % 1000000 == 0;
    const bool teens = n100 >= 12 && n100 <= 14;
    const QString code = language;
    auto in = [&](std::initializer_list<const char *> codes) {
        for (const char *c : codes) if (code == QLatin1String(c)) return true;
        return false;
    };
    if (in({"ja", "ko", "zh-CN", "zh-TW", "th", "vi", "id", "ms"})) return "other";
    if (in({"hi", "bn", "fa", "si"})) return n <= 1 ? "one" : "other";
    if (in({"fr", "pt-BR"})) return n <= 1 ? "one" : millions ? "many" : "other";
    if (in({"ca", "es-MX", "it", "pt"})) return n == 1 ? "one" : millions ? "many" : "other";
    if (code == "fil") return (n10 == 4 || n10 == 6 || n10 == 9) ? "other" : "one";
    if (code == "is") return n10 == 1 && n100 != 11 ? "one" : "other";
    if (in({"cs", "sk"})) return n == 1 ? "one" : n >= 2 && n <= 4 ? "few" : "other";
    if (code == "pl") return n == 1 ? "one" : n10 >= 2 && n10 <= 4 && !teens ? "few" : "many";
    if (in({"ru", "uk"})) return n10 == 1 && n100 != 11 ? "one" : n10 >= 2 && n10 <= 4 && !teens ? "few" : "many";
    if (in({"hr", "sr"})) return n10 == 1 && n100 != 11 ? "one" : n10 >= 2 && n10 <= 4 && !teens ? "few" : "other";
    if (code == "sl") return n100 == 1 ? "one" : n100 == 2 ? "two" : n100 >= 3 && n100 <= 4 ? "few" : "other";
    if (code == "lt") return n100 >= 11 && n100 <= 19 ? "other" : n10 == 1 ? "one" : n10 >= 2 ? "few" : "other";
    if (code == "lv") return n10 == 0 || (n100 >= 11 && n100 <= 19) ? "zero" : n10 == 1 ? "one" : "other";
    if (code == "ro") return n == 1 ? "one" : n == 0 || (n100 >= 1 && n100 <= 19) ? "few" : "other";
    if (code == "ar") {
        if (n <= 2) return n == 0 ? "zero" : n == 1 ? "one" : "two";
        return n100 >= 3 && n100 <= 10 ? "few" : n100 >= 11 ? "many" : "other";
    }
    if (code == "he") return n == 1 ? "one" : n == 2 ? "two" : "other";
    if (code == "ga") return n == 1 ? "one" : n == 2 ? "two" : n >= 3 && n <= 6 ? "few" : n >= 7 && n <= 10 ? "many" : "other";
    return n == 1 ? "one" : "other";
}

// The context is left out: a string reads the same wherever it is shown, and
// Qt's own requests for one (a standard button's "Save") find it too.
QString Translator::translate(const char *, const char *sourceText, const char *, int n) const {
    // A null string tells Qt there is no translation, so it shows the English.
    const Entry entry = m_strings.value(QString::fromUtf8(sourceText));
    const QJsonValue &value = entry.value;
    if (value.isString())
        return value.toString();
    if (value.isObject()) {
        const QJsonObject forms = value.toObject();
        const QString form = forms.value(n < 0 ? "other" : pluralCategory(entry.language, n)).toString();
        return form.isEmpty() ? forms.value("other").toString() : form;
    }
    return {};
}
