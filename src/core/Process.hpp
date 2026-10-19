#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace eduvm {

struct ProcessResult {
    int exitCode = -1;
    std::string output;  // stdout and stderr combined
};

// Runs argv[0] with the given arguments, waits, and captures its output.
ProcessResult runAndCapture(const std::vector<std::string>& argv);

// Starts a long-running process (a VM) that outlives the CLI. stdout/stderr go
// to logFile. Returns the OS process id.
long spawnDetached(const std::vector<std::string>& argv, const std::filesystem::path& logFile);

bool isProcessAlive(long pid);
bool killProcess(long pid);

// Searches PATH (and extraDirs first) for an executable. On Windows ".exe" is
// appended automatically.
std::optional<std::filesystem::path> findExecutable(const std::string& name,
                                                    const std::vector<std::filesystem::path>& extraDirs = {});

#ifdef _WIN32
// Exposed for tests: builds a CreateProcess command line with correct quoting.
std::string buildWindowsCommandLine(const std::vector<std::string>& argv);
#endif

}  // namespace eduvm
