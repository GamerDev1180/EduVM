#include "core/OsDetect.hpp"

#include <regex>

#include "core/Strings.hpp"

namespace eduvm {

OsGuess defaultsFor(GuestOs os) {
    OsGuess g;
    g.os = os;
    switch (os) {
        case GuestOs::Windows:
            g.name = "Windows";
            g.memoryMb = 4096;
            g.diskGb = 64;
            break;
        case GuestOs::Linux:
            g.name = "Linux";
            g.memoryMb = 2048;
            g.diskGb = 20;
            break;
        case GuestOs::Other:
            g.name = "New Virtual Machine";
            g.memoryMb = 1024;
            g.diskGb = 16;
            break;
    }
    return g;
}

OsGuess guessOsFromIso(const std::string& isoPath) {
    std::string file = toLower(pathToUtf8(utf8ToPath(isoPath).filename()));

    // Windows: "Win11_24H2_English_x64.iso", "en-us_windows_10_...", "windows_server_2022"
    if (startsWith(file, "win") || file.find("windows") != std::string::npos) {
        OsGuess g = defaultsFor(GuestOs::Windows);
        g.detected = true;
        std::smatch m;
        if (std::regex_search(file, m, std::regex(R"(server[ _-]?(20\d\d))"))) g.name = "Windows Server " + m[1].str();
        else if (std::regex_search(file, m, std::regex(R"(win(dows)?[ _-]?(1[01]|7|8\.1))"))) g.name = "Windows " + m[2].str();
        if (g.name == "Windows 7" || g.name == "Windows 8.1") g.memoryMb = 2048;
        return g;
    }

    static const std::pair<const char*, const char*> distros[] = {
        {"kubuntu", "Kubuntu"}, {"xubuntu", "Xubuntu"}, {"ubuntu", "Ubuntu"}, {"debian", "Debian"},
        {"fedora", "Fedora"},   {"linuxmint", "Linux Mint"}, {"mint", "Linux Mint"}, {"kali", "Kali Linux"},
        {"alpine", "Alpine Linux"}, {"archlinux", "Arch Linux"}, {"centos", "CentOS"}, {"rocky", "Rocky Linux"},
        {"alma", "AlmaLinux"},  {"opensuse", "openSUSE"}, {"manjaro", "Manjaro"}, {"pop-os", "Pop!_OS"},
        {"zorin", "Zorin OS"},  {"rhel", "Red Hat Enterprise Linux"}, {"parrot", "Parrot OS"}, {"linux", "Linux"},
    };
    for (auto& [key, pretty] : distros) {
        if (file.find(key) == std::string::npos) continue;
        OsGuess g = defaultsFor(GuestOs::Linux);
        g.detected = true;
        g.name = pretty;
        std::smatch m;
        if (std::regex_search(file, m, std::regex(R"((\d+(\.\d+)*))"))) g.name += " " + m[1].str();
        if (std::string(key) == "alpine") {  // tiny distro, tiny defaults
            g.memoryMb = 1024;
            g.diskGb = 8;
        }
        bool desktop = file.find("desktop") != std::string::npos || file.find("live") != std::string::npos ||
                       std::string(key) == "kali" || std::string(key) == "mint" || std::string(key) == "fedora";
        if (desktop) {
            g.memoryMb = 4096;
            g.diskGb = 30;
        }
        return g;
    }
    return defaultsFor(GuestOs::Other);
}

}  // namespace eduvm
