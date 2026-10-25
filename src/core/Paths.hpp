#pragma once

#include <filesystem>
#include <string>

namespace eduvm {

// Root folder for all EduVM state. Resolution order:
//   1. $EDUVM_HOME
//   2. Windows: %APPDATA%\EduVM
//   3. $XDG_DATA_HOME/eduvm, else ~/.local/share/eduvm
std::filesystem::path dataDir();

std::filesystem::path vmsDir();       // one sub-folder per VM
std::filesystem::path settingsFile();

// Reads/writes a whole file. Writes are atomic (temp file + rename) so a crash
// never leaves a half-written vm.json behind.
std::string readFile(const std::filesystem::path& p);
void writeFileAtomic(const std::filesystem::path& p, const std::string& content);

}  // namespace eduvm
