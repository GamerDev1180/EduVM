#include "core/Updater.hpp"

#include <nlohmann/json.hpp>

#include "core/Http.hpp"
#include "core/Strings.hpp"
#include "eduvm/Version.hpp"

using nlohmann::json;

namespace eduvm {

UpdateInfo parseReleaseJson(const std::string& body, const std::string& currentVersion) {
    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.contains("tag_name")) throw Error("Update server returned an unexpected response");
    UpdateInfo info;
    info.currentVersion = currentVersion;
    info.latestVersion = j["tag_name"].get<std::string>();
    if (!info.latestVersion.empty() && (info.latestVersion[0] == 'v' || info.latestVersion[0] == 'V'))
        info.latestVersion.erase(0, 1);
    info.downloadUrl = j.value("html_url", std::string());
    info.releaseNotes = j.value("body", std::string());
    info.available = compareVersions(info.latestVersion, currentVersion) > 0;
    return info;
}

UpdateInfo checkForAppUpdate(const Settings& settings) {
    if (settings.updateUrl.empty()) throw Error("No update URL configured");
    HttpResponse r = httpGet(settings.updateUrl, {{"accept", "application/vnd.github+json"}});
    if (r.status == 404) throw Error("No releases published yet at " + settings.updateUrl);
    if (r.status != 200) throw Error("Update check failed with HTTP " + std::to_string(r.status));
    return parseReleaseJson(r.body, kVersion);
}

}  // namespace eduvm
