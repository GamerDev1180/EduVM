#pragma once

#include <string>
#include <vector>

#include "core/Settings.hpp"

namespace eduvm {

struct UpdateInfo {
    bool available = false;
    std::string currentVersion;
    std::string latestVersion;
    std::string downloadUrl;
    std::string releaseNotes;
};

// Reads a GitHub "latest release" JSON document (tag_name, html_url, body).
UpdateInfo checkForAppUpdate(const Settings& settings);
UpdateInfo parseReleaseJson(const std::string& body, const std::string& currentVersion);

struct CatalogEntry {
    std::string id;
    std::string title;
    std::string version;
    std::string level;
    std::string url;  // manifest URL, passed to LabLibrary::install
};

// Lab catalogs let instructors publish new and updated labs during the
// course without shipping a new EduVM build:
//   { "labs": [ { "id": ..., "title": ..., "version": ..., "url": ... } ] }
std::vector<CatalogEntry> fetchLabCatalog(const std::string& url);
std::vector<CatalogEntry> parseLabCatalog(const std::string& body);

}  // namespace eduvm
