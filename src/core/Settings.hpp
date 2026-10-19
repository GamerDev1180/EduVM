#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace eduvm {

// User/installation settings (settings.json). Secrets are never stored here:
// the API key is read from the environment variable named by apiKeyEnv.
struct Settings {
    std::string qemuDir;      // empty = search PATH and default install dirs
    std::string studentName;  // printed on progress reports

    bool aiEnabled = true;
    std::string aiModel = "claude-opus-5-5";
    std::string aiEffort = "low";      // tutoring is conversational; low keeps answers fast
    std::string tutorMode = "coach";   // coach | explain  (labs may lock this)
    std::string apiKeyEnv = "ANTHROPIC_API_KEY";
    // Schools can point this at their own proxy that holds the key, so
    // students never need a personal key. Set apiKeyEnv to "" in that case.
    std::string aiBaseUrl = "https://api.anthropic.com";

    std::string updateUrl = "https://api.github.com/repos/eduvm/eduvm/releases/latest";
    std::string labCatalogUrl;  // JSON list of labs published by the instructor

    static Settings load();
    void save() const;

    // Generic access for `eduvm config get/set`.
    std::string get(const std::string& key) const;
    void set(const std::string& key, const std::string& value);
    static std::vector<std::string> keys();
};

void to_json(nlohmann::json& j, const Settings& s);
void from_json(const nlohmann::json& j, Settings& s);

}  // namespace eduvm
