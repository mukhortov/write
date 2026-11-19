#pragma once

#include <QHash>
#include <QJsonValue>
#include <QStringList>
#include <QTranslator>

// Write's interface in the Mac's language. Catalogs are flat JSON objects
// from English source text to translation, one per language (i18n/de.json,
// i18n/pt-BR.json), so the code keeps plain tr("Save"). A counted string,
// tr("%n Words", nullptr, count), maps to an object of plural forms keyed by
// CLDR category ({"one": "%n Wort", "other": "%n Wörter"}).
//
// A language counts only once its catalog covers every string en.json lists,
// so the interface is never half translated. Among those, macOS picks the
// language as it picks any app's localization: the app's own language in
// System Settings first, then the Mac's languages in order, then English.
class Translator : public QTranslator {
    Q_OBJECT

public:
    // `preferred` stands in for the Mac's languages, most preferred first.
    explicit Translator(const QString &catalogDirectory = QStringLiteral(":/i18n"),
                        const QStringList &preferred = {}, QObject *parent = nullptr);
    ~Translator() override;

    QString language() const { return m_language; }
    bool isEmpty() const override;
    QString translate(const char *context, const char *sourceText, const char *disambiguation = nullptr,
                      int n = -1) const override;

    // The strings en.json lists that the catalogs for `language` leave out.
    static QStringList missingStrings(const QString &catalogDirectory, const QString &language);
    // The languages whose catalogs cover every string, English first.
    static QStringList completeLanguages(const QString &catalogDirectory);
    // The one of `available` macOS shows for `preferred`, or for the Mac's
    // languages when `preferred` is empty; English when none fits.
    static QString preferredLanguage(const QStringList &available, const QStringList &preferred = {});
    static QString pluralCategory(const QString &language, long long count);

private:
    // A translation, and the language whose catalog it came from, so a
    // regional language falling back to its base (pt-BR to pt) takes the
    // base's plural rules.
    struct Entry {
        QJsonValue value;
        QString language;
    };

    QString m_language;
    // Filled once, before the translator is installed, and only read after,
    // so worker threads can translate without a lock.
    QHash<QString, Entry> m_strings;
};
