#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace eduvm {

enum class GuestOs { Linux, Windows, Other };
enum class DisplayMode {
    Embedded,  // shown inside the EduVM window (QEMU serves a local VNC display)
    Window,    // QEMU opens its own window
    Headless,  // no display; reach the VM over SSH
};

// Persistent definition of one VM (vm.json).
struct VmConfig {
    std::string name;         // folder-safe id, e.g. "ubuntu-server"
    std::string displayName;  // what the user typed, e.g. "Ubuntu Server 24.04"
    std::string description;
    GuestOs guestOs = GuestOs::Linux;
    std::string arch = "x86_64";
    int cpus = 2;
    int memoryMb = 2048;
    int diskGb = 20;
    std::string diskFile = "disk.qcow2";  // relative to the VM folder
    std::string isoPath;                   // CD/DVD drive; empty = no disc
    DisplayMode display = DisplayMode::Embedded;
    std::string cpuModel;                  // empty = pick per accelerator
    bool nat = true;                       // network adapter with internet access (NAT)
    bool forwardSsh = true;                // NAT only: host 127.0.0.1:<port> -> guest :22
    // LAN segments: VMs that share a segment name are on the same private
    // virtual switch (like VMware LAN segments). No admin rights needed.
    std::vector<std::string> segments;
    std::string createdAt;
};

// Live state of a running VM (runtime.json). Removed when the VM stops.
struct VmRuntime {
    long pid = 0;
    int qmpPort = 0;
    int sshPort = 0;
    int vncPort = 0;  // only for DisplayMode::Embedded
    std::string startedAt;
};

std::string toString(GuestOs os);
GuestOs guestOsFromString(const std::string& s);
std::string toString(DisplayMode d);
DisplayMode displayModeFromString(const std::string& s);

// VM ids become folder names and QEMU arguments: keep them boring.
bool isValidVmName(const std::string& name);
// "Ubuntu Server 24.04" -> "Ubuntu-Server-24-04"
std::string slugify(const std::string& displayName);
// Name to show in the UI.
inline const std::string& vmTitle(const VmConfig& c) { return c.displayName.empty() ? c.name : c.displayName; }

void to_json(nlohmann::json& j, const VmConfig& c);
void from_json(const nlohmann::json& j, VmConfig& c);
void to_json(nlohmann::json& j, const VmRuntime& r);
void from_json(const nlohmann::json& j, VmRuntime& r);

}  // namespace eduvm
