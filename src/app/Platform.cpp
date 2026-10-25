#include "app/Platform.hpp"

#include <cstdlib>
#include <vector>

#include "core/Process.hpp"
#include "core/Strings.hpp"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#else
#include <unistd.h>

#include <climits>
#endif

namespace fs = std::filesystem;

namespace eduvm::app {

namespace {
void* g_owner = nullptr;
}

void setDialogOwner(void* nativeWindow) { g_owner = nativeWindow; }

#ifdef _WIN32

// "Name|*.ext|Name2|*.*" -> "Name\0*.ext\0Name2\0*.*\0\0"
static std::wstring win32Filter(const std::string& filter) {
    std::wstring f = widen(filter);
    for (auto& c : f)
        if (c == L'|') c = L'\0';
    f.push_back(L'\0');
    f.push_back(L'\0');
    return f;
}

std::optional<std::string> openFileDialog(const std::string& title, const std::string& filter) {
    std::wstring wtitle = widen(title), wfilter = win32Filter(filter);
    wchar_t file[MAX_PATH * 4] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(g_owner);
    ofn.lpstrFilter = wfilter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file));
    ofn.lpstrTitle = wtitle.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return std::nullopt;
    return narrow(file);
}

std::optional<std::string> saveFileDialog(const std::string& title, const std::string& defaultName, const std::string& filter) {
    std::wstring wtitle = widen(title), wfilter = win32Filter(filter);
    wchar_t file[MAX_PATH * 4] = {};
    std::wstring def = widen(defaultName);
    def.copy(file, std::min(def.size(), std::size(file) - 1));
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(g_owner);
    ofn.lpstrFilter = wfilter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file));
    ofn.lpstrTitle = wtitle.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return std::nullopt;
    return narrow(file);
}

void openUrl(const std::string& url) { ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

void openFolder(const fs::path& dir) { ShellExecuteW(nullptr, L"open", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

fs::path executableDir() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(std::wstring(buf, n)).parent_path();
}

#else

// zenity is available on most Linux desktops; macOS users can type paths.
static std::optional<std::string> zenity(std::vector<std::string> args) {
    args.insert(args.begin(), "zenity");
    try {
        auto r = runAndCapture(args);
        if (r.exitCode != 0) return std::nullopt;
        std::string path = trim(r.output);
        return path.empty() ? std::nullopt : std::optional<std::string>(path);
    } catch (const Error&) {
        return std::nullopt;
    }
}

static std::string zenityFilter(const std::string& filter) {
    auto parts = split(filter, '|');
    std::string patterns;
    for (size_t i = 1; i < parts.size(); i += 2) patterns += (patterns.empty() ? "" : " ") + parts[i];
    for (auto& c : patterns)
        if (c == ';') c = ' ';  // Windows-style "*.a;*.b" -> zenity "*.a *.b"
    return patterns;
}

std::optional<std::string> openFileDialog(const std::string& title, const std::string& filter) {
    return zenity({"--file-selection", "--title=" + title, "--file-filter=" + zenityFilter(filter)});
}

std::optional<std::string> saveFileDialog(const std::string& title, const std::string& defaultName, const std::string&) {
    return zenity({"--file-selection", "--save", "--confirm-overwrite", "--title=" + title, "--filename=" + defaultName});
}

static void launch(const std::string& target) {
#ifdef __APPLE__
    spawnDetached({"open", target}, "/dev/null");
#else
    spawnDetached({"xdg-open", target}, "/dev/null");
#endif
}

void openUrl(const std::string& url) { launch(url); }
void openFolder(const fs::path& dir) { launch(dir.string()); }

fs::path executableDir() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf));
    if (n > 0) return fs::path(std::string(buf, static_cast<size_t>(n))).parent_path();
    return fs::current_path();
}

#endif

}  // namespace eduvm::app
