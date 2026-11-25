#pragma once

#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace gpu_report {

enum class Language {
    English,
    Russian,
    Japanese,
    Korean,
    Chinese,
    Spanish
};

struct LanguageResolution {
    Language language = Language::English;
    bool invalidExplicit = false;
    std::string requested;
};

std::string languageCode(Language language);
std::string languageNameKey(Language language);
LanguageResolution resolveLanguage(const std::string& explicitValue,
                                   const std::string& lcAll,
                                   const std::string& lcMessages,
                                   const std::string& lang);
LanguageResolution resolveLanguageFromEnvironment(const std::string& explicitValue);

class Localization {
public:
    bool load(Language language);
    bool loadFromDirectory(Language language, const std::string& directory,
                           std::string* error = nullptr);

    bool loaded() const { return loaded_; }
    Language language() const { return language_; }
    const std::string& sourceDirectory() const { return sourceDirectory_; }
    const std::vector<std::string>& warnings() const { return warnings_; }

    std::string tr(const std::string& key) const;
    std::string tr(const std::string& key, const std::vector<std::string>& arguments) const;
    std::string tr(const std::string& key,
                   std::initializer_list<std::string> arguments) const;
    std::string codecComment(const std::string& codec) const;

private:
    bool loadPair(Language language, const std::string& directory, std::string* error);

    std::map<std::string, std::string> base_;
    std::map<std::string, std::string> overlay_;
    std::vector<std::string> warnings_;
    std::string sourceDirectory_;
    Language language_ = Language::English;
    bool loaded_ = false;
};

std::vector<std::string> localeSearchDirectories();

} // namespace gpu_report
