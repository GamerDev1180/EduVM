#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace eduvm::app {

// Native dialogs and shell integration. Filters use the form
// "ISO images|*.iso|All files|*.*".
void setDialogOwner(void* nativeWindow);
std::optional<std::string> openFileDialog(const std::string& title, const std::string& filter);
std::optional<std::string> saveFileDialog(const std::string& title, const std::string& defaultName, const std::string& filter);
void openUrl(const std::string& url);
void openFolder(const std::filesystem::path& dir);
std::filesystem::path executableDir();

}  // namespace eduvm::app
