#include "localization/Localization.hpp"

#include "core/Strings.hpp"
#include "core/Sysfs.hpp"
#include "gpu/Codecs.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlerror.h>
#include <unistd.h>

namespace gpu_report {

namespace {

std::string normalizeLocaleId(std::string value)
{
    value = toLowerAscii(trim(value));
    std::replace(value.begin(), value.end(), '-', '_');
    const std::size_t dot = value.find('.');
    if (dot != std::string::npos) {
        value = value.substr(0, dot);
    }
    const std::size_t at = value.find('@');
    if (at != std::string::npos) {
        value = value.substr(0, at);
    }
    return value;
}

Language languageFromId(const std::string& id)
{
    if (id.empty() || id == "c" || id == "posix") {
        return Language::English;
    }
    if (id == "english" || id == "en" || startsWith(id, "en_")) {
        return Language::English;
    }
    if (id == "russian" || id == "ru" || startsWith(id, "ru_")) {
        return Language::Russian;
    }
    if (id == "japanese" || id == "ja" || startsWith(id, "ja_")) {
        return Language::Japanese;
    }
    if (id == "korean" || id == "ko" || startsWith(id, "ko_")) {
        return Language::Korean;
    }
    if (id == "chinese" || id == "zh" || startsWith(id, "zh_")
        || id == "zh_cn" || id == "zh_tw" || id == "zh_hk") {
        return Language::Chinese;
    }
    if (id == "spanish" || id == "es" || startsWith(id, "es_")) {
        return Language::Spanish;
    }
    return Language::English;
}

bool isSupportedId(const std::string& id)
{
    return id == "english" || id == "en" || startsWith(id, "en_")
        || id == "russian" || id == "ru" || startsWith(id, "ru_")
        || id == "japanese" || id == "ja" || startsWith(id, "ja_")
        || id == "korean" || id == "ko" || startsWith(id, "ko_")
        || id == "chinese" || id == "zh" || startsWith(id, "zh_")
        || id == "spanish" || id == "es" || startsWith(id, "es_");
}

std::string executableDirectory()
{
    char buffer[4096] = {0};
    const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return {};
    }
    buffer[length] = '\0';
    return std::filesystem::path(buffer).parent_path().string();
}

void addUniqueDirectory(std::vector<std::string>& directories, const std::string& directory)
{
    if (directory.empty()) {
        return;
    }
    std::error_code error;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(directory, error);
    const std::string value = error ? directory : normalized.string();
    if (std::find(directories.begin(), directories.end(), value) == directories.end()) {
        directories.push_back(value);
    }
}

bool collectMessages(xmlNodePtr parent, std::map<std::string, std::string>& messages)
{
    for (xmlNodePtr node = parent; node != nullptr; node = node->next) {
        if (node->type != XML_ELEMENT_NODE) {
            continue;
        }
        const std::string name(reinterpret_cast<const char*>(node->name));
        if (name == "message" || name == "string") {
            xmlChar* key = xmlGetProp(node, BAD_CAST "key");
            xmlChar* value = xmlNodeGetContent(node);
            if (key != nullptr && value != nullptr) {
                messages[reinterpret_cast<const char*>(key)] =
                    trim(reinterpret_cast<const char*>(value));
            }
            if (key != nullptr) {
                xmlFree(key);
            }
            if (value != nullptr) {
                xmlFree(value);
            }
        } else if (name == "messages" || name == "strings") {
            collectMessages(node->children, messages);
        }
    }
    return true;
}

bool loadLocaleFile(const std::string& path, std::map<std::string, std::string>& messages,
                    std::string* error)
{
    xmlResetLastError();
    xmlDocPtr document = xmlReadFile(path.c_str(), nullptr,
                                     XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (document == nullptr) {
        if (error != nullptr) {
            const xmlError* xmlError = xmlGetLastError();
            *error = xmlError != nullptr && xmlError->message != nullptr
                ? trim(xmlError->message)
                : "could not parse locale file";
        }
        return false;
    }

    xmlNodePtr root = xmlDocGetRootElement(document);
    if (root == nullptr
        || xmlStrcmp(root->name, BAD_CAST "locale") != 0) {
        if (error != nullptr) {
            *error = "root element is not <locale>";
        }
        xmlFreeDoc(document);
        return false;
    }

    collectMessages(root->children, messages);
    xmlFreeDoc(document);
    return true;
}

} // namespace

std::string languageCode(Language language)
{
    switch (language) {
    case Language::Russian:
        return "ru";
    case Language::Japanese:
        return "ja";
    case Language::Korean:
        return "ko";
    case Language::Chinese:
        return "zh";
    case Language::Spanish:
        return "es";
    case Language::English:
    default:
        return "en";
    }
}

std::string languageNameKey(Language language)
{
    return "lang." + languageCode(language);
}

LanguageResolution resolveLanguage(const std::string& explicitValue,
                                   const std::string& lcAll,
                                   const std::string& lcMessages,
                                   const std::string& lang)
{
    LanguageResolution resolution;
    if (!trim(explicitValue).empty()) {
        resolution.requested = trim(explicitValue);
        const std::string normalized = normalizeLocaleId(explicitValue);
        if (isSupportedId(normalized)) {
            resolution.language = languageFromId(normalized);
            resolution.invalidExplicit = false;
        } else {
            resolution.language = Language::English;
            resolution.invalidExplicit = true;
        }
        return resolution;
    }

    std::string systemLocale;
    if (!trim(lcAll).empty()) {
        systemLocale = lcAll;
    } else if (!trim(lcMessages).empty()) {
        systemLocale = lcMessages;
    } else if (!trim(lang).empty()) {
        systemLocale = lang;
    }

    resolution.requested = systemLocale;
    resolution.language = languageFromId(normalizeLocaleId(systemLocale));
    return resolution;
}

LanguageResolution resolveLanguageFromEnvironment(const std::string& explicitValue)
{
    const char* lcAll = std::getenv("LC_ALL");
    const char* lcMessages = std::getenv("LC_MESSAGES");
    const char* lang = std::getenv("LANG");
    return resolveLanguage(explicitValue,
                           lcAll != nullptr ? lcAll : "",
                           lcMessages != nullptr ? lcMessages : "",
                           lang != nullptr ? lang : "");
}

std::vector<std::string> localeSearchDirectories()
{
    std::vector<std::string> directories;
    const char* environment = std::getenv("GPU_REPORT_LOCALE_DIR");
    if (environment != nullptr && *environment != '\0') {
        addUniqueDirectory(directories, environment);
    }

    const std::string executableDir = executableDirectory();
    if (!executableDir.empty()) {
        addUniqueDirectory(directories, executableDir + "/../share/gpu-report/locales");
        addUniqueDirectory(directories, executableDir + "/locales");
        addUniqueDirectory(directories, executableDir + "/../locales");
    }

    std::error_code error;
    const std::filesystem::path current = std::filesystem::current_path(error);
    if (!error) {
        addUniqueDirectory(directories, (current / "locales").string());
    }

    addUniqueDirectory(directories, "/usr/local/share/gpu-report/locales");
    addUniqueDirectory(directories, "/usr/share/gpu-report/locales");

#ifdef GPU_REPORT_BUILD_LOCALES_DIR
    addUniqueDirectory(directories, GPU_REPORT_BUILD_LOCALES_DIR);
#endif

    return directories;
}

bool Localization::load(Language language)
{
    language_ = language;
    base_.clear();
    overlay_.clear();
    warnings_.clear();
    sourceDirectory_.clear();
    loaded_ = false;

    const std::vector<std::string> directories = localeSearchDirectories();
    std::string englishDirectory;
    std::string targetDirectory;

    for (const std::string& directory : directories) {
        if (englishDirectory.empty()
            && pathExists(std::filesystem::path(directory) / "en.xml")) {
            englishDirectory = directory;
        }
        if (targetDirectory.empty()
            && pathExists(std::filesystem::path(directory) / (languageCode(language) + ".xml"))) {
            targetDirectory = directory;
        }
    }

    if (englishDirectory.empty() && targetDirectory.empty()) {
        warnings_.push_back("no locale directory found");
        return false;
    }

    // English and the target language may live in different search
    // directories (for example a system install with a user override), so load
    // each file from the directory where it was actually found.
    std::string localError;
    bool loadedAny = false;
    if (!englishDirectory.empty()) {
        const std::string englishPath = englishDirectory + "/en.xml";
        if (pathExists(englishPath) && loadLocaleFile(englishPath, base_, &localError)) {
            loadedAny = true;
        } else if (!localError.empty()) {
            warnings_.push_back(englishPath + ": " + localError);
        }
    }

    if (language != Language::English) {
        const std::string directory = !targetDirectory.empty()
            ? targetDirectory
            : englishDirectory;
        const std::string targetPath = directory + "/" + languageCode(language) + ".xml";
        if (pathExists(targetPath) && loadLocaleFile(targetPath, overlay_, &localError)) {
            loadedAny = true;
        } else {
            warnings_.push_back("locale file not found or invalid: " + targetPath);
            overlay_.clear();
        }
    } else if (base_.empty() && !targetDirectory.empty()) {
        const std::string targetPath = targetDirectory + "/en.xml";
        if (loadLocaleFile(targetPath, base_, &localError)) {
            loadedAny = true;
        }
    }

    if (!loadedAny) {
        warnings_.push_back("no locale files could be loaded");
        return false;
    }

    sourceDirectory_ = !englishDirectory.empty() ? englishDirectory : targetDirectory;
    language_ = language;
    loaded_ = true;
    return true;
}

bool Localization::loadFromDirectory(Language language, const std::string& directory,
                                     std::string* error)
{
    language_ = language;
    base_.clear();
    overlay_.clear();
    warnings_.clear();
    sourceDirectory_.clear();
    loaded_ = false;

    if (!loadPair(language, directory, error)) {
        return false;
    }
    return loaded_;
}

bool Localization::loadPair(Language language, const std::string& directory, std::string* error)
{
    const std::string englishPath = directory + "/en.xml";
    const std::string targetPath = directory + "/" + languageCode(language) + ".xml";

    std::string localError;
    bool loadedAny = false;

    if (pathExists(englishPath)) {
        if (loadLocaleFile(englishPath, base_, &localError)) {
            loadedAny = true;
        } else if (error != nullptr) {
            *error = englishPath + ": " + localError;
        }
    }

    if (language != Language::English) {
        if (pathExists(targetPath)) {
            if (loadLocaleFile(targetPath, overlay_, &localError)) {
                loadedAny = true;
            } else {
                warnings_.push_back(targetPath + ": " + localError);
                overlay_.clear();
            }
        } else {
            warnings_.push_back("locale file not found: " + targetPath);
        }
    } else if (!loadedAny && pathExists(targetPath)) {
        if (loadLocaleFile(targetPath, base_, &localError)) {
            loadedAny = true;
        }
    }

    if (loadedAny) {
        sourceDirectory_ = directory;
        loaded_ = true;
        return true;
    }

    if (error != nullptr && error->empty()) {
        *error = "no locale files found in " + directory;
    }
    return false;
}

std::string Localization::tr(const std::string& key) const
{
    const auto overlay = overlay_.find(key);
    if (overlay != overlay_.end()) {
        return overlay->second;
    }
    const auto base = base_.find(key);
    if (base != base_.end()) {
        return base->second;
    }
    return key;
}

std::string Localization::tr(const std::string& key,
                             const std::vector<std::string>& arguments) const
{
    std::string value = tr(key);
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        value = replaceAll(value, "{" + std::to_string(i) + "}", arguments[i]);
    }
    return value;
}

std::string Localization::tr(const std::string& key,
                             std::initializer_list<std::string> arguments) const
{
    return tr(key, std::vector<std::string>(arguments));
}

std::string Localization::codecComment(const std::string& codec) const
{
    const std::string key = codecs::commentKey(codec);
    if (key.empty()) {
        return {};
    }
    return tr(key);
}

} // namespace gpu_report
