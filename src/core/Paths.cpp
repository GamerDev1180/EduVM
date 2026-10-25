#include "core/Paths.hpp"

#include <fstream>
#include <sstream>

#include "core/Strings.hpp"

namespace fs = std::filesystem;

namespace eduvm {

static fs::path ensureDir(const fs::path& p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    if (ec) throw Error("Cannot create directory " + pathToUtf8(p) + ": " + ec.message());
    return p;
}

fs::path dataDir() {
    if (auto home = getEnv("EDUVM_HOME")) return ensureDir(utf8ToPath(*home));
#ifdef _WIN32
    if (auto appData = getEnv("APPDATA")) return ensureDir(utf8ToPath(*appData) / "EduVM");
#else
    if (auto xdg = getEnv("XDG_DATA_HOME")) return ensureDir(utf8ToPath(*xdg) / "eduvm");
    if (auto home = getEnv("HOME")) return ensureDir(utf8ToPath(*home) / ".local" / "share" / "eduvm");
#endif
    return ensureDir(fs::current_path() / ".eduvm");
}

fs::path vmsDir() { return ensureDir(dataDir() / "vms"); }
fs::path settingsFile() { return dataDir() / "settings.json"; }

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw Error("Cannot read " + pathToUtf8(p));
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void writeFileAtomic(const fs::path& p, const std::string& content) {
    if (p.has_parent_path()) ensureDir(p.parent_path());
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw Error("Cannot write " + pathToUtf8(tmp));
        out << content;
        if (!out) throw Error("Write failed for " + pathToUtf8(tmp));
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) throw Error("Cannot replace " + pathToUtf8(p) + ": " + ec.message());
}

}  // namespace eduvm
