#include "core/VmConfig.hpp"

#include "core/Strings.hpp"

namespace eduvm {

std::string toString(GuestOs os) {
    switch (os) {
        case GuestOs::Linux: return "linux";
        case GuestOs::Windows: return "windows";
        case GuestOs::Other: return "other";
    }
    return "other";
}

GuestOs guestOsFromString(const std::string& s) {
    std::string v = toLower(s);
    if (v == "linux") return GuestOs::Linux;
    if (v == "windows") return GuestOs::Windows;
    if (v == "other") return GuestOs::Other;
    throw Error("Unknown guest OS '" + s + "' (expected linux, windows or other)");
}

std::string toString(DisplayMode d) {
    switch (d) {
        case DisplayMode::Embedded: return "embedded";
        case DisplayMode::Window: return "window";
        case DisplayMode::Headless: return "headless";
    }
    return "embedded";
}

DisplayMode displayModeFromString(const std::string& s) {
    std::string v = toLower(s);
    if (v == "embedded") return DisplayMode::Embedded;
    if (v == "window") return DisplayMode::Window;
    if (v == "headless") return DisplayMode::Headless;
    throw Error("Unknown display mode '" + s + "' (expected embedded, window or headless)");
}

bool isValidVmName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

void to_json(nlohmann::json& j, const VmConfig& c) {
    j = {
        {"name", c.name},
        {"description", c.description},
        {"guestOs", toString(c.guestOs)},
        {"arch", c.arch},
        {"cpus", c.cpus},
        {"memoryMb", c.memoryMb},
        {"diskGb", c.diskGb},
        {"diskFile", c.diskFile},
        {"isoPath", c.isoPath},
        {"display", toString(c.display)},
        {"cpuModel", c.cpuModel},
        {"forwardSsh", c.forwardSsh},
        {"segments", c.segments},
        {"labId", c.labId},
        {"createdAt", c.createdAt},
    };
}

void from_json(const nlohmann::json& j, VmConfig& c) {
    VmConfig d;  // defaults for missing keys keep old vm.json files loadable
    c.name = j.at("name").get<std::string>();
    c.description = j.value("description", d.description);
    c.guestOs = guestOsFromString(j.value("guestOs", toString(d.guestOs)));
    c.arch = j.value("arch", d.arch);
    c.cpus = j.value("cpus", d.cpus);
    c.memoryMb = j.value("memoryMb", d.memoryMb);
    c.diskGb = j.value("diskGb", d.diskGb);
    c.diskFile = j.value("diskFile", d.diskFile);
    c.isoPath = j.value("isoPath", d.isoPath);
    c.display = displayModeFromString(j.value("display", toString(d.display)));
    c.cpuModel = j.value("cpuModel", d.cpuModel);
    c.forwardSsh = j.value("forwardSsh", d.forwardSsh);
    c.segments = j.value("segments", d.segments);
    c.labId = j.value("labId", d.labId);
    c.createdAt = j.value("createdAt", d.createdAt);
}

void to_json(nlohmann::json& j, const VmRuntime& r) {
    j = {{"pid", r.pid}, {"qmpPort", r.qmpPort}, {"qgaPort", r.qgaPort}, {"sshPort", r.sshPort}, {"vncPort", r.vncPort},
         {"startedAt", r.startedAt}};
}

void from_json(const nlohmann::json& j, VmRuntime& r) {
    r.pid = j.value("pid", 0L);
    r.qmpPort = j.value("qmpPort", 0);
    r.qgaPort = j.value("qgaPort", 0);
    r.sshPort = j.value("sshPort", 0);
    r.vncPort = j.value("vncPort", 0);
    r.startedAt = j.value("startedAt", std::string());
}

}  // namespace eduvm
