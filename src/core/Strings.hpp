#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace eduvm {

// Base error type for everything EduVM reports to the user.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::string trim(std::string_view s);
std::string toLower(std::string_view s);
std::vector<std::string> split(std::string_view s, char sep);
std::string join(const std::vector<std::string>& parts, std::string_view sep);
bool startsWith(std::string_view s, std::string_view prefix);
bool endsWith(std::string_view s, std::string_view suffix);

std::string base64Decode(std::string_view in);

// UTC timestamp, e.g. 2026-10-19T21:18:00Z
std::string nowIso8601();

// Compare dotted versions ("1.2.10" > "1.2.9"); a leading 'v' is ignored.
// Returns <0, 0, >0 like strcmp.
int compareVersions(std::string_view a, std::string_view b);

// Path <-> UTF-8 without locale surprises on Windows.
std::string pathToUtf8(const std::filesystem::path& p);
std::filesystem::path utf8ToPath(std::string_view s);

std::optional<std::string> getEnv(const char* name);

#ifdef _WIN32
std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view wide);
#endif

}  // namespace eduvm
