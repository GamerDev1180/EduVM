#include "core/Settings.hpp"

#include <filesystem>

#include "core/Paths.hpp"
#include "core/Strings.hpp"

using nlohmann::json;

namespace eduvm {

void to_json(json& j, const Settings& s) {
    j = {
        {"qemuDir", s.qemuDir},
        {"studentName", s.studentName},
        {"aiEnabled", s.aiEnabled},
        {"aiModel", s.aiModel},
        {"aiEffort", s.aiEffort},
        {"tutorMode", s.tutorMode},
        {"apiKeyEnv", s.apiKeyEnv},
        {"aiBaseUrl", s.aiBaseUrl},
        {"updateUrl", s.updateUrl},
        {"labCatalogUrl", s.labCatalogUrl},
    };
}

void from_json(const json& j, Settings& s) {
    Settings d;
    s.qemuDir = j.value("qemuDir", d.qemuDir);
    s.studentName = j.value("studentName", d.studentName);
    s.aiEnabled = j.value("aiEnabled", d.aiEnabled);
    s.aiModel = j.value("aiModel", d.aiModel);
    s.aiEffort = j.value("aiEffort", d.aiEffort);
    s.tutorMode = j.value("tutorMode", d.tutorMode);
    s.apiKeyEnv = j.value("apiKeyEnv", d.apiKeyEnv);
    s.aiBaseUrl = j.value("aiBaseUrl", d.aiBaseUrl);
    s.updateUrl = j.value("updateUrl", d.updateUrl);
    s.labCatalogUrl = j.value("labCatalogUrl", d.labCatalogUrl);
}

Settings Settings::load() {
    auto file = settingsFile();
    if (!std::filesystem::exists(file)) return Settings{};
    return json::parse(readFile(file)).get<Settings>();
}

void Settings::save() const { writeFileAtomic(settingsFile(), json(*this).dump(2)); }

std::vector<std::string> Settings::keys() {
    std::vector<std::string> out;
    const json defaults = Settings{};  // named: items() must not outlive a temporary
    for (auto& [k, v] : defaults.items()) out.push_back(k);
    return out;
}

std::string Settings::get(const std::string& key) const {
    json j = *this;
    if (!j.contains(key)) throw Error("Unknown setting '" + key + "'. Known: " + join(keys(), ", "));
    return j[key].is_string() ? j[key].get<std::string>() : j[key].dump();
}

void Settings::set(const std::string& key, const std::string& value) {
    json j = *this;
    if (!j.contains(key)) throw Error("Unknown setting '" + key + "'. Known: " + join(keys(), ", "));
    if (j[key].is_boolean()) {
        std::string v = toLower(value);
        if (v != "true" && v != "false") throw Error(key + " must be true or false");
        j[key] = (v == "true");
    } else {
        j[key] = value;
    }
    if (key == "tutorMode" && value != "coach" && value != "explain")
        throw Error("tutorMode must be 'coach' or 'explain'");
    *this = j.get<Settings>();
}

}  // namespace eduvm
