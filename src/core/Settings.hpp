#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace eduvm {

// Application settings (settings.json in the data folder).
struct Settings {
    std::string qemuDir;  // empty = search PATH and default install dirs
    std::string updateUrl = "https://api.github.com/repos/eduvm/eduvm/releases/latest";
    bool checkForUpdates = true;

    static Settings load();
    void save() const;
};

void to_json(nlohmann::json& j, const Settings& s);
void from_json(const nlohmann::json& j, Settings& s);

}  // namespace eduvm
