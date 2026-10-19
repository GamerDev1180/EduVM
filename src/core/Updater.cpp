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
    if (settings.updateUrl.empty()) throw Error("No update URL configured (eduvm config set updateUrl <url>)");
    HttpResponse r = httpGet(settings.updateUrl, {{"accept", "application/vnd.github+json"}});
    if (r.status == 404) throw Error("No releases published yet at " + settings.updateUrl);
    if (r.status != 200) throw Error("Update check failed with HTTP " + std::to_string(r.status));
    return parseReleaseJson(r.body, kVersion);
}

std::vector<CatalogEntry> parseLabCatalog(const std::string& body) {
    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.contains("labs")) throw Error("Lab catalog is not valid (expected a 'labs' array)");
    std::vector<CatalogEntry> out;
    for (auto& e : j["labs"]) {
        CatalogEntry c;
        c.id = e.at("id").get<std::string>();
        c.title = e.value("title", c.id);
        c.version = e.value("version", std::string("1.0.0"));
        c.level = e.value("level", std::string());
        c.url = e.at("url").get<std::string>();
        out.push_back(c);
    }
    return out;
}

std::vector<CatalogEntry> fetchLabCatalog(const std::string& url) {
    if (url.empty()) throw Error("No lab catalog configured. Ask your instructor for the URL, then run:\n"
                                 "  eduvm config set labCatalogUrl <url>");
    HttpResponse r = httpGet(url);
    if (r.status != 200) throw Error("Could not fetch lab catalog (HTTP " + std::to_string(r.status) + ")");
    return parseLabCatalog(r.body);
}

}  // namespace eduvm
