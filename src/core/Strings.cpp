#include "core/Strings.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#endif

namespace eduvm {

std::string trim(std::string_view s) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && isSpace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && isSpace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

std::string toLower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            parts.emplace_back(s.substr(start));
            break;
        }
        parts.emplace_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

std::string nowIso8601() {
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

int compareVersions(std::string_view a, std::string_view b) {
    if (!a.empty() && (a[0] == 'v' || a[0] == 'V')) a.remove_prefix(1);
    if (!b.empty() && (b[0] == 'v' || b[0] == 'V')) b.remove_prefix(1);
    // Ignore pre-release/build suffixes ("1.2.0-beta" compares as 1.2.0).
    auto core = [](std::string_view v) { return v.substr(0, v.find_first_of("-+")); };
    auto pa = split(core(a), '.');
    auto pb = split(core(b), '.');
    size_t n = std::max(pa.size(), pb.size());
    for (size_t i = 0; i < n; ++i) {
        long va = i < pa.size() ? std::strtol(pa[i].c_str(), nullptr, 10) : 0;
        long vb = i < pb.size() ? std::strtol(pb[i].c_str(), nullptr, 10) : 0;
        if (va != vb) return va < vb ? -1 : 1;
    }
    return 0;
}

std::string pathToUtf8(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

std::filesystem::path utf8ToPath(std::string_view s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

std::optional<std::string> getEnv(const char* name) {
#ifdef _WIN32
    std::wstring wname = widen(name);
    DWORD size = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
    if (size == 0) return std::nullopt;
    std::wstring value(size, L'\0');
    DWORD written = GetEnvironmentVariableW(wname.c_str(), value.data(), size);
    value.resize(written);
    if (value.empty()) return std::nullopt;
    return narrow(value);
#else
    const char* v = std::getenv(name);
    if (!v || !*v) return std::nullopt;
    return std::string(v);
#endif
}

#ifdef _WIN32
std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), len);
    return out;
}

std::string narrow(std::wstring_view wide) {
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                  nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), len, nullptr,
                        nullptr);
    return out;
}
#endif

}  // namespace eduvm
