#include "TestHarness.hpp"

#include "gpu/Codecs.hpp"
#include "localization/Localization.hpp"

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <set>
#include <string>
#include <vector>

using namespace gpu_report;

namespace {

std::set<std::string> localeKeys(const std::string& path)
{
    std::set<std::string> keys;
    xmlDocPtr document = xmlReadFile(path.c_str(), nullptr, XML_PARSE_NONET);
    if (document == nullptr) {
        return keys;
    }
    xmlNodePtr root = xmlDocGetRootElement(document);
    for (xmlNodePtr node = root != nullptr ? root->children : nullptr;
         node != nullptr; node = node->next) {
        if (node->type != XML_ELEMENT_NODE) {
            continue;
        }
        const std::string name(reinterpret_cast<const char*>(node->name));
        if (name == "message" || name == "string") {
            xmlChar* key = xmlGetProp(node, BAD_CAST "key");
            if (key != nullptr) {
                keys.insert(reinterpret_cast<const char*>(key));
                xmlFree(key);
            }
        }
    }
    xmlFreeDoc(document);
    return keys;
}

std::string localePath(const std::string& code)
{
    return std::string(GPU_REPORT_BUILD_LOCALES_DIR) + "/" + code + ".xml";
}

} // namespace

int main()
{
    const std::vector<std::string> codes = {"en", "ru", "ja", "ko", "zh", "es"};
    const std::set<std::string> englishKeys = localeKeys(localePath("en"));
    CHECK(!englishKeys.empty());
    CHECK(englishKeys.count("app.title") == 1);
    CHECK(englishKeys.count("report.capabilities") == 1);
    CHECK(englishKeys.count("common.host") == 1);
    CHECK(englishKeys.count("report.sources") == 1);

    for (const std::string& code : codes) {
        const std::set<std::string> keys = localeKeys(localePath(code));
        CHECK_MSG(!keys.empty(), code);
        bool complete = true;
        for (const std::string& key : englishKeys) {
            if (keys.count(key) == 0) {
                complete = false;
                std::cerr << code << " is missing key " << key << "\n";
            }
        }
        CHECK_MSG(complete, code);
    }

    LanguageResolution explicitRussian = resolveLanguage("russian", "en_US.UTF-8", "", "");
    CHECK(explicitRussian.language == Language::Russian);
    CHECK(!explicitRussian.invalidExplicit);

    LanguageResolution shortCode = resolveLanguage("zh", "", "", "");
    CHECK(shortCode.language == Language::Chinese);

    LanguageResolution invalid = resolveLanguage("klingon", "", "", "");
    CHECK(invalid.invalidExplicit);
    CHECK(invalid.language == Language::English);

    LanguageResolution lcAll = resolveLanguage("", "ru_RU.UTF-8", "ja_JP.UTF-8", "ko_KR.UTF-8");
    CHECK(lcAll.language == Language::Russian);

    LanguageResolution lcMessages = resolveLanguage("", "", "ja_JP.UTF-8", "ko_KR.UTF-8");
    CHECK(lcMessages.language == Language::Japanese);

    LanguageResolution langFallback = resolveLanguage("", "", "", "ko_KR.UTF-8");
    CHECK(langFallback.language == Language::Korean);

    LanguageResolution cLocale = resolveLanguage("", "C", "", "");
    CHECK(cLocale.language == Language::English);

    LanguageResolution unsupported = resolveLanguage("", "de_DE.UTF-8", "", "");
    CHECK(unsupported.language == Language::English);

    const std::vector<std::pair<Language, std::string>> languages = {
        {Language::English, "en"},
        {Language::Russian, "ru"},
        {Language::Japanese, "ja"},
        {Language::Korean, "ko"},
        {Language::Chinese, "zh"},
        {Language::Spanish, "es"},
    };

    for (const auto& entry : languages) {
        CHECK(languageCode(entry.first) == entry.second);
        CHECK(languageNameKey(entry.first) == "lang." + entry.second);

        Localization localization;
        std::string error;
        const bool loaded = localization.loadFromDirectory(
            entry.first, GPU_REPORT_BUILD_LOCALES_DIR, &error);
        CHECK_MSG(loaded, entry.second + " " + error);
        CHECK(localization.loaded());
        CHECK(localization.tr("app.title") == "GPU Report");
        CHECK(localization.tr("common.yes") == "YES");
        CHECK(localization.tr("common.no") == "NO");
        CHECK(localization.tr("common.unknown_status") == "UNKNOWN");
        CHECK(localization.tr("common.gpu_counter", {"1", "2"}) == "GPU 1/2");
        CHECK(localization.tr("does.not.exist") == "does.not.exist");
        CHECK(!localization.tr("app.tagline").empty());
        CHECK(!localization.tr("help.description").empty());
        for (const std::string& codec : codecs::mandatoryCodecs()) {
            CHECK_MSG(!localization.codecComment(codec).empty(), codec);
        }
    }

    Localization russian;
    CHECK(russian.loadFromDirectory(Language::Russian, GPU_REPORT_BUILD_LOCALES_DIR));
    Localization english;
    CHECK(english.loadFromDirectory(Language::English, GPU_REPORT_BUILD_LOCALES_DIR));
    CHECK(russian.tr("app.tagline") != english.tr("app.tagline"));

    Localization defaults;
    const bool defaultsLoaded = defaults.load(Language::English);
    CHECK(defaultsLoaded);
    CHECK(defaults.loaded());

    xmlCleanupParser();
    return testharness::summary("localization");
}
