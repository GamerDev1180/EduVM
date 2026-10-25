#pragma once

#include <string>

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

}  // namespace eduvm
