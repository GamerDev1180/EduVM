#include "core/Settings.hpp"

#include <filesystem>

#include "core/Paths.hpp"

using nlohmann::json;

namespace eduvm {

void to_json(json& j, const Settings& s) {
    j = {{"qemuDir", s.qemuDir}, {"updateUrl", s.updateUrl}, {"checkForUpdates", s.checkForUpdates}};
}

void from_json(const json& j, Settings& s) {
    Settings d;
    s.qemuDir = j.value("qemuDir", d.qemuDir);
    s.updateUrl = j.value("updateUrl", d.updateUrl);
    s.checkForUpdates = j.value("checkForUpdates", d.checkForUpdates);
}

Settings Settings::load() {
    auto file = settingsFile();
    if (!std::filesystem::exists(file)) return Settings{};
    return json::parse(readFile(file)).get<Settings>();
}

void Settings::save() const { writeFileAtomic(settingsFile(), json(*this).dump(2)); }

}  // namespace eduvm
